
#include <stdio.h>
#include "graph.h"

int main(void)
{
    Graph *g = load_graph(
        "Data/campus_nodes.json",
        "Data/campus_edges.json"
    );

    if (g == NULL) {
        printf("Failed to load campus graph.\n");
        return 1;
    }

    printf("Graph loaded successfully!\n");
    printf("Number of nodes: %d\n", g->node_count);
    printf("Number of edges: %d\n", g->edge_count);
    
    Route *route = dijkstra(g, 1, 1);

if (route == NULL) {
    printf("No route found.\n");
} else {
    printf("Shortest distance: %d m\n", route->total_distance_m);
    printf("Total time: %d s\n", route->total_time_s);
    printf("Path: ");

    for (int i = 0; i < route->path_length; i++) {
    printf("%d", route->path_node_ids[i]);

    if (i < route->path_length - 1) {
        printf(" -> ");
    }
}

printf("\n");
free_route(route);
}
    free_graph(g);

    return 0;
}
