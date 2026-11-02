#include "dispatch.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>
#include <errno.h>

static char* read_file_to_string(const char* filepath) {
    if (!filepath) { errno = EINVAL; return NULL; }
    FILE* f = fopen(filepath, "rb");
    if (!f) { if (errno == 0) errno = EIO; return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) {
        int err = errno ? errno : EIO;
        fclose(f);
        errno = err;
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET) != 0) {
        int err = errno ? errno : EIO;
        fclose(f);
        errno = err;
        return NULL;
    }
    char* buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) { fclose(f); errno = ENOMEM; return NULL; }
    size_t read_bytes = fread(buffer, 1, (size_t)length, f);
    if (read_bytes != (size_t)length || ferror(f)) {
        int err = errno ? errno : EIO;
        free(buffer);
        fclose(f);
        errno = err;
        return NULL;
    }
    buffer[read_bytes] = '\0';
    fclose(f);
    return buffer;
}

Buggy* load_fleet(const char* buggies_path, int* count_out) {
    if (!buggies_path || !count_out) { errno = EINVAL; return NULL; }
    *count_out = 0;
    char* json_data = read_file_to_string(buggies_path);
    if (!json_data) return NULL;
    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) { errno = EINVAL; return NULL; }
    cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "buggies");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); errno = EINVAL; return NULL; }
    int count = cJSON_GetArraySize(array);
    if (count <= 0) { cJSON_Delete(root); errno = EINVAL; return NULL; }
    Buggy* fleet = (Buggy*)calloc((size_t)count, sizeof(Buggy));
    if (!fleet) { cJSON_Delete(root); errno = ENOMEM; return NULL; }
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsObject(item)) {
            free(fleet);
            cJSON_Delete(root);
            errno = EINVAL;
            return NULL;
        }
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* capacity = cJSON_GetObjectItemCaseSensitive(item, "capacity");
        cJSON* location = cJSON_GetObjectItemCaseSensitive(item, "current_location");
        cJSON* status = cJSON_GetObjectItemCaseSensitive(item, "status");
        cJSON* speed = cJSON_GetObjectItemCaseSensitive(item, "speed_mps");
        if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] ||
            !cJSON_IsNumber(capacity) || capacity->valueint < 0 ||
            !cJSON_IsNumber(location)) {
            free(fleet);
            cJSON_Delete(root);
            errno = EINVAL;
            return NULL;
        }
        snprintf(fleet[i].id, sizeof(fleet[i].id), "%s", id->valuestring);
        fleet[i].capacity = capacity->valueint;
        fleet[i].current_location = location->valueint;
        fleet[i].status = ((cJSON_IsString(status) && status->valuestring &&
                            strcmp(status->valuestring, "AVAILABLE") == 0) ||
                           (cJSON_IsNumber(status) && status->valueint == STATUS_AVAILABLE) ||
                           cJSON_IsTrue(status))
                              ? STATUS_AVAILABLE : STATUS_NOT_AVAILABLE;
        if (cJSON_IsNumber(speed)) fleet[i].speed_mps = speed->valuedouble;
    }
    *count_out = count;
    cJSON_Delete(root);
    return fleet;
}

Request* load_requests(const char* requests_path, int* count_out) {
    if (!requests_path || !count_out) { errno = EINVAL; return NULL; }
    *count_out = 0;
    char* json_data = read_file_to_string(requests_path);
    if (!json_data) return NULL;
    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) { errno = EINVAL; return NULL; }
    cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "requests");
    if (!cJSON_IsArray(array)) { cJSON_Delete(root); errno = EINVAL; return NULL; }
    int count = cJSON_GetArraySize(array);
    if (count <= 0) { cJSON_Delete(root); errno = EINVAL; return NULL; }
    Request* requests = (Request*)calloc((size_t)count, sizeof(Request));
    if (!requests) { cJSON_Delete(root); errno = ENOMEM; return NULL; }
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsObject(item)) {
            free(requests);
            cJSON_Delete(root);
            errno = EINVAL;
            return NULL;
        }
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* pickup = cJSON_GetObjectItemCaseSensitive(item, "pickup");
        cJSON* destination = cJSON_GetObjectItemCaseSensitive(item, "destination");
        cJSON* passengers = cJSON_GetObjectItemCaseSensitive(item, "passengers");
        cJSON* urgency = cJSON_GetObjectItemCaseSensitive(item, "urgency");
        cJSON* request_time = cJSON_GetObjectItemCaseSensitive(item, "request_time");
        if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] ||
            !cJSON_IsNumber(pickup) || !cJSON_IsNumber(destination) ||
            !cJSON_IsNumber(passengers) || passengers->valueint <= 0 ||
            !cJSON_IsNumber(urgency) || !cJSON_IsNumber(request_time)) {
            free(requests);
            cJSON_Delete(root);
            errno = EINVAL;
            return NULL;
        }
        snprintf(requests[i].id, sizeof(requests[i].id), "%s", id->valuestring);
        requests[i].pickup = pickup->valueint;
        requests[i].destination = destination->valueint;
        requests[i].passengers = passengers->valueint;
        requests[i].urgency = urgency->valueint;
        requests[i].request_time = request_time->valueint;
        requests[i].assigned = 0;
    }
    *count_out = count;
    cJSON_Delete(root);
    return requests;
}

