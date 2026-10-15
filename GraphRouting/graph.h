#ifndef GRAPH_H
#define GRAPH_H

typedef struct {
    int id;
    char name[64];
    double lat, lon;
} Node;

typedef struct {
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

/* Function declarations */
Graph* load_graph(const char* nodes_path, const char* edges_path);
Route* dijkstra(Graph* g, int from_id, int to_id);
void free_graph(Graph* g);
void free_route(Route* r);

#endif /* GRAPH_H */
