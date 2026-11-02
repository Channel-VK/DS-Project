#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

#include "GraphRouting/graph.h"
#include "Dispatch/dispatch.h"
#include "Output/outputwriter.h"
#include "lib/cJSON.h"

#ifndef ENAMETOOLONG
#define ENAMETOOLONG ERANGE
#endif

#define DEFAULT_URGENCY_WEIGHT   1.0
#define DEFAULT_AGING_WEIGHT     0.5
#define DEFAULT_TIME_STEP_S      10
#define DEFAULT_MAX_TIME_S       1800

typedef enum {
    ERR_OK = 0,
    ERR_PATH_TOO_LONG = 1,
    ERR_CONFIG_READ = 2,
    ERR_CONFIG_PARSE = 3,
    ERR_CONFIG_INVALID = 4,
    ERR_GRAPH_LOAD = 5,
    ERR_FLEET_LOAD = 6,
    ERR_REQUESTS_LOAD = 7,
    ERR_OUT_OF_MEMORY = 8,
    ERR_UNKNOWN_BUGGY = 9,
    ERR_UNKNOWN_REQUEST = 10,
    ERR_RESULTS_WRITE = 11
} ErrorCode;

static const char *error_message(ErrorCode code) {
    switch (code) {
        case ERR_OK:              return "success";
        case ERR_PATH_TOO_LONG:   return "scenario or results path is too long";
        case ERR_CONFIG_READ:     return "could not read config file";
        case ERR_CONFIG_PARSE:    return "config file is not valid JSON object data";
        case ERR_CONFIG_INVALID:  return "invalid simulation configuration values";
        case ERR_GRAPH_LOAD:      return "failed to load campus graph";
        case ERR_FLEET_LOAD:      return "failed to load buggy fleet";
        case ERR_REQUESTS_LOAD:   return "failed to load requests";
        case ERR_OUT_OF_MEMORY:   return "out of memory while preparing simulation";
        case ERR_UNKNOWN_BUGGY:   return "dispatch returned unknown buggy";
        case ERR_UNKNOWN_REQUEST: return "dispatch returned unknown request";
        case ERR_RESULTS_WRITE:   return "failed to write results";
        default:                  return "unknown error";
    }
}

