
#include <assert.h>
#include <errno.h>
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

static void test_hash_map_and_errno(Graph *graph) {
    assert(graph->hash_table != NULL);
    assert(graph->hash_size == graph->node_count * 2 + 1);

    /* Test colliding node IDs (congruent mod 11) and negative node ID (-1),
       plus parallel edges and unreachable node (ENOENT). */
    {
        Node nodes[5] = {
            { 1, "N1", 0.0, 0.0 },
            { 12, "N12", 0.0, 0.0 },
            { 23, "N23", 0.0, 0.0 },
            { -1, "N_NEG", 0.0, 0.0 },
            { 34, "N_ISO", 0.0, 0.0 }
        };
        int hash_table[11];
        AdjNode e_slow = { 1, 500, 100, NULL };
        AdjNode e_fast = { 1, 100, 20, &e_slow };
        AdjNode e_to_23 = { 2, 50, 10, NULL };
        AdjNode e_to_neg = { 3, 30, 5, NULL };
        AdjNode *adj[5] = { &e_fast, &e_to_23, &e_to_neg, NULL, NULL };
        Graph custom = { nodes, 5, NULL, 0, adj, hash_table, 11 };

        for (int i = 0; i < 11; i++) hash_table[i] = -1;
        for (int i = 0; i < 5; i++) {
            unsigned int h = (unsigned int)nodes[i].id % 11u;
            while (hash_table[h] != -1) h = (h + 1) % 11u;
            hash_table[h] = i;
        }

        Route *r = dijkstra(&custom, 1, -1);
        assert(r != NULL);
        assert(r->path_length == 4);
        assert(r->path_node_ids[0] == 1 && r->path_node_ids[1] == 12 &&
               r->path_node_ids[2] == 23 && r->path_node_ids[3] == -1);
        assert(r->total_distance_m == 180);
        assert(r->total_time_s == 35);
        free_route(r);

        /* Unreachable node should set errno = ENOENT */
        errno = 0;
        assert(dijkstra(&custom, 1, 34) == NULL);
        assert(errno == ENOENT);

        /* Fallback when hash_table is NULL */
        custom.hash_table = NULL;
        custom.hash_size = 0;
        r = dijkstra(&custom, 1, -1);
        assert(r != NULL);
        assert(r->total_distance_m == 180 && r->total_time_s == 35);
        free_route(r);
    }

    /* Test path exceeding 64 nodes sets errno = ERANGE instead of overflowing */
    {
        Node chain_nodes[65];
        AdjNode chain_edges[64];
        AdjNode *chain_adj[65];
        for (int i = 0; i < 65; i++) {
            chain_nodes[i].id = i + 1;
            chain_adj[i] = NULL;
        }
        for (int i = 0; i < 64; i++) {
            chain_edges[i] = (AdjNode){ i + 1, 10, 2, NULL };
            chain_adj[i] = &chain_edges[i];
        }
        Graph chain = { chain_nodes, 65, NULL, 64, chain_adj, NULL, 0 };
        errno = 0;
        assert(dijkstra(&chain, 1, 65) == NULL);
        assert(errno == ERANGE);
    }

    errno = 0;
    assert(dijkstra(graph, 9999, 1) == NULL);
    assert(errno == EINVAL);

    errno = 0;
    assert(dijkstra(NULL, 1, 2) == NULL);
    assert(errno == EINVAL);

    errno = 0;
    assert(load_graph(NULL, EDGES_PATH) == NULL);
    assert(errno == EINVAL);

    errno = 0;
    assert(load_graph("Data/missing_nodes.json", EDGES_PATH) == NULL);
    assert(errno == ENOENT);

    errno = 0;
    assert(load_fleet(NULL, NULL) == NULL);
    assert(errno == EINVAL);

    errno = 0;
    assert(load_requests(NULL, NULL) == NULL);
    assert(errno == EINVAL);

    errno = 0;
    assert(compute_priority(NULL, 1.0, 0.5, 0) == 0.0);
    assert(errno == EINVAL);

    errno = 0;
    AssignmentResult bad = run_dispatch_tick(NULL, 0, NULL, 0, NULL, 0, 1.0, 0.5, 1800, "greedy");
    assert(bad.success == 0);
    assert(errno == EINVAL);

    printf("[PASS] Hash map and errno context\n");
}

