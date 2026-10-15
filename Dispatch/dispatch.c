#include "dispatch.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* Helper function to read entire file content into dynamically allocated string */
static char* read_file_to_string(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length < 0) {
        fclose(f);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)length, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    return buffer;
}

Buggy* load_fleet(const char* buggies_path, int* count_out) {
    if (!buggies_path || !count_out) return NULL;
    *count_out = 0;

    char* json_data = read_file_to_string(buggies_path);
    if (!json_data) {
        return NULL;
    }

    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) {
        return NULL;
    }

    cJSON* buggies_array = cJSON_GetObjectItemCaseSensitive(root, "buggies");
    if (!cJSON_IsArray(buggies_array)) {
        cJSON_Delete(root);
        return NULL;
    }

    int count = cJSON_GetArraySize(buggies_array);
    if (count <= 0) {
        cJSON_Delete(root);
        return NULL;
    }

    Buggy* fleet = (Buggy*)calloc((size_t)count, sizeof(Buggy));
    if (!fleet) {
        cJSON_Delete(root);
        return NULL;
    }

    int i;
    for (i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(buggies_array, i);
        if (!item) continue;

        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* capacity = cJSON_GetObjectItemCaseSensitive(item, "capacity");
        cJSON* current_location = cJSON_GetObjectItemCaseSensitive(item, "current_location");
        cJSON* status = cJSON_GetObjectItemCaseSensitive(item, "status");
        cJSON* speed_mps = cJSON_GetObjectItemCaseSensitive(item, "speed_mps");

        if (cJSON_IsString(id) && (id->valuestring != NULL)) {
            strncpy(fleet[i].id, id->valuestring, sizeof(fleet[i].id) - 1);
            fleet[i].id[sizeof(fleet[i].id) - 1] = '\0';
        }

        if (cJSON_IsNumber(capacity)) {
            fleet[i].capacity = capacity->valueint;
        }

        if (cJSON_IsNumber(current_location)) {
            fleet[i].current_location = current_location->valueint;
        }

        if (cJSON_IsString(status) && (status->valuestring != NULL)) {
            if (strcmp(status->valuestring, "AVAILABLE") == 0) {
                fleet[i].status = STATUS_AVAILABLE;
            } else {
                fleet[i].status = STATUS_NOT_AVAILABLE;
            }
        } else {
            fleet[i].status = STATUS_NOT_AVAILABLE;
        }

        if (cJSON_IsNumber(speed_mps)) {
            fleet[i].speed_mps = speed_mps->valuedouble;
        }
    }

    *count_out = count;
    cJSON_Delete(root);
    return fleet;
}

Request* load_requests(const char* requests_path, int* count_out) {
    if (!requests_path || !count_out) return NULL;
    *count_out = 0;

    char* json_data = read_file_to_string(requests_path);
    if (!json_data) {
        return NULL;
    }

    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) {
        return NULL;
    }

    cJSON* requests_array = cJSON_GetObjectItemCaseSensitive(root, "requests");
    if (!cJSON_IsArray(requests_array)) {
        cJSON_Delete(root);
        return NULL;
    }

    int count = cJSON_GetArraySize(requests_array);
    if (count <= 0) {
        cJSON_Delete(root);
        return NULL;
    }

    Request* requests = (Request*)calloc((size_t)count, sizeof(Request));
    if (!requests) {
        cJSON_Delete(root);
        return NULL;
    }

    int i;
    for (i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(requests_array, i);
        if (!item) continue;

        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON* pickup = cJSON_GetObjectItemCaseSensitive(item, "pickup");
        cJSON* destination = cJSON_GetObjectItemCaseSensitive(item, "destination");
        cJSON* passengers = cJSON_GetObjectItemCaseSensitive(item, "passengers");
        cJSON* urgency = cJSON_GetObjectItemCaseSensitive(item, "urgency");
        cJSON* request_time = cJSON_GetObjectItemCaseSensitive(item, "request_time");

        if (cJSON_IsString(id) && (id->valuestring != NULL)) {
            strncpy(requests[i].id, id->valuestring, sizeof(requests[i].id) - 1);
            requests[i].id[sizeof(requests[i].id) - 1] = '\0';
        }

        if (cJSON_IsNumber(pickup)) {
            requests[i].pickup = pickup->valueint;
        }

        if (cJSON_IsNumber(destination)) {
            requests[i].destination = destination->valueint;
        }

        if (cJSON_IsNumber(passengers)) {
            requests[i].passengers = passengers->valueint;
        }

        if (cJSON_IsNumber(urgency)) {
            requests[i].urgency = urgency->valueint;
        }

        if (cJSON_IsNumber(request_time)) {
            requests[i].request_time = request_time->valueint;
        }

        requests[i].assigned = 0;
    }

    *count_out = count;
    cJSON_Delete(root);
    return requests;
}

