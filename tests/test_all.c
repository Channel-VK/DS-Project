
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../GraphRouting/graph.h"
#include "../Dispatch/dispatch.h"

#define NODES_PATH "Data/campus_nodes.json"
#define EDGES_PATH "Data/campus_edges.json"

static void test_priority_aging(void) {
    Request aged = {0};
    Request newer = {0};

    aged.urgency = 1;
    aged.request_time = 0;

    newer.urgency = 5;
    newer.request_time = 20;

    double aged_priority =
        compute_priority(&aged, 1.0, 0.5, 0);

    double newer_priority =
        compute_priority(&newer, 1.0, 0.5, 20);

    double aged_later =
        compute_priority(&aged, 1.0, 0.5, 40);

    assert(aged_priority == 1.0);
    assert(newer_priority == 5.0);
    assert(aged_later == 21.0);
    assert(aged_later > newer_priority);

    printf("[PASS] Priority aging\n");
}

static void test_graph_routing(Graph *graph) {
    assert(graph != NULL);

    Route *route = dijkstra(graph, 1, 2);
    assert(route != NULL);
    assert(route->path_length > 0);
    assert(route->total_distance_m >= 0);
    assert(route->total_time_s >= 0);

    free_route(route);
    printf("[PASS] Graph routing\n");
}

static void test_successful_dispatch(Graph *graph) {
    Buggy buggy = {0};
    Request request = {0};

    strcpy(buggy.id, "TEST1");
    buggy.capacity = 6;
    buggy.current_location = 1;
    buggy.status = STATUS_AVAILABLE;
    buggy.speed_mps = 5.5;

    strcpy(request.id, "TEST_R1");
    request.pickup = 1;
    request.destination = 2;
    request.passengers = 1;
    request.urgency = 3;
    request.request_time = 0;
    request.assigned = 0;

    AssignmentResult result = run_dispatch_tick(
        &buggy, 1, &request, 1, graph,
        0, 1.0, 0.5, 1800, "greedy"
    );

    assert(result.success == 1);
    assert(strcmp(result.assigned_buggy, "TEST1") == 0);
    assert(strcmp(result.request_id, "TEST_R1") == 0);

    printf("[PASS] Successful dispatch\n");
}

static void test_capacity_constraint(Graph *graph) {
    Buggy buggy = {0};
    Request request = {0};

    strcpy(buggy.id, "SMALL1");
    buggy.capacity = 1;
    buggy.current_location = 1;
    buggy.status = STATUS_AVAILABLE;
    buggy.speed_mps = 5.5;

    strcpy(request.id, "TEST_R2");
    request.pickup = 1;
    request.destination = 2;
    request.passengers = 2;
    request.urgency = 5;
    request.request_time = 0;
    request.assigned = 0;

    AssignmentResult result = run_dispatch_tick(
        &buggy, 1, &request, 1, graph,
        0, 1.0, 0.5, 1800, "greedy"
    );

    assert(result.success == 0);
    assert(request.assigned == 0);

    printf("[PASS] Capacity constraint\n");
}

static void test_regret_dispatch(Graph *graph) {
    Buggy g_fleet[2] = {
        { "BG1", 6, 2, STATUS_AVAILABLE, 5.5 },
        { "BG2", 6, 16, STATUS_AVAILABLE, 5.5 }
    };
    Request g_reqs[2] = {
        { "R1", 9, 18, 1, 5, 0, 0 },
        { "R2", 2, 1, 1, 1, 0, 0 }
    };
    Buggy r_fleet[2];
    Request r_reqs[2];
    memcpy(r_fleet, g_fleet, sizeof(g_fleet));
    memcpy(r_reqs, g_reqs, sizeof(g_reqs));

    AssignmentResult g1 = run_dispatch_tick(g_fleet, 2, g_reqs, 2, graph, 0, 1.0, 0.5, 1800, "greedy");
    AssignmentResult g2 = run_dispatch_tick(g_fleet, 2, g_reqs, 2, graph, 0, 1.0, 0.5, 1800, "greedy");
    AssignmentResult r1 = run_dispatch_tick(r_fleet, 2, r_reqs, 2, graph, 0, 1.0, 0.5, 1800, "regret");
    AssignmentResult r2 = run_dispatch_tick(r_fleet, 2, r_reqs, 2, graph, 0, 1.0, 0.5, 1800, "regret");

    assert(g1.success == 1 && g2.success == 1);
    assert(r1.success == 1 && r2.success == 1);
    assert(strcmp(g1.request_id, "R1") == 0 && strcmp(g1.assigned_buggy, "BG1") == 0);
    assert(strcmp(r1.request_id, "R2") == 0 && strcmp(r1.assigned_buggy, "BG1") == 0);
    assert((r1.eta_s + r2.eta_s) < (g1.eta_s + g2.eta_s));

    printf("[PASS] Regret dispatch\n");
}

int main(void) {
    Graph *graph = load_graph(NODES_PATH, EDGES_PATH);

    assert(graph != NULL);

    test_priority_aging();
    test_graph_routing(graph);
    test_successful_dispatch(graph);
    test_capacity_constraint(graph);
    test_regret_dispatch(graph);

    free_graph(graph);

    printf("\nAll integration tests passed.\n");
    return 0;
}
