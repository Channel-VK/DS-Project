#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "GraphRouting/graph.h"
#include "Dispatch/dispatch.h"
#include "Output/outputwriter.h"
#include "lib/cJSON.h"


#define DEFAULT_URGENCY_WEIGHT   1.0
#define DEFAULT_AGING_WEIGHT     0.5
#define DEFAULT_TIME_STEP_S      10
#define DEFAULT_MAX_TIME_S       1800

static void make_path(char *destination, size_t size,
                      const char *directory, const char *filename) {
    int n;
    size_t length;
    if (!destination || size == 0 || !directory || !filename) return;
    length = strlen(directory);
    n = snprintf(destination, size, "%s%s%s", directory,
                 (length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\')) ? "" : "/",
                 filename);
    if (n < 0 || (size_t)n >= size) destination[0] = '\0';
}

/* Configuration fields used by the simulation driver. */
typedef struct {
    char algorithm[32];
    char routing[32];
    char output_format[16];
    double urgency_weight;
    double aging_weight;
    int time_step_s;
    int max_simulation_time_s;
} SimulationConfig;

static char *read_file(const char *path) {
    FILE *file;
    long length;
    size_t bytes_read;
    char *buffer;

    file = fopen(path, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    bytes_read = fread(buffer, 1, (size_t)length, file);
    if (ferror(file)) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static void copy_json_string(cJSON *object, const char *key,
                             char *destination, size_t destination_size,
                             const char *fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    const char *value = (cJSON_IsString(item) && item->valuestring)
                            ? item->valuestring : fallback;
    if (destination_size == 0) return;
    snprintf(destination, destination_size, "%s", value);
}

static double get_json_number(cJSON *object, const char *key, double fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static int load_config(const char *path, SimulationConfig *config) {
    char *json_text = read_file(path);
    cJSON *root;

    if (!json_text) {
        fprintf(stderr, "Error: could not read config file '%s'.\n", path);
        return 0;
    }

    root = cJSON_Parse(json_text);
    free(json_text);
    if (!root || !cJSON_IsObject(root)) {
        fprintf(stderr, "Error: '%s' is not valid JSON object data.\n", path);
        cJSON_Delete(root);
        return 0;
    }

    copy_json_string(root, "algorithm", config->algorithm,
                     sizeof(config->algorithm), "greedy");
    copy_json_string(root, "routing", config->routing,
                     sizeof(config->routing), "dijkstra");
    copy_json_string(root, "output_format", config->output_format,
                     sizeof(config->output_format), "json");
    config->urgency_weight = get_json_number(root, "urgency_weight",
                                             DEFAULT_URGENCY_WEIGHT);
    config->aging_weight = get_json_number(root, "aging_weight",
                                           DEFAULT_AGING_WEIGHT);
    config->time_step_s = (int)get_json_number(root, "time_step_s",
                                               DEFAULT_TIME_STEP_S);
    config->max_simulation_time_s = (int)get_json_number(
        root, "max_simulation_time_s", DEFAULT_MAX_TIME_S);

    cJSON_Delete(root);

    if (config->time_step_s <= 0 || config->max_simulation_time_s <= 0) {
        fprintf(stderr, "Error: time_step_s and max_simulation_time_s must be positive.\n");
        return 0;
    }
    if (strcmp(config->output_format, "json") != 0 &&
        strcmp(config->output_format, "csv") != 0) {
        fprintf(stderr, "Error: output_format must be 'json' or 'csv'.\n");
        return 0;
    }
    return 1;
}

/* Classify structural feasibility, not whether there is enough time left at
   the final instant. A request is structurally feasible if at least one
   online buggy has enough capacity and both routes exist in the graph. */
static int request_structurally_feasible(const Buggy *fleet,
                                         const int *offline,
                                         int fleet_count,
                                         const Request *request,
                                         Graph *graph) {
    int j;

    for (j = 0; j < fleet_count; ++j) {
        Route *pickup_route;
        Route *trip_route;

        if (offline[j] || fleet[j].capacity < request->passengers) {
            continue;
        }

        pickup_route = dijkstra(graph, fleet[j].current_location, request->pickup);
        if (!pickup_route) {
            continue;
        }
        free_route(pickup_route);

        trip_route = dijkstra(graph, request->pickup, request->destination);
        if (!trip_route) {
            continue;
        }
        free_route(trip_route);
        return 1;
    }

    return 0;
}

static int find_buggy_index(const Buggy *fleet, int fleet_count, const char *id) {
    int i;
    for (i = 0; i < fleet_count; ++i) {
        if (strcmp(fleet[i].id, id) == 0) return i;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    SimulationConfig config;
    Graph *graph = NULL;
    Buggy *fleet = NULL;
    Request *requests = NULL;
    int fleet_count = 0;
    int request_count = 0;
    int *busy_until = NULL;
    int *destination_after_trip = NULL;
    int *offline = NULL;
    double *busy_seconds = NULL;
    RequestOutcome *outcomes = NULL;
    int served = 0;
    int current_time;
    int i;
    int success = EXIT_FAILURE;
    Metrics metrics;
    char output_path[512];
    char config_path[512];
    char nodes_path[512];
    char edges_path[512];
    char buggies_path[512];
    char requests_path[512];
    const char *data_dir = "Data";

/*
 * Some launch environments pass the executable path as an argument.
 * Skip that path and use the first actual scenario directory instead.
 */
for (int argi = 1; argi < argc; ++argi) {
    if (argv[argi] == NULL || argv[argi][0] == '\0') {
        continue;
    }

    if (strstr(argv[argi], "buggy_dispatch.exe") != NULL) {
        continue;
    }

    data_dir = argv[argi];
    break;
}

    memset(&config, 0, sizeof(config));
    make_path(config_path, sizeof(config_path), data_dir, "simulation_config.json");
    make_path(nodes_path, sizeof(nodes_path), data_dir, "campus_nodes.json");
    make_path(edges_path, sizeof(edges_path), data_dir, "campus_edges.json");
    make_path(buggies_path, sizeof(buggies_path), data_dir, "campus_buggies.json");
    make_path(requests_path, sizeof(requests_path), data_dir, "requests.json");
    if (!config_path[0] || !nodes_path[0] || !edges_path[0] ||
        !buggies_path[0] || !requests_path[0]) {
        fprintf(stderr, "Error: scenario path is too long.\n");
        goto cleanup;
    }

    if (!load_config(config_path, &config)) goto cleanup;

    graph = load_graph(nodes_path, edges_path);
    if (!graph) {
        fprintf(stderr, "Error: failed to load campus graph.\n");
        goto cleanup;
    }

    fleet = load_fleet(buggies_path, &fleet_count);
    if (!fleet || fleet_count <= 0) {
        fprintf(stderr, "Error: failed to load buggy fleet.\n");
        goto cleanup;
    }

    requests = load_requests(requests_path, &request_count);
    if (!requests || request_count < 0) {
        fprintf(stderr, "Error: failed to load requests.\n");
        goto cleanup;
    }

    busy_until = (int *)calloc((size_t)fleet_count, sizeof(int));
    destination_after_trip = (int *)calloc((size_t)fleet_count, sizeof(int));
    offline = (int *)calloc((size_t)fleet_count, sizeof(int));
    busy_seconds = (double *)calloc((size_t)fleet_count, sizeof(double));
    outcomes = (RequestOutcome *)calloc((size_t)request_count, sizeof(RequestOutcome));
    if (!busy_until || !destination_after_trip || !offline || !busy_seconds ||
        (request_count > 0 && !outcomes)) {
        fprintf(stderr, "Error: out of memory while preparing simulation.\n");
        goto cleanup;
    }

    for (i = 0; i < fleet_count; ++i) {
        busy_until[i] = 0;
        destination_after_trip[i] = fleet[i].current_location;
        /* Preserve the JSON initial status: NOT_AVAILABLE means offline all run. */
        offline[i] = (fleet[i].status != STATUS_AVAILABLE);
    }

    /*
     * The requests array is also the pending queue: dispatch.c skips requests
     * whose assigned flag is set and requests that have not arrived yet.
     * This avoids copying requests and keeps assigned flags consistent.
     */
    for (current_time = 0;
         current_time < config.max_simulation_time_s;
         current_time += config.time_step_s) {

        /* Release buggies whose trips have finished. */
        for (i = 0; i < fleet_count; ++i) {
            if (offline[i]) {
                fleet[i].status = STATUS_NOT_AVAILABLE;
                continue;
            }
            if (busy_until[i] <= current_time) {
                if (busy_until[i] > 0) {
                    fleet[i].current_location = destination_after_trip[i];
                }
                fleet[i].status = STATUS_AVAILABLE;
            } else {
                fleet[i].status = STATUS_NOT_AVAILABLE;
            }
        }

        /* Assign as many requests as possible during this simulation tick. */
        for (;;) {
            AssignmentResult assignment = run_dispatch_tick(
                fleet, fleet_count, requests, request_count, graph,
                current_time, config.urgency_weight, config.aging_weight,
                config.max_simulation_time_s, config.algorithm);
            int buggy_index;
            int request_index = -1;
            Route *trip_route;
            int trip_time;
            int total_duration;

            if (!assignment.success) break;

            buggy_index = find_buggy_index(fleet, fleet_count,
                                           assignment.assigned_buggy);
            if (buggy_index < 0) {
                fprintf(stderr, "Error: dispatch returned unknown buggy '%s'.\n",
                        assignment.assigned_buggy);
                goto cleanup;
            }

            for (i = 0; i < request_count; ++i) {
                if (strcmp(requests[i].id, assignment.request_id) == 0) {
                    request_index = i;
                    break;
                }
            }
            if (request_index < 0) {
                fprintf(stderr, "Error: dispatch returned unknown request '%s'.\n",
                        assignment.request_id);
                goto cleanup;
            }

            /* ETA is from the buggy's current location to the pickup. */
            trip_route = dijkstra(graph, requests[request_index].pickup,
                                  requests[request_index].destination);
            if (!trip_route) {
                fprintf(stderr, "Warning: no route for request %s; assignment skipped.\n",
                        requests[request_index].id);
                /* dispatch already marked this request assigned. Keep simulation consistent
                   by recording it as unserved rather than silently losing it. */
                requests[request_index].assigned = 0;
                fleet[buggy_index].status = STATUS_AVAILABLE;
                break;
            }
            trip_time = trip_route->total_time_s;
            free_route(trip_route);
            if (trip_time < 0) trip_time = 0;

            total_duration = assignment.eta_s + trip_time;
            if (total_duration < 0) total_duration = 0;

            /* DEBUG: show exactly what each successful assignment costs. */
            printf("[ASSIGN] t=%d | Request=%s | Buggy=%s | Pickup=%d | Destination=%d | ",
                   current_time,
                   assignment.request_id,
                   assignment.assigned_buggy,
                   requests[request_index].pickup,
                   requests[request_index].destination);
            printf("Pickup ETA=%d s | Passenger trip=%d s | Busy total=%d s | Wait=%d s\n",
                   assignment.eta_s,
                   trip_time,
                   total_duration,
                   (current_time + assignment.eta_s - requests[request_index].request_time) > 0
                       ? (current_time + assignment.eta_s - requests[request_index].request_time)
                       : 0);

            /* Final wait includes the time already waited plus travel to pickup. */
            outcomes[served].wait_time_s = current_time + assignment.eta_s -
                                           requests[request_index].request_time;
            if (outcomes[served].wait_time_s < 0) outcomes[served].wait_time_s = 0;
            outcomes[served].eta_s = assignment.eta_s;
            snprintf(outcomes[served].request_id,
                     sizeof(outcomes[served].request_id), "%s",
                     requests[request_index].id);
            snprintf(outcomes[served].assigned_buggy,
                     sizeof(outcomes[served].assigned_buggy), "%s",
                     assignment.assigned_buggy);
            ++served;

            busy_until[buggy_index] = current_time + total_duration;
            destination_after_trip[buggy_index] = requests[request_index].destination;
            fleet[buggy_index].status = STATUS_NOT_AVAILABLE;

            /* Utilization counts only busy time that falls inside the simulation window. */
            if (current_time < config.max_simulation_time_s) {
                int available_window = config.max_simulation_time_s - current_time;
                busy_seconds[buggy_index] += (double)(total_duration < available_window
                                                         ? total_duration : available_window);
            }
        }
    }

    memset(&metrics, 0, sizeof(metrics));
    metrics.algorithm = config.algorithm;
    metrics.routing = config.routing;
    metrics.total_requests = request_count;
    metrics.served = served;
    metrics.unserved = request_count - served;

    if (served > 0) {
        double total_wait = 0.0;
        for (i = 0; i < served; ++i) total_wait += outcomes[i].wait_time_s;
        metrics.avg_wait_time_s = total_wait / (double)served;
    }

    {
        double total_busy = 0.0;
        int online_count = 0;
        double total_available_time;
        for (i = 0; i < fleet_count; ++i) {
            if (!offline[i]) {
                total_busy += busy_seconds[i];
                ++online_count;
            }
            printf("[UTILIZATION] Buggy=%s | Status=%s | Busy=%.0f s / %d s\n",
                   fleet[i].id,
                   offline[i] ? "OFFLINE" : "ONLINE",
                   busy_seconds[i], config.max_simulation_time_s);
        }
        total_available_time = (double)online_count *
                               (double)config.max_simulation_time_s;
        metrics.avg_buggy_utilization_pct = total_available_time > 0.0
            ? (total_busy / total_available_time) * 100.0 : 0.0;
        if (metrics.avg_buggy_utilization_pct > 100.0)
            metrics.avg_buggy_utilization_pct = 100.0;
    }

    make_path(output_path, sizeof(output_path), data_dir,
              (strcmp(config.output_format, "csv") == 0)
                  ? "results.csv" : "results.json");
    if (!output_path[0]) {
        fprintf(stderr, "Error: results path is too long.\n");
        goto cleanup;
    }
    if (strcmp(config.output_format, "csv") == 0) {
        if (!write_results_csv(output_path, &metrics, outcomes, served)) {
            fprintf(stderr, "Error: failed to write results to '%s'.\n", output_path);
            goto cleanup;
        }
    } else {
        if (!write_results_json(output_path, &metrics, outcomes, served)) {
            fprintf(stderr, "Error: failed to write results to '%s'.\n", output_path);
            goto cleanup;
        }
    }

    {
        int feasible_but_waiting = 0;
        int infeasible_at_deadline = 0;
        int not_yet_arrived = 0;
        for (i = 0; i < request_count; ++i) {
            if (requests[i].assigned) continue;

            if (requests[i].request_time >= config.max_simulation_time_s) {
                ++not_yet_arrived;
            } else if (request_structurally_feasible(
                           fleet, offline, fleet_count, &requests[i], graph)) {
                ++feasible_but_waiting;
            } else {
                ++infeasible_at_deadline;
            }
        }

        printf("Algorithm: %s\n", config.algorithm);
        printf("Simulation complete.\n");
        printf("Requests: %d | Assigned: %d | Unassigned: %d\n",
               metrics.total_requests, served, request_count - served);
        printf("Unserved breakdown: Structurally feasible but unserved: %d | Structurally infeasible: %d | Not yet arrived: %d\n",
               feasible_but_waiting, infeasible_at_deadline, not_yet_arrived);
    }
    printf("Average wait: %.1f s | Average buggy utilization: %.1f%%\n",
           metrics.avg_wait_time_s, metrics.avg_buggy_utilization_pct);
    printf("Results written to %s\n", output_path);
    success = EXIT_SUCCESS;

cleanup:
    free(busy_until);
    free(destination_after_trip);
    free(offline);
    free(busy_seconds);
    free(outcomes);
    free_requests(requests);
    free_fleet(fleet);
    free_graph(graph);
    return success;
}