static void test_zero_weight_and_edge_cases(Graph *graph) {
    /* 1. Real campus graph 0-weight edge (Node 8 OJAS <-> Node 21 Physics Dept.) */
    Route *r_zero = dijkstra(graph, 8, 21);
    assert(r_zero != NULL);
    assert(r_zero->total_distance_m == 0);
    assert(r_zero->total_time_s == 0);
    assert(r_zero->path_length == 2);
    assert(r_zero->path_node_ids[0] == 8 && r_zero->path_node_ids[1] == 21);
    free_route(r_zero);

    /* Same-node route (from_id == to_id) has 0 distance, 0 time, length 1 */
    Route *r_same = dijkstra(graph, 8, 8);
    assert(r_same != NULL);
    assert(r_same->total_distance_m == 0);
    assert(r_same->total_time_s == 0);
    assert(r_same->path_length == 1);
    assert(r_same->path_node_ids[0] == 8);
    free_route(r_same);

    /* 2. Custom graph with 0-weight cycle, parallel 0-weight edges, and equal-distance tie-breaking */
    {
        Node nodes[4] = {
            { 10, "A", 0.0, 0.0 },
            { 20, "B", 0.0, 0.0 },
            { 30, "C", 0.0, 0.0 },
            { 40, "D", 0.0, 0.0 }
        };
        /* A <-> B is a 0-weight cycle (dist=0, time=0).
           A -> C has dist=0, time=50 (slow 0-dist edge).
           B -> C has parallel edges: (dist=0, time=30) and (dist=0, time=0).
           C -> D has (dist=0, time=5), while A -> D direct has (dist=0, time=20). */
        AdjNode a_to_d_slow = { 3, 0, 20, NULL };
        AdjNode a_to_c_slow = { 2, 0, 50, &a_to_d_slow };
        AdjNode a_to_b_zero = { 1, 0, 0, &a_to_c_slow };

        AdjNode b_to_a_zero = { 0, 0, 0, NULL };
        AdjNode b_to_c_fast = { 2, 0, 0, &b_to_a_zero };
        AdjNode b_to_c_mid  = { 2, 0, 30, &b_to_c_fast };

        AdjNode c_to_d_fast = { 3, 0, 5, NULL };

        AdjNode *adj[4] = { &a_to_b_zero, &b_to_c_mid, &c_to_d_fast, NULL };
        Graph g_zero = { nodes, 4, NULL, 0, adj, NULL, 0 };

        Route *r = dijkstra(&g_zero, 10, 40);
        assert(r != NULL);
        assert(r->total_distance_m == 0);
        assert(r->total_time_s == 5);
        assert(r->path_length == 4);
        assert(r->path_node_ids[0] == 10 && r->path_node_ids[1] == 20 &&
               r->path_node_ids[2] == 30 && r->path_node_ids[3] == 40);
        free_route(r);
    }

    /* 3. Fallback when adj_list is NULL (using edges array directly) */
    {
        Node nodes[3] = {
            { 1, "N1", 0.0, 0.0 },
            { 2, "N2", 0.0, 0.0 },
            { 3, "N3", 0.0, 0.0 }
        };
        Edge edges[2] = {
            { 1, 2, 0, 0, 1 },
            { 2, 3, 50, 10, 0 }
        };
        Graph g_edges_only = { nodes, 3, edges, 2, NULL, NULL, 0 };
        Route *r = dijkstra(&g_edges_only, 1, 3);
        assert(r != NULL);
        assert(r->total_distance_m == 50);
        assert(r->total_time_s == 10);
        assert(r->path_length == 3);
        free_route(r);
    }

    /* 4. Dispatch with 0-weight edges: prefers buggy already at pickup (1 hop) over buggy across 0-time edge (2 hops) */
    {
        Buggy fleet[2] = {
            { "BG_ADJ", 6, 8, STATUS_AVAILABLE, 5.5 },
            { "BG_EXACT", 6, 21, STATUS_AVAILABLE, 5.5 }
        };
        Request req = { "R_ZERO", 21, 8, 1, 2, 0, 0 };
        AssignmentResult res = run_dispatch_tick(fleet, 2, &req, 1, graph, 0, 1.0, 0.5, 1800, "greedy");
        assert(res.success == 1);
        assert(strcmp(res.assigned_buggy, "BG_EXACT") == 0);
        assert(res.eta_s == 0);
        assert(res.wait_time_s == 0);
    }

    /* 5. Out-of-bounds neighbor index in adj_list and corrupted hash_table entry are safely ignored */
    {
        Node nodes[2] = {
            { 1, "N1", 0.0, 0.0 },
            { 2, "N2", 0.0, 0.0 }
        };
        AdjNode valid_edge = { 1, 10, 5, NULL };
        AdjNode oob_edge = { 99, 0, 0, &valid_edge };
        AdjNode *adj[2] = { &oob_edge, NULL };
        int bad_hash[5] = { 99, 99, 99, 99, 99 };
        Graph g_oob = { nodes, 2, NULL, 0, adj, bad_hash, 5 };

        errno = 0;
        assert(dijkstra(&g_oob, 1, 2) == NULL);
        assert(errno == EINVAL);

        g_oob.hash_table = NULL;
        g_oob.hash_size = 0;
        Route *r = dijkstra(&g_oob, 1, 2);
        assert(r != NULL);
        assert(r->total_distance_m == 10 && r->total_time_s == 5);
        free_route(r);
    }

    printf("[PASS] Zero-weight nodes/edges and edge cases\n");
}

int main(void) {
    Graph *graph = load_graph(NODES_PATH, EDGES_PATH);

    assert(graph != NULL);

    test_priority_aging();
    test_graph_routing(graph);
    test_successful_dispatch(graph);
    test_capacity_constraint(graph);
    test_regret_dispatch(graph);
    test_hash_map_and_errno(graph);
    test_zero_weight_and_edge_cases(graph);

    free_graph(graph);

    printf("\nAll integration tests passed.\n");
    return 0;
}