static void make_path(char *destination, size_t size,
                      const char *directory, const char *filename) {
    int n;
    size_t length;
    if (!destination || size == 0) {
        errno = EINVAL;
        return;
    }
    destination[0] = '\0';
    if (!directory || !filename) {
        errno = EINVAL;
        return;
    }
    length = strlen(directory);
    n = snprintf(destination, size, "%s%s%s", directory,
                 (length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\')) ? "" : "/",
                 filename);
    if (n < 0 || (size_t)n >= size) {
        destination[0] = '\0';
        errno = ENAMETOOLONG;
    }
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

typedef struct {
    Graph *graph;
    Buggy *fleet;
    Request *requests;
    int fleet_count;
    int request_count;
    int *busy_until;
    int *destination_after_trip;
    int *offline;
    double *busy_seconds;
    RequestOutcome *outcomes;
} SimulationState;

static void free_simulation_state(SimulationState *sim) {
    int saved_errno = errno;
    if (!sim) return;
    free(sim->busy_until);
    free(sim->destination_after_trip);
    free(sim->offline);
    free(sim->busy_seconds);
    free(sim->outcomes);
    free_requests(sim->requests);
    free_fleet(sim->fleet);
    free_graph(sim->graph);
    memset(sim, 0, sizeof(*sim));
    errno = saved_errno;
}

static char *read_file(const char *path) {
    FILE *file;
    long length;
    size_t bytes_read;
    char *buffer;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    file = fopen(path, "rb");
    if (!file) {
        if (errno == 0) errno = EIO;
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        int err = errno ? errno : EIO;
        fclose(file);
        errno = err;
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        int err = errno ? errno : EIO;
        fclose(file);
        errno = err;
        return NULL;
    }

    buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        errno = ENOMEM;
        return NULL;
    }

    bytes_read = fread(buffer, 1, (size_t)length, file);
    if (bytes_read != (size_t)length || ferror(file)) {
        int err = errno ? errno : EIO;
        free(buffer);
        fclose(file);
        errno = err;
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

static ErrorCode load_config(const char *path, SimulationConfig *config) {
    char *json_text;
    cJSON *root;

    if (!path || !config) {
        errno = EINVAL;
        return ERR_CONFIG_INVALID;
    }

    json_text = read_file(path);
    if (!json_text) {
        return ERR_CONFIG_READ;
    }

    root = cJSON_Parse(json_text);
    free(json_text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        errno = EINVAL;
        return ERR_CONFIG_PARSE;
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

    if (config->time_step_s <= 0 || config->max_simulation_time_s <= 0 ||
        isnan(config->urgency_weight) || isinf(config->urgency_weight) ||
        isnan(config->aging_weight) || isinf(config->aging_weight)) {
        errno = EINVAL;
        return ERR_CONFIG_INVALID;
    }
    if (strcmp(config->output_format, "json") != 0 &&
        strcmp(config->output_format, "csv") != 0) {
        errno = EINVAL;
        return ERR_CONFIG_INVALID;
    }
    return ERR_OK;
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

    if (!request || request->passengers <= 0) return 0;

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

static int is_zero_weight_connected(Graph *g, int from_id, int to_id) {
    if (from_id == to_id) return 1;
    if (!g) return 0;
    Route *r = dijkstra(g, from_id, to_id);
    if (!r) return 0;
    int zero = (r->total_distance_m == 0 && r->total_time_s == 0);
    free_route(r);
    return zero;
}

static int is_on_route_corridor(Graph *g, const Route *route, const Route *route_k,
                                int pickup_id, int dropoff_id) {
    int pickup_idx = -1;
    if (!route || route->path_length <= 0) return 0;
    for (int i = 0; i < route->path_length; ++i) {
        if (is_zero_weight_connected(g, route->path_node_ids[i], pickup_id)) {
            if (pickup_idx < 0) pickup_idx = i;
            if (route->path_length == 1 ||
                (route->total_distance_m == 0 && route->total_time_s == 0)) {
                return 1;
            }
            if (route_k) {
                for (int m = i + 1; m < route->path_length; ++m) {
                    int next_node = route->path_node_ids[m];
                    if (is_zero_weight_connected(g, next_node, pickup_id)) {
                        continue;
                    }
                    for (int j = 1; j < route_k->path_length; ++j) {
                        if (is_zero_weight_connected(g, route_k->path_node_ids[j], next_node)) {
                            return 1;
                        }
                    }
                    break;
                }
            }
        }
        if (pickup_idx >= 0 &&
            is_zero_weight_connected(g, route->path_node_ids[i], dropoff_id)) {
            return 1;
        }
    }
    return 0;
}

static ErrorCode run_simulation(const char *data_dir) {
    SimulationConfig config;
    SimulationState sim;
    ErrorCode err;
    int served = 0;
    int current_time;
    int i;
    Metrics metrics;
    char output_path[512];
    char config_path[512];
    char nodes_path[512];
    char edges_path[512];
    char buggies_path[512];
    char requests_path[512];

    memset(&config, 0, sizeof(config));
    memset(&sim, 0, sizeof(sim));

    if (!data_dir || !data_dir[0]) {
        errno = EINVAL;
        return ERR_CONFIG_INVALID;
    }

    make_path(config_path, sizeof(config_path), data_dir, "simulation_config.json");
    make_path(nodes_path, sizeof(nodes_path), data_dir, "campus_nodes.json");
    make_path(edges_path, sizeof(edges_path), data_dir, "campus_edges.json");
    make_path(buggies_path, sizeof(buggies_path), data_dir, "campus_buggies.json");
    make_path(requests_path, sizeof(requests_path), data_dir, "requests.json");
    if (!config_path[0] || !nodes_path[0] || !edges_path[0] ||
        !buggies_path[0] || !requests_path[0]) {
        errno = ENAMETOOLONG;
        return ERR_PATH_TOO_LONG;
    }

    err = load_config(config_path, &config);
    if (err != ERR_OK) {
        return err;
    }

    sim.graph = load_graph(nodes_path, edges_path);
    if (!sim.graph) {
        free_simulation_state(&sim);
        return ERR_GRAPH_LOAD;
    }

    sim.fleet = load_fleet(buggies_path, &sim.fleet_count);
    if (!sim.fleet || sim.fleet_count <= 0) {
        if (errno == 0) errno = EINVAL;
        free_simulation_state(&sim);
        return ERR_FLEET_LOAD;
    }

    sim.requests = load_requests(requests_path, &sim.request_count);
    if (!sim.requests || sim.request_count <= 0) {
        if (errno == 0) errno = EINVAL;
        free_simulation_state(&sim);
        return ERR_REQUESTS_LOAD;
    }

    sim.busy_until = (int *)calloc((size_t)sim.fleet_count, sizeof(int));
    sim.destination_after_trip = (int *)calloc((size_t)sim.fleet_count, sizeof(int));
    sim.offline = (int *)calloc((size_t)sim.fleet_count, sizeof(int));
    sim.busy_seconds = (double *)calloc((size_t)sim.fleet_count, sizeof(double));
    sim.outcomes = (RequestOutcome *)calloc((size_t)sim.request_count, sizeof(RequestOutcome));
    if (!sim.busy_until || !sim.destination_after_trip || !sim.offline || !sim.busy_seconds ||
        (sim.request_count > 0 && !sim.outcomes)) {
        errno = ENOMEM;
        free_simulation_state(&sim);
        return ERR_OUT_OF_MEMORY;
    }

    for (i = 0; i < sim.fleet_count; ++i) {
        sim.busy_until[i] = 0;
        sim.destination_after_trip[i] = sim.fleet[i].current_location;
        /* Preserve the JSON initial status: NOT_AVAILABLE means offline all run. */
        sim.offline[i] = (sim.fleet[i].status != STATUS_AVAILABLE);
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
        for (i = 0; i < sim.fleet_count; ++i) {
            if (sim.offline[i]) {
                sim.fleet[i].status = STATUS_NOT_AVAILABLE;
                continue;
            }
            if (sim.busy_until[i] <= current_time) {
                sim.fleet[i].current_location = sim.destination_after_trip[i];
                sim.fleet[i].status = STATUS_AVAILABLE;
            } else {
                sim.fleet[i].status = STATUS_NOT_AVAILABLE;
            }
        }

        /* Assign as many requests as possible during this simulation tick. */
        for (;;) {
            AssignmentResult assignment = run_dispatch_tick(
                sim.fleet, sim.fleet_count, sim.requests, sim.request_count, sim.graph,
                current_time, config.urgency_weight, config.aging_weight,
                config.max_simulation_time_s, config.algorithm);
            int buggy_index;
            int request_index = -1;
            Route *trip_route;
            int trip_time;
            int total_duration;
            int final_destination;

            if (!assignment.success) break;

            buggy_index = find_buggy_index(sim.fleet, sim.fleet_count,
                                           assignment.assigned_buggy);
            if (buggy_index < 0) {
                errno = EINVAL;
                free_simulation_state(&sim);
                return ERR_UNKNOWN_BUGGY;
            }

            for (i = 0; i < sim.request_count; ++i) {
                if (strcmp(sim.requests[i].id, assignment.request_id) == 0) {
                    request_index = i;
                    break;
                }
            }
            if (request_index < 0) {
                errno = EINVAL;
                free_simulation_state(&sim);
                return ERR_UNKNOWN_REQUEST;
            }

            /* ETA is from the buggy's current location to the pickup. */
            trip_route = dijkstra(sim.graph, sim.requests[request_index].pickup,
                                  sim.requests[request_index].destination);
            if (!trip_route) {
                /* dispatch already marked this request assigned. Keep simulation consistent
                   by recording it as unserved rather than silently losing it. */
                sim.requests[request_index].assigned = 0;
                sim.fleet[buggy_index].status = STATUS_AVAILABLE;
                break;
            }
            trip_time = trip_route->total_time_s;
            if (trip_time < 0) trip_time = 0;

            total_duration = assignment.eta_s + trip_time;
            if (total_duration < 0) total_duration = 0;
            final_destination = sim.requests[request_index].destination;

            /* DEBUG: show exactly what each successful assignment costs. */
            printf("[ASSIGN] t=%d | Request=%s | Buggy=%s | Pickup=%d | Destination=%d | ",
                   current_time,
                   assignment.request_id,
                   assignment.assigned_buggy,
                   sim.requests[request_index].pickup,
                   sim.requests[request_index].destination);
            printf("Pickup ETA=%d s | Passenger trip=%d s | Busy total=%d s | Wait=%d s\n",
                   assignment.eta_s,
                   trip_time,
                   total_duration,
                   (current_time + assignment.eta_s - sim.requests[request_index].request_time) > 0
                       ? (current_time + assignment.eta_s - sim.requests[request_index].request_time)
                       : 0);

            /* Final wait includes the time already waited plus travel to pickup. */
            sim.outcomes[served].wait_time_s = current_time + assignment.eta_s -
                                               sim.requests[request_index].request_time;
            if (sim.outcomes[served].wait_time_s < 0) sim.outcomes[served].wait_time_s = 0;
            sim.outcomes[served].eta_s = assignment.eta_s;
            snprintf(sim.outcomes[served].request_id,
                     sizeof(sim.outcomes[served].request_id), "%s",
                     sim.requests[request_index].id);
            snprintf(sim.outcomes[served].assigned_buggy,
                     sizeof(sim.outcomes[served].assigned_buggy), "%s",
                     assignment.assigned_buggy);
            ++served;

            int remaining_seats = sim.fleet[buggy_index].capacity - sim.requests[request_index].passengers;
            Route *active_corridor = trip_route;
            for (int k = 0; remaining_seats > 0 && k < sim.request_count; ++k) {
                if (sim.requests[k].assigned || sim.requests[k].passengers <= 0 ||
                    sim.requests[k].request_time > current_time ||
                    sim.requests[k].passengers > remaining_seats)
                    continue;
                Route *pickup_k = dijkstra(sim.graph, sim.fleet[buggy_index].current_location, sim.requests[k].pickup);
                Route *from_primary_k = dijkstra(sim.graph, sim.requests[request_index].pickup, sim.requests[k].pickup);
                Route *route_k = dijkstra(sim.graph, sim.requests[k].pickup, sim.requests[k].destination);
                int eta_k = pickup_k ? pickup_k->total_time_s : -1;
                int via_primary_eta = (from_primary_k && from_primary_k->total_time_s >= 0 &&
                                       assignment.eta_s <= INT_MAX - from_primary_k->total_time_s)
                                          ? (assignment.eta_s + from_primary_k->total_time_s) : eta_k;
                int effective_eta_k = (via_primary_eta > eta_k) ? via_primary_eta : eta_k;
                int trip_k = route_k ? route_k->total_time_s : -1;
                int on_corridor = is_on_route_corridor(sim.graph, trip_route, route_k,
                                                       sim.requests[k].pickup,
                                                       sim.requests[k].destination) ||
                                  (active_corridor != trip_route &&
                                   is_on_route_corridor(sim.graph, active_corridor, route_k,
                                                        sim.requests[k].pickup,
                                                        sim.requests[k].destination));
                if (on_corridor && active_corridor != trip_route &&
                    !is_on_route_corridor(sim.graph, trip_route, NULL,
                                          sim.requests[k].pickup, sim.requests[k].destination) &&
                    !is_on_route_corridor(sim.graph, active_corridor, NULL,
                                          sim.requests[k].pickup, sim.requests[k].destination)) {
                    int passes_final = 0;
                    if (route_k) {
                        for (int j = 0; j < route_k->path_length; ++j) {
                            if (is_zero_weight_connected(sim.graph, route_k->path_node_ids[j], final_destination)) {
                                passes_final = 1;
                                break;
                            }
                        }
                    }
                    if (!passes_final) on_corridor = 0;
                }
                free_route(pickup_k);
                free_route(from_primary_k);
                if (!on_corridor || eta_k < 0 || trip_k < 0 ||
                    effective_eta_k > config.max_simulation_time_s - current_time ||
                    trip_k > config.max_simulation_time_s - current_time - effective_eta_k) {
                    free_route(route_k);
                    continue;
                }
                sim.requests[k].assigned = 1;
                remaining_seats -= sim.requests[k].passengers;
                if (effective_eta_k + trip_k > total_duration) {
                    total_duration = effective_eta_k + trip_k;
                    final_destination = sim.requests[k].destination;
                    if (active_corridor != trip_route) free_route(active_corridor);
                    active_corridor = route_k;
                    route_k = NULL;
                }
                free_route(route_k);
                sim.outcomes[served].wait_time_s = current_time + eta_k - sim.requests[k].request_time;
                if (sim.outcomes[served].wait_time_s < 0) sim.outcomes[served].wait_time_s = 0;
                sim.outcomes[served].eta_s = eta_k;
                snprintf(sim.outcomes[served].request_id, sizeof(sim.outcomes[served].request_id), "%s", sim.requests[k].id);
                snprintf(sim.outcomes[served].assigned_buggy, sizeof(sim.outcomes[served].assigned_buggy), "%s", assignment.assigned_buggy);
                ++served;
            }
            if (active_corridor != trip_route) free_route(active_corridor);
            free_route(trip_route);

            sim.busy_until[buggy_index] = current_time + total_duration;
            sim.destination_after_trip[buggy_index] = final_destination;
            if (total_duration == 0) {
                sim.fleet[buggy_index].current_location = final_destination;
                sim.fleet[buggy_index].status = STATUS_AVAILABLE;
            } else {
                sim.fleet[buggy_index].status = STATUS_NOT_AVAILABLE;
            }

            /* Utilization counts only busy time that falls inside the simulation window. */
            if (current_time < config.max_simulation_time_s) {
                int available_window = config.max_simulation_time_s - current_time;
                sim.busy_seconds[buggy_index] += (double)(total_duration < available_window
                                                             ? total_duration : available_window);
            }
        }
    }

    memset(&metrics, 0, sizeof(metrics));
    metrics.algorithm = config.algorithm;
    metrics.routing = config.routing;
    metrics.total_requests = sim.request_count;
    metrics.served = served;
    metrics.unserved = sim.request_count - served;

    if (served > 0) {
        double total_wait = 0.0;
        for (i = 0; i < served; ++i) total_wait += sim.outcomes[i].wait_time_s;
        metrics.avg_wait_time_s = total_wait / (double)served;
    }

    {
        double total_busy = 0.0;
        int online_count = 0;
        double total_available_time;
        for (i = 0; i < sim.fleet_count; ++i) {
            if (!sim.offline[i]) {
                total_busy += sim.busy_seconds[i];
                ++online_count;
            }
            printf("[UTILIZATION] Buggy=%s | Status=%s | Busy=%.0f s / %d s\n",
                   sim.fleet[i].id,
                   sim.offline[i] ? "OFFLINE" : "ONLINE",
                   sim.busy_seconds[i], config.max_simulation_time_s);
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
        errno = ENAMETOOLONG;
        free_simulation_state(&sim);
        return ERR_PATH_TOO_LONG;
    }
    if (strcmp(config.output_format, "csv") == 0) {
        if (!write_results_csv(output_path, &metrics, sim.outcomes, served)) {
            free_simulation_state(&sim);
            return ERR_RESULTS_WRITE;
        }
    } else {
        if (!write_results_json(output_path, &metrics, sim.outcomes, served)) {
            free_simulation_state(&sim);
            return ERR_RESULTS_WRITE;
        }
    }

    {
        int feasible_but_waiting = 0;
        int infeasible_at_deadline = 0;
        int not_yet_arrived = 0;
        for (i = 0; i < sim.request_count; ++i) {
            if (sim.requests[i].assigned) continue;

            if (sim.requests[i].request_time >= config.max_simulation_time_s) {
                ++not_yet_arrived;
            } else if (request_structurally_feasible(
                           sim.fleet, sim.offline, sim.fleet_count, &sim.requests[i], sim.graph)) {
                ++feasible_but_waiting;
            } else {
                ++infeasible_at_deadline;
            }
        }

        printf("Algorithm: %s\n", config.algorithm);
        printf("Simulation complete.\n");
        printf("Requests: %d | Assigned: %d | Unassigned: %d\n",
               metrics.total_requests, served, sim.request_count - served);
        printf("Unserved breakdown: Structurally feasible but unserved: %d | Structurally infeasible: %d | Not yet arrived: %d\n",
               feasible_but_waiting, infeasible_at_deadline, not_yet_arrived);
    }
    printf("Average wait: %.1f s | Average buggy utilization: %.1f%%\n",
           metrics.avg_wait_time_s, metrics.avg_buggy_utilization_pct);
    printf("Results written to %s\n", output_path);

    free_simulation_state(&sim);
    return ERR_OK;
}

int main(int argc, char *argv[]) {
    const char *data_dir = "Data";
    ErrorCode err;

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

    errno = 0;
    err = run_simulation(data_dir);
    if (err != ERR_OK) {
        perror(error_message(err));
        return (int)err;
    }

    return EXIT_SUCCESS;
}