void free_fleet(Buggy* fleet) {
    int saved_errno = errno;
    free(fleet);
    errno = saved_errno;
}

void free_requests(Request* requests) {
    int saved_errno = errno;
    free(requests);
    errno = saved_errno;
}

double compute_priority(Request* r, double urgency_weight,
                        double aging_weight, int current_time) {
    if (!r) { errno = EINVAL; return 0.0; }
    double wait_time = (double)current_time - (double)r->request_time;
    if (wait_time < 0.0) wait_time = 0.0;
    return urgency_weight * (double)r->urgency + aging_weight * wait_time;
}

AssignmentResult run_dispatch_tick(Buggy* fleet, int fleet_count,
                                   Request* pending, int pending_count,
                                   Graph* g, int current_time,
                                   double urgency_weight, double aging_weight,
                                   int max_simulation_time, const char* algorithm) {
    AssignmentResult result;
    memset(&result, 0, sizeof(result));
    if (!fleet || fleet_count <= 0 || !pending || pending_count <= 0 || !g ||
        current_time < 0 || max_simulation_time < 0) {
        errno = EINVAL;
        return result;
    }

    int best_request_idx = -1, best_buggy_idx = -1, best_eta = INT_MAX;
    double highest_priority = -DBL_MAX;
    int shortest_total_duration = INT_MAX;
    int shortest_mode = (algorithm && strcmp(algorithm, "shortest") == 0);
    int regret_mode = (algorithm && (strcmp(algorithm, "regret") == 0 ||
                                     strcmp(algorithm, "smart") == 0));

    for (int i = 0; i < pending_count; ++i) {
        Request* request = &pending[i];
        if (request->assigned || request->passengers <= 0 ||
            request->request_time > current_time) continue;

        int nearest_buggy_idx = -1, nearest_eta = INT_MAX, second_best_eta = INT_MAX;
        int nearest_dist = INT_MAX, nearest_hops = INT_MAX;
        for (int j = 0; j < fleet_count; ++j) {
            if (fleet[j].status != STATUS_AVAILABLE ||
                fleet[j].capacity < request->passengers) continue;
            Route* route = dijkstra(g, fleet[j].current_location, request->pickup);
            if (!route) continue;
            int eta = route->total_time_s;
            int dist = route->total_distance_m;
            int hops = route->path_length;
            free_route(route);
            if (eta < 0 || dist < 0) continue;
            if (eta < nearest_eta ||
                (eta == nearest_eta && (dist < nearest_dist ||
                                        (dist == nearest_dist && hops < nearest_hops)))) {
                second_best_eta = nearest_eta;
                nearest_eta = eta;
                nearest_dist = dist;
                nearest_hops = hops;
                nearest_buggy_idx = j;
            } else if (eta < second_best_eta) {
                second_best_eta = eta;
            }
        }
        if (nearest_buggy_idx < 0) continue;

        /* Reject requests whose pickup + passenger trip cannot finish in-window. */
        Route* trip_route = dijkstra(g, request->pickup, request->destination);
        if (!trip_route) continue;
        int trip_time = trip_route->total_time_s;
        free_route(trip_route);
        if (trip_time < 0 || nearest_eta > max_simulation_time - current_time ||
            trip_time > max_simulation_time - current_time - nearest_eta)
            continue;

        int total_duration = nearest_eta + trip_time;
        double priority = compute_priority(request, urgency_weight,
                                           aging_weight, current_time);
        if (regret_mode) {
            double regret = (second_best_eta == INT_MAX)
                                ? 300.0 : (double)(second_best_eta - nearest_eta);
            priority += 0.15 * regret;
        }
        int better = 0;
        if (best_request_idx == -1) {
            better = 1;
        } else if (shortest_mode) {
            better = (total_duration < shortest_total_duration) ||
                     (total_duration == shortest_total_duration && priority > highest_priority);
        } else {
            better = (priority > highest_priority) ||
                     (priority == highest_priority && nearest_eta < best_eta);
        }
        if (better) {
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
