#ifndef GRAPH_H
#define GRAPH_H

typedef struct {       /* node declaration */
    int id;
    char name[64];
    double lat, lon;
} Node;

typedef struct {       /* edge declaration */
    int from, to;
    int distance_m;
    int avg_time_s;
    int bidirectional;   /* 1 = true, 0 = false */
} Edge;

typedef struct {
    Node* nodes;
    int node_count;
    Edge* edges;
    int edge_count;
} Graph;

typedef struct {
    int path_node_ids[64];  /* sequence of node ids from source to dest */
    int path_length;
    int total_distance_m;
    int total_time_s;
} Route;

/* Function declarations (not implementations - the actual logic goes in graph.c) */

/* Reads the JSON files and loads the campus map into memory. */
Graph* load_graph(const char* nodes_path, const char* edges_path);

/* Finds the shortest route between two location IDs. */
Route* dijkstra(Graph* g, int from_id, int to_id);

/* Releases memory allocated for the graph. */
void free_graph(Graph* g);

/* Releases memory allocated for a route. */
void free_route(Route* r);

#endif /* GRAPH_H */