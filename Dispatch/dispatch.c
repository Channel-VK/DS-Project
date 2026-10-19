#include "dispatch.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>

static char* read_file_to_string(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) { fclose(f); return NULL; }
    size_t read_bytes = fread(buffer, 1, (size_t)length, f);
    if (ferror(f)) { free(buffer); fclose(f); return NULL; }
    buffer[read_bytes] = '\0';
    fclose(f);
    return buffer;
}

Buggy* load_fleet(const char* buggies_path, int* count_out) {
    if (!buggies_path || !count_out) return NULL;
    *count_out = 0;
    char* json_data = read_file_to_string(buggies_path);
    if (!json_data) return NULL;
    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) return NULL;
    cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "buggies");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); return NULL; }
    int count = cJSON_GetArraySize(array);
    if (count <= 0) { cJSON_Delete(root); return NULL; }
    Buggy* fleet = (Buggy*)calloc((size_t)count, sizeof(Buggy));
    if (!fleet) { cJSON_Delete(root); return NULL; }
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(array, i);
        if (!item) continue;
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* capacity = cJSON_GetObjectItemCaseSensitive(item, "capacity");
        cJSON* location = cJSON_GetObjectItemCaseSensitive(item, "current_location");
        cJSON* status = cJSON_GetObjectItemCaseSensitive(item, "status");
        cJSON* speed = cJSON_GetObjectItemCaseSensitive(item, "speed_mps");
        if (cJSON_IsString(id) && id->valuestring)
            snprintf(fleet[i].id, sizeof(fleet[i].id), "%s", id->valuestring);
        if (cJSON_IsNumber(capacity)) fleet[i].capacity = capacity->valueint;
        if (cJSON_IsNumber(location)) fleet[i].current_location = location->valueint;
        fleet[i].status = (cJSON_IsString(status) && status->valuestring &&
                           strcmp(status->valuestring, "AVAILABLE") == 0)
                              ? STATUS_AVAILABLE : STATUS_NOT_AVAILABLE;
        if (cJSON_IsNumber(speed)) fleet[i].speed_mps = speed->valuedouble;
    }
    *count_out = count;
    cJSON_Delete(root);
    return fleet;
}

Request* load_requests(const char* requests_path, int* count_out) {
    if (!requests_path || !count_out) return NULL;
    *count_out = 0;
    char* json_data = read_file_to_string(requests_path);
    if (!json_data) return NULL;
    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) return NULL;
    cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "requests");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); return NULL; }
    int count = cJSON_GetArraySize(array);
    if (count <= 0) { cJSON_Delete(root); return NULL; }
    Request* requests = (Request*)calloc((size_t)count, sizeof(Request));
    if (!requests) { cJSON_Delete(root); return NULL; }
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(array, i);
        if (!item) continue;
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* pickup = cJSON_GetObjectItemCaseSensitive(item, "pickup");
        cJSON* destination = cJSON_GetObjectItemCaseSensitive(item, "destination");
        cJSON* passengers = cJSON_GetObjectItemCaseSensitive(item, "passengers");
        cJSON* urgency = cJSON_GetObjectItemCaseSensitive(item, "urgency");
        cJSON* request_time = cJSON_GetObjectItemCaseSensitive(item, "request_time");
        if (cJSON_IsString(id) && id->valuestring)
            snprintf(requests[i].id, sizeof(requests[i].id), "%s", id->valuestring);
        if (cJSON_IsNumber(pickup)) requests[i].pickup = pickup->valueint;
        if (cJSON_IsNumber(destination)) requests[i].destination = destination->valueint;
        if (cJSON_IsNumber(passengers)) requests[i].passengers = passengers->valueint;
        if (cJSON_IsNumber(urgency)) requests[i].urgency = urgency->valueint;
        if (cJSON_IsNumber(request_time)) requests[i].request_time = request_time->valueint;
        requests[i].assigned = 0;
    }
    *count_out = count;
    cJSON_Delete(root);
    return requests;
}

void free_fleet(Buggy* fleet) { free(fleet); }
void free_requests(Request* requests) { free(requests); }

double compute_priority(Request* r, double urgency_weight,
                        double aging_weight, int current_time) {
    if (!r) return 0.0;
    int wait_time = current_time - r->request_time;
    if (wait_time < 0) wait_time = 0;
    return urgency_weight * r->urgency + aging_weight * (double)wait_time;
}

AssignmentResult run_dispatch_tick(Buggy* fleet, int fleet_count,
                                   Request* pending, int pending_count,
                                   Graph* g, int current_time,
                                   double urgency_weight, double aging_weight,
                                   int max_simulation_time, const char* algorithm) {
    AssignmentResult result;
    memset(&result, 0, sizeof(result));
    if (!fleet || fleet_count <= 0 || !pending || pending_count <= 0 || !g)
        return result;

    int best_request_idx = -1, best_buggy_idx = -1, best_eta = INT_MAX;
    double highest_priority = -DBL_MAX;
    int shortest_total_duration = INT_MAX;
    int shortest_mode = (algorithm && strcmp(algorithm, "shortest") == 0);

    for (int i = 0; i < pending_count; ++i) {
        Request* request = &pending[i];
        if (request->assigned || request->request_time > current_time) continue;

        int nearest_buggy_idx = -1, nearest_eta = INT_MAX;
        for (int j = 0; j < fleet_count; ++j) {
            if (fleet[j].status != STATUS_AVAILABLE ||
                fleet[j].capacity < request->passengers) continue;
            Route* route = dijkstra(g, fleet[j].current_location, request->pickup);
            if (!route) continue;
            int eta = route->total_time_s;
            free_route(route);
            if (eta < 0 || eta >= nearest_eta) continue;
            nearest_eta = eta;
            nearest_buggy_idx = j;
        }
        if (nearest_buggy_idx < 0) continue;

        /* Reject requests whose pickup + passenger trip cannot finish in-window. */
        Route* trip_route = dijkstra(g, request->pickup, request->destination);
        if (!trip_route) continue;
        int trip_time = trip_route->total_time_s;
        free_route(trip_route);
        if (trip_time < 0 || current_time + nearest_eta + trip_time > max_simulation_time)
            continue;

        int total_duration = nearest_eta + trip_time;
        double priority = compute_priority(request, urgency_weight,
                                           aging_weight, current_time);
        if (best_request_idx == -1 ||
            (shortest_mode && total_duration < shortest_total_duration) ||
            (!shortest_mode && priority > highest_priority)) {
            highest_priority = priority;
            shortest_total_duration = total_duration;
            best_request_idx = i;
            best_buggy_idx = nearest_buggy_idx;
            best_eta = nearest_eta;
        }
    }

    if (best_request_idx < 0) return result;
    Request* request = &pending[best_request_idx];
    Buggy* buggy = &fleet[best_buggy_idx];
    request->assigned = 1;
    buggy->status = STATUS_NOT_AVAILABLE;
    result.success = 1;
    snprintf(result.assigned_buggy, sizeof(result.assigned_buggy), "%s", buggy->id);
    snprintf(result.request_id, sizeof(result.request_id), "%s", request->id);
    result.eta_s = best_eta;
    result.wait_time_s = current_time - request->request_time;
    if (result.wait_time_s < 0) result.wait_time_s = 0;
    return result;
}
