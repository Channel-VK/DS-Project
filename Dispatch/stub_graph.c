#include <stdlib.h>
#include "../GraphRouting/graph.h"

/* 
 * Mock dijkstra implementation for testing Dispatch module independently.
 * Calculates mock distance/time based on simple node differences or direct edge lookups.
 */
Route* dijkstra(Graph* g, int from_id, int to_id) {
    Route* r = (Route*)malloc(sizeof(Route));
    if (!r) return NULL;

    r->path_length = 2;
    r->path_node_ids[0] = from_id;
    r->path_node_ids[1] = to_id;

    if (from_id == to_id) {
        r->path_length = 1;
        r->total_distance_m = 0;
        r->total_time_s = 0;
        return r;
    }

    /* Check if an edge exists in graph if g is provided */
    if (g && g->edges && g->edge_count > 0) {
        int i;
        for (i = 0; i < g->edge_count; i++) {
            Edge* e = &g->edges[i];
            if ((e->from == from_id && e->to == to_id) ||
                (e->bidirectional && e->to == from_id && e->from == to_id)) {
                r->total_distance_m = e->distance_m;
                r->total_time_s = e->avg_time_s;
                return r;
            }
        }
    }

    /* Fallback reasonable heuristic if not directly connected or g is NULL */
    int diff = abs(to_id - from_id);
    r->total_distance_m = diff * 150;
    r->total_time_s = diff * 30; /* ~30 seconds per node diff */

    return r;
}

void free_route(Route* r) {
    if (r) {
        free(r);
    }
}