void free_fleet(Buggy* fleet) {
    if (fleet) {
        free(fleet);
    }
}

void free_requests(Request* requests) {
    if (requests) {
        free(requests);
    }
}

double compute_priority(Request* r, double urgency_weight,
                         double aging_weight, int current_time) {
    if (!r) return 0.0;
    int wait_time = current_time - r->request_time;
    if (wait_time < 0) {
        wait_time = 0;
    }
    return (urgency_weight * r->urgency) + (aging_weight * (double)wait_time);
}

AssignmentResult run_dispatch_tick(Buggy* fleet, int fleet_count,
                                    Request* pending, int pending_count,
                                    Graph* g, int current_time,
                                    double urgency_weight,
                                    double aging_weight) {
    AssignmentResult result;
    memset(&result, 0, sizeof(AssignmentResult));
    result.success = 0;

    if (!fleet || fleet_count <= 0 || !pending || pending_count <= 0) {
        return result;
    }

    /* 1. Find the highest priority unassigned request that has arrived (request_time <= current_time) */
    int best_request_idx = -1;
    double highest_priority = -DBL_MAX;

    int i;
    for (i = 0; i < pending_count; i++) {
        if (pending[i].assigned) {
            continue;
        }
        if (pending[i].request_time > current_time) {
            continue; /* Has not arrived yet */
        }

        double prio = compute_priority(&pending[i], urgency_weight, aging_weight, current_time);
        if (prio > highest_priority) {
            highest_priority = prio;
            best_request_idx = i;
        }
    }

    if (best_request_idx == -1) {
        /* No valid pending requests to serve right now */
        return result;
    }

    Request* target_req = &pending[best_request_idx];

    /* 2. Find candidate buggies: AVAILABLE and capacity >= passengers */
    int best_buggy_idx = -1;
    int min_travel_time = 1000000000;

    for (i = 0; i < fleet_count; i++) {
        if (fleet[i].status != STATUS_AVAILABLE) {
            continue;
        }
        if (fleet[i].capacity < target_req->passengers) {
            continue;
        }

        /* Calculate travel time from buggy location to pickup point */
        Route* r = dijkstra(g, fleet[i].current_location, target_req->pickup);
        if (r) {
            int travel_time = r->total_time_s;
            free_route(r);

            if (travel_time < min_travel_time) {
                min_travel_time = travel_time;
                best_buggy_idx = i;
            }
        }
    }

    if (best_buggy_idx == -1) {
        /* No suitable buggy available */
        return result;
    }

    /* 3. Make assignment */
    Buggy* assigned_b = &fleet[best_buggy_idx];
    assigned_b->status = STATUS_NOT_AVAILABLE;
    target_req->assigned = 1;

    result.success = 1;
    strncpy(result.assigned_buggy, assigned_b->id, sizeof(result.assigned_buggy) - 1);
    result.assigned_buggy[sizeof(result.assigned_buggy) - 1] = '\0';

    strncpy(result.request_id, target_req->id, sizeof(result.request_id) - 1);
    result.request_id[sizeof(result.request_id) - 1] = '\0';

    result.wait_time_s = current_time - target_req->request_time;
    if (result.wait_time_s < 0) result.wait_time_s = 0;

    result.eta_s = min_travel_time;

    return result;
}
