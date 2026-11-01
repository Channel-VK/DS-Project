#include "graph.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
/*
Header	          Purpose
graph.h	   Gives us Graph, Node, Edge, Route and function declarations
cJSON.h	   Provides JSON parsing functions
stdio.h	   File operations such as fopen(), fread() and fclose()
stdlib.h   Memory functions such as malloc(), calloc() and free()
string.h   String utilities, if needed
stdint.h   We use it for uintmax_t & SIZE_MAX, which help us check that the file size can safely accommodate the extra '\0' character.
*/

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);          // we go until end to know its size
        return NULL;
    }

    long size = ftell(fp);  // ftell will now say its size

    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    size_t file_size = (size_t)size;

    if ((uintmax_t)size >= (uintmax_t)SIZE_MAX) {
        fclose(fp);               // checks if size is safe or not , can accomodate in given size or not
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {      // after reading , goes back to beginning to read the content 
        fclose(fp);
        return NULL;
    }

    char *buffer = malloc(file_size + 1);  // we ask to allocate memory

    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fp); // txt cntnt is now stored in buffer

    if (bytes_read != file_size || ferror(fp)) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    buffer[bytes_read] = '\0';       //termiantes the string txt 

    fclose(fp);                   
    return buffer;
}

static int node_id_exists(const Graph *g, int id)
{
    for (int i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id == id) {
            return 1;
        }
    }

    return 0;
}

static int is_valid_int(const cJSON *item)
{
    return cJSON_IsNumber(item) &&
           item->valuedouble >= INT_MIN &&
           item->valuedouble <= INT_MAX &&
           item->valuedouble == (double)item->valueint;
}

// LOAD GRAPHHHHHHHHHH

Graph *load_graph(const char *nodes_path, const char *edges_path)
{
    char *nodes_text = read_file(nodes_path);

    if (nodes_text == NULL) {
        return NULL;
    }

    char *edges_text = read_file(edges_path);

    if (edges_text == NULL) {
        free(nodes_text);
        return NULL;
    }

cJSON *nodes_root = cJSON_Parse(nodes_text);  // WE'LL GET PARSING STRUCTURE
cJSON *edges_root = cJSON_Parse(edges_text);

free(nodes_text);   // AFTER PARSING , FREE UP TEXTS
free(edges_text);

if (nodes_root == NULL || edges_root == NULL) {
    cJSON_Delete(nodes_root);      // IF EITHER IS NULL , WE FREEUP AND RETURN NULL
    cJSON_Delete(edges_root);
    return NULL;
}             


if (!cJSON_IsObject(nodes_root) || !cJSON_IsObject(edges_root)) {
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

cJSON *nodes_array = cJSON_GetObjectItemCaseSensitive(nodes_root, "nodes");

cJSON *edges_array = cJSON_GetObjectItemCaseSensitive(edges_root, "edges");

if (!cJSON_IsArray(nodes_array) || !cJSON_IsArray(edges_array)) {
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

int node_count = cJSON_GetArraySize(nodes_array);
int edge_count = cJSON_GetArraySize(edges_array);

if (node_count <= 0 || edge_count < 0) {
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

//memory to store the info we got in our Graph structure.

Graph *g = malloc(sizeof(Graph));

if (g == NULL) {
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

g->nodes = malloc((size_t)node_count * sizeof(Node));

g->edges = edge_count > 0 ? malloc((size_t)edge_count * sizeof(Edge)): NULL;

if (g->nodes == NULL || (edge_count > 0 && g->edges == NULL)) {
    free(g->nodes);
    free(g->edges);
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

g->node_count = node_count;
g->edge_count = edge_count;  // till now,I just reserved memory for nodes , edges in graph
g->adj_list = NULL;

//loading nodes
for (int i = 0; i < node_count; i++) {
    cJSON *node_json = cJSON_GetArrayItem(nodes_array, i);
// cJSON_GetArrayItem(nodes_root, i) retrieves the JSON object at index i
if (!cJSON_IsObject(node_json)) {
    free(g->nodes);
    free(g->edges);
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

cJSON *id_item   = cJSON_GetObjectItemCaseSensitive(node_json, "id");
cJSON *name_item = cJSON_GetObjectItemCaseSensitive(node_json, "name");
cJSON *lat_item = cJSON_GetObjectItemCaseSensitive(node_json, "lat");
cJSON *lon_item = cJSON_GetObjectItemCaseSensitive(node_json, "lon");

if (!is_valid_int(id_item) ||
    !cJSON_IsString(name_item) ||
    !cJSON_IsNumber(lat_item) ||
    !cJSON_IsNumber(lon_item)) {
    free(g->nodes);
    free(g->edges);                // CHECKING FIELDS ARE OF CORRECT DTATYPE
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}
// Reject duplicate node IDs so each ID identifies exactly one location.
for (int j = 0; j < i; j++) {
    if (g->nodes[j].id == id_item->valueint) {
        free(g->nodes);
        free(g->edges);
        free(g);
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        return NULL;
    }
}

g->nodes[i].id = id_item->valueint;

snprintf(g->nodes[i].name,sizeof(g->nodes[i].name), "%s",name_item->valuestring);

g->nodes[i].lat = lat_item->valuedouble;
g->nodes[i].lon = lon_item->valuedouble;
}

// loading edges 
for (int i = 0; i < edge_count; i++) {
    cJSON *edge_json = cJSON_GetArrayItem(edges_array, i);

    //this checks only that the item is a JSON object
    if (!cJSON_IsObject(edge_json)) {
    free(g->nodes);
    free(g->edges);
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;   }

 //Each call retrieves a field from the current JSON edge object.
cJSON *from_item = cJSON_GetObjectItemCaseSensitive(edge_json, "from");

cJSON *to_item = cJSON_GetObjectItemCaseSensitive(edge_json, "to");

cJSON *distance_item = cJSON_GetObjectItemCaseSensitive(edge_json, "distance_m");

cJSON *time_item = cJSON_GetObjectItemCaseSensitive(edge_json, "avg_time_s");

cJSON *bidirectional_item = cJSON_GetObjectItemCaseSensitive(edge_json, "bidirectional");


if (!is_valid_int(from_item) ||
    !is_valid_int(to_item) ||
    !is_valid_int(distance_item) ||
    !is_valid_int(time_item)  ||
    distance_item->valueint < 0 ||
    time_item->valueint < 0) {
    free(g->nodes);
    free(g->edges);                 // checks every field type 
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}
if (!cJSON_IsBool(bidirectional_item)) {
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    free_graph(g);
    return NULL;
}


// Checkng whether both edge endpoints refer to existing nodes in the graph.
if (!node_id_exists(g, from_item->valueint) ||!node_id_exists(g, to_item->valueint)) {
    free(g->nodes);            
    free(g->edges);
    free(g);
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);
    return NULL;
}

g->edges[i].from = from_item->valueint;
g->edges[i].to = to_item->valueint;
g->edges[i].distance_m = distance_item->valueint;
g->edges[i].avg_time_s = time_item->valueint;
g->edges[i].bidirectional = cJSON_IsTrue(bidirectional_item) ? 1 : 0 ;


}
cJSON_Delete(nodes_root);
cJSON_Delete(edges_root);

g->adj_list = calloc((size_t)g->node_count, sizeof(AdjNode *));
if (g->adj_list == NULL) {
    free_graph(g);
    return NULL;
}
for (int i = g->edge_count - 1; i >= 0; i--) {
    int u = -1, v = -1;
    for (int j = 0; j < g->node_count; j++) {
        if (g->nodes[j].id == g->edges[i].from) u = j;
        if (g->nodes[j].id == g->edges[i].to) v = j;
    }
    AdjNode *node = malloc(sizeof(AdjNode));
    if (node == NULL) { free_graph(g); return NULL; }
    *node = (AdjNode){ v, g->edges[i].distance_m, g->edges[i].avg_time_s, g->adj_list[u] };
    g->adj_list[u] = node;
    if (g->edges[i].bidirectional) {
        AdjNode *rev = malloc(sizeof(AdjNode));
        if (rev == NULL) { free_graph(g); return NULL; }
        *rev = (AdjNode){ u, g->edges[i].distance_m, g->edges[i].avg_time_s, g->adj_list[v] };
        g->adj_list[v] = rev;
    }
}

return g;
}

void free_graph(Graph *g)
{
    if (g == NULL) {
        return;
    }
    if (g->adj_list != NULL) {
        for (int i = 0; i < g->node_count; i++) {
            AdjNode *curr = g->adj_list[i];
            while (curr != NULL) {
                AdjNode *next = curr->next;
                free(curr);
                curr = next;
            }
        }
        free(g->adj_list);
    }
    free(g->nodes);
    free(g->edges);
    free(g);
}


void free_route(Route *r)
{
    free(r);
}


// DIJKSTRA'S ALGORITHM
//helper functions to find index used for that resp ID num
static int find_index(const Graph *g, int node_id)
{
    for (int i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id == node_id) {
            return i;
        }
    }

    return -1;
}

typedef struct {
    int vertex;
    int dist;
} HeapNode;

typedef struct {
    HeapNode *data;
    int size;
} MinHeap;

static void push_heap(MinHeap *h, int vertex, int dist)
{
    int i = h->size++;
    h->data[i] = (HeapNode){ vertex, dist };
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->data[p].dist < h->data[i].dist ||
            (h->data[p].dist == h->data[i].dist && h->data[p].vertex <= h->data[i].vertex))
            break;
        HeapNode tmp = h->data[p];
        h->data[p] = h->data[i];
        h->data[i] = tmp;
        i = p;
    }
}

static HeapNode pop_heap(MinHeap *h)
{
    HeapNode top = h->data[0];
    h->data[0] = h->data[--h->size];
    int i = 0;
    for (;;) {
        int left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left < h->size && (h->data[left].dist < h->data[smallest].dist ||
            (h->data[left].dist == h->data[smallest].dist && h->data[left].vertex < h->data[smallest].vertex)))
            smallest = left;
        if (right < h->size && (h->data[right].dist < h->data[smallest].dist ||
            (h->data[right].dist == h->data[smallest].dist && h->data[right].vertex < h->data[smallest].vertex)))
            smallest = right;
        if (smallest == i) break;
        HeapNode tmp = h->data[i];
        h->data[i] = h->data[smallest];
        h->data[smallest] = tmp;
        i = smallest;
    }
    return top;
}

Route *dijkstra(Graph *g, int from_id, int to_id)
{
    if (g == NULL || g->adj_list == NULL) {
        return NULL;
    }

    int source_index = find_index(g, from_id);
    int destination_index = find_index(g, to_id);

    if (source_index == -1 || destination_index == -1) {
        return NULL;
    }

    int *dist    = malloc(g->node_count * sizeof(int));
    int *visited = malloc(g->node_count * sizeof(int));
    int *previous = malloc(g->node_count * sizeof(int));
    MinHeap heap = { malloc((g->edge_count * 2 + g->node_count + 1) * sizeof(HeapNode)), 0 };

    if (dist == NULL || visited == NULL || previous == NULL || heap.data == NULL) {
    free(dist);
    free(visited);
    free(previous);
    free(heap.data);
    return NULL;
}
    
    for (int i = 0; i < g->node_count; i++){
    dist[i] = INT_MAX;
    visited[i] = 0;
    previous[i] = -1;
    }
    dist[source_index] = 0;
    push_heap(&heap, source_index, 0);

    while (heap.size > 0) {
        int current = pop_heap(&heap).vertex;
        if (visited[current]) continue;
        visited[current] = 1;
        if (current == destination_index) break;

        for (AdjNode *adj = g->adj_list[current]; adj != NULL; adj = adj->next) {
            int neighbor_index = adj->to_index;
            int new_distance = dist[current] + adj->distance_m;
            if (!visited[neighbor_index] && new_distance < dist[neighbor_index]) {
                dist[neighbor_index] = new_distance;
                previous[neighbor_index] = current;
                push_heap(&heap, neighbor_index, new_distance);
            }
        }
    }
    free(heap.data); 
     if (dist[destination_index] == INT_MAX){
    free(dist);
    free(visited);
    free(previous);
    return NULL;
}
    
    Route *route = malloc(sizeof(Route));
    
     if (route == NULL){
    free(dist);
    free(visited);
    free(previous);
    return NULL; }
    int current = destination_index;

    route->path_length = 0;
    while (current != -1) {
    route->path_node_ids[route->path_length] = g->nodes[current].id;
    route->path_length++;
    current = previous[current];
    }
    for (int i = 0; i < route->path_length / 2; i++) {
    int temp = route->path_node_ids[i];

    route->path_node_ids[i] = route->path_node_ids[route->path_length - 1 - i];

    route->path_node_ids[route->path_length - 1 - i] = temp;
}
    route->total_distance_m = dist[destination_index];
    route->total_time_s = 0;

    for (int i = 0; i < route->path_length - 1; i++) {

    int from = route->path_node_ids[i];
    int to = route->path_node_ids[i + 1];

    for (int j = 0; j < g->edge_count; j++) {
        Edge *edge = &g->edges[j];

        if ((edge->from == from && edge->to == to) ||
            (edge->bidirectional && edge->from == to && edge->to == from)) {

            route->total_time_s += edge->avg_time_s;
            break;
        }
    }
}
free(dist);
free(visited);
free(previous);
return route; }