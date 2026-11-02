#include "graph.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
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
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        if (errno == 0) errno = EIO;
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        int err = errno ? errno : EIO;
        fclose(fp);          // we go until end to know its size
        errno = err;
        return NULL;
    }

    long size = ftell(fp);  // ftell will now say its size

    if (size < 0) {
        int err = errno ? errno : EIO;
        fclose(fp);
        errno = err;
        return NULL;
    }

    size_t file_size = (size_t)size;

    if ((uintmax_t)size >= (uintmax_t)SIZE_MAX) {
        fclose(fp);               // checks if size is safe or not , can accomodate in given size or not
        errno = ENOMEM;
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {      // after reading , goes back to beginning to read the content 
        int err = errno ? errno : EIO;
        fclose(fp);
        errno = err;
        return NULL;
    }

    char *buffer = malloc(file_size + 1);  // we ask to allocate memory

    if (buffer == NULL) {
        fclose(fp);
        errno = ENOMEM;
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, fp); // txt cntnt is now stored in buffer

    if (bytes_read != file_size || ferror(fp)) {
        int err = errno ? errno : EIO;
        free(buffer);
        fclose(fp);
        errno = err;
        return NULL;
    }

    buffer[bytes_read] = '\0';       //termiantes the string txt 

    fclose(fp);                   
    return buffer;
}

static int find_index(const Graph *g, int node_id)
{
    if (g == NULL || g->nodes == NULL || g->node_count <= 0) {
        return -1;
    }

    if (g->hash_table != NULL && g->hash_size > 0) {
        unsigned int h = (unsigned int)node_id % (unsigned int)g->hash_size;
        for (int step = 0; step < g->hash_size; step++) {
            int idx = g->hash_table[h];
            if (idx == -1) {
                return -1;
            }
            if (idx < 0 || idx >= g->node_count) {
                return -1;
            }
            if (g->nodes[idx].id == node_id) {
                return idx;
            }
            h = (h + 1) % (unsigned int)g->hash_size;
        }
        return -1;
    }

    for (int i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id == node_id) {
            return i;
        }
    }

    return -1;
}

static int node_id_exists(const Graph *g, int id)
{
    return find_index(g, id) != -1;
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
    if (nodes_path == NULL || edges_path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    char *nodes_text = read_file(nodes_path);

    if (nodes_text == NULL) {
        return NULL;
    }

    char *edges_text = read_file(edges_path);

    if (edges_text == NULL) {
        int err = errno;
        free(nodes_text);
        errno = err;
        return NULL;
    }

    cJSON *nodes_root = cJSON_Parse(nodes_text);  // WE'LL GET PARSING STRUCTURE
    cJSON *edges_root = cJSON_Parse(edges_text);

    free(nodes_text);   // AFTER PARSING , FREE UP TEXTS
    free(edges_text);

    if (nodes_root == NULL || edges_root == NULL) {
        cJSON_Delete(nodes_root);      // IF EITHER IS NULL , WE FREEUP AND RETURN NULL
        cJSON_Delete(edges_root);
        errno = EINVAL;
        return NULL;
    }             

    if (!cJSON_IsObject(nodes_root) || !cJSON_IsObject(edges_root)) {
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        errno = EINVAL;
        return NULL;
    }

    cJSON *nodes_array = cJSON_GetObjectItemCaseSensitive(nodes_root, "nodes");
    cJSON *edges_array = cJSON_GetObjectItemCaseSensitive(edges_root, "edges");

    if (!cJSON_IsArray(nodes_array) || !cJSON_IsArray(edges_array)) {
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        errno = EINVAL;
        return NULL;
    }

    int node_count = cJSON_GetArraySize(nodes_array);
    int edge_count = cJSON_GetArraySize(edges_array);

    if (node_count <= 0 || edge_count < 0) {
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        errno = EINVAL;
        return NULL;
    }

    //memory to store the info we got in our Graph structure.

    Graph *g = calloc(1, sizeof(Graph));

    if (g == NULL) {
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        errno = ENOMEM;
        return NULL;
    }

    g->node_count = node_count;
    g->edge_count = edge_count;
    g->hash_size = node_count * 2 + 1;
    g->nodes = malloc((size_t)node_count * sizeof(Node));
    g->edges = edge_count > 0 ? malloc((size_t)edge_count * sizeof(Edge)) : NULL;
    g->hash_table = malloc((size_t)g->hash_size * sizeof(int));

    if (g->nodes == NULL || g->hash_table == NULL || (edge_count > 0 && g->edges == NULL)) {
        free_graph(g);
        cJSON_Delete(nodes_root);
        cJSON_Delete(edges_root);
        errno = ENOMEM;
        return NULL;
    }

    for (int i = 0; i < g->hash_size; i++) {
        g->hash_table[i] = -1;
    }

    //loading nodes
    for (int i = 0; i < node_count; i++) {
        cJSON *node_json = cJSON_GetArrayItem(nodes_array, i);
        // cJSON_GetArrayItem(nodes_root, i) retrieves the JSON object at index i
        if (!cJSON_IsObject(node_json)) {
            free_graph(g);
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }

        cJSON *id_item   = cJSON_GetObjectItemCaseSensitive(node_json, "id");
        cJSON *name_item = cJSON_GetObjectItemCaseSensitive(node_json, "name");
        cJSON *lat_item  = cJSON_GetObjectItemCaseSensitive(node_json, "lat");
        cJSON *lon_item  = cJSON_GetObjectItemCaseSensitive(node_json, "lon");

        if (!is_valid_int(id_item) ||
            !cJSON_IsString(name_item) ||
            !cJSON_IsNumber(lat_item) ||
            !cJSON_IsNumber(lon_item)) {
            free_graph(g);                // CHECKING FIELDS ARE OF CORRECT DTATYPE
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }

        // Reject duplicate node IDs so each ID identifies exactly one location.
        if (find_index(g, id_item->valueint) != -1) {
            free_graph(g);
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }

        g->nodes[i].id = id_item->valueint;
        snprintf(g->nodes[i].name, sizeof(g->nodes[i].name), "%s", name_item->valuestring);
        g->nodes[i].lat = lat_item->valuedouble;
        g->nodes[i].lon = lon_item->valuedouble;

        unsigned int h = (unsigned int)g->nodes[i].id % (unsigned int)g->hash_size;
        while (g->hash_table[h] != -1) {
            h = (h + 1) % (unsigned int)g->hash_size;
        }
        g->hash_table[h] = i;
    }

    // loading edges 
    for (int i = 0; i < edge_count; i++) {
        cJSON *edge_json = cJSON_GetArrayItem(edges_array, i);

        //this checks only that the item is a JSON object
        if (!cJSON_IsObject(edge_json)) {
            free_graph(g);
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }

        //Each call retrieves a field from the current JSON edge object.
        cJSON *from_item = cJSON_GetObjectItemCaseSensitive(edge_json, "from");
        cJSON *to_item = cJSON_GetObjectItemCaseSensitive(edge_json, "to");
        cJSON *distance_item = cJSON_GetObjectItemCaseSensitive(edge_json, "distance_m");
        cJSON *time_item = cJSON_GetObjectItemCaseSensitive(edge_json, "avg_time_s");
        cJSON *bidirectional_item = cJSON_GetObjectItemCaseSensitive(edge_json, "bidirectional");

        if (!is_valid_int(from_item) ||
            !is_valid_int(to_item) ||
            !is_valid_int(distance_item) ||
            !is_valid_int(time_item) ||
            distance_item->valueint < 0 ||
            time_item->valueint < 0) {
            free_graph(g);                 // checks every field type 
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }
        if (!cJSON_IsBool(bidirectional_item) &&
            !(is_valid_int(bidirectional_item) &&
              (bidirectional_item->valueint == 0 || bidirectional_item->valueint == 1))) {
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            free_graph(g);
            errno = EINVAL;
            return NULL;
        }

        // Checkng whether both edge endpoints refer to existing nodes in the graph.
        if (!node_id_exists(g, from_item->valueint) || !node_id_exists(g, to_item->valueint)) {
            free_graph(g);
            cJSON_Delete(nodes_root);
            cJSON_Delete(edges_root);
            errno = EINVAL;
            return NULL;
        }

        g->edges[i].from = from_item->valueint;
        g->edges[i].to = to_item->valueint;
        g->edges[i].distance_m = distance_item->valueint;
        g->edges[i].avg_time_s = time_item->valueint;
        g->edges[i].bidirectional =
            (cJSON_IsTrue(bidirectional_item) ||
             (cJSON_IsNumber(bidirectional_item) && bidirectional_item->valueint == 1)) ? 1 : 0;
    }
    cJSON_Delete(nodes_root);
    cJSON_Delete(edges_root);

    g->adj_list = calloc((size_t)g->node_count, sizeof(AdjNode *));
    if (g->adj_list == NULL) {
        free_graph(g);
        errno = ENOMEM;
        return NULL;
    }
    for (int i = g->edge_count - 1; i >= 0; i--) {
        int u = find_index(g, g->edges[i].from);
        int v = find_index(g, g->edges[i].to);
        AdjNode *node = malloc(sizeof(AdjNode));
        if (node == NULL) { free_graph(g); errno = ENOMEM; return NULL; }
        *node = (AdjNode){ v, g->edges[i].distance_m, g->edges[i].avg_time_s, g->adj_list[u] };
        g->adj_list[u] = node;
        if (g->edges[i].bidirectional) {
            AdjNode *rev = malloc(sizeof(AdjNode));
            if (rev == NULL) { free_graph(g); errno = ENOMEM; return NULL; }
            *rev = (AdjNode){ u, g->edges[i].distance_m, g->edges[i].avg_time_s, g->adj_list[v] };
            g->adj_list[v] = rev;
        }
    }

    return g;
}

void free_graph(Graph *g)
{
    int saved_errno = errno;
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
    free(g->hash_table);
    free(g->nodes);
    free(g->edges);
    free(g);
    errno = saved_errno;
}


void free_route(Route *r)
{
    int saved_errno = errno;
    free(r);
    errno = saved_errno;
}


// DIJKSTRA'S ALGORITHM

typedef struct {
    int vertex;
    int dist;
    int time_s;
    int hops;
} HeapNode;

typedef struct {
    HeapNode *data;
    int size;
    int capacity;
} MinHeap;

static int heap_less(HeapNode a, HeapNode b)
{
    if (a.dist != b.dist) return a.dist < b.dist;
    if (a.time_s != b.time_s) return a.time_s < b.time_s;
    if (a.hops != b.hops) return a.hops < b.hops;
    return a.vertex < b.vertex;
}

static int push_heap(MinHeap *h, HeapNode item)
{
    if (h->size >= h->capacity) {
        int new_cap = h->capacity * 2;
        HeapNode *grown = realloc(h->data, (size_t)new_cap * sizeof(HeapNode));
        if (grown == NULL) return 0;
        h->data = grown;
        h->capacity = new_cap;
    }
    int i = h->size++;
    h->data[i] = item;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!heap_less(h->data[i], h->data[p]))
            break;
        HeapNode tmp = h->data[p];
        h->data[p] = h->data[i];
        h->data[i] = tmp;
        i = p;
    }
    return 1;
}

static HeapNode pop_heap(MinHeap *h)
{
    HeapNode top = h->data[0];
    h->data[0] = h->data[--h->size];
    int i = 0;
    for (;;) {
        int left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left < h->size && heap_less(h->data[left], h->data[smallest]))
            smallest = left;
        if (right < h->size && heap_less(h->data[right], h->data[smallest]))
            smallest = right;
        if (smallest == i) break;
        HeapNode tmp = h->data[i];
        h->data[i] = h->data[smallest];
        h->data[smallest] = tmp;
        i = smallest;
    }
    return top;
}

static int relax_neighbor(MinHeap *heap, int *dist, int *best_time, int *hops,
                          int *previous, const int *visited, int node_count,
                          int current, int neighbor, int edge_dist, int edge_time)
{
    if (neighbor < 0 || neighbor >= node_count || visited[neighbor] ||
        edge_dist < 0 || edge_time < 0) {
        return 1;
    }
    if (dist[current] > INT_MAX - edge_dist || best_time[current] > INT_MAX - edge_time) {
        return 1;
    }
    int new_dist = dist[current] + edge_dist;
    int new_time = best_time[current] + edge_time;
    int new_hops = hops[current] + 1;
    if (new_dist < dist[neighbor] ||
        (new_dist == dist[neighbor] && new_time < best_time[neighbor]) ||
        (new_dist == dist[neighbor] && new_time == best_time[neighbor] && new_hops < hops[neighbor])) {
        dist[neighbor] = new_dist;
        best_time[neighbor] = new_time;
        hops[neighbor] = new_hops;
        previous[neighbor] = current;
        return push_heap(heap, (HeapNode){ neighbor, new_dist, new_time, new_hops });
    }
    return 1;
}

Route *dijkstra(Graph *g, int from_id, int to_id)
{
    if (g == NULL || g->nodes == NULL || g->node_count <= 0 || g->edge_count < 0 ||
        (g->adj_list == NULL && g->edge_count > 0 && g->edges == NULL)) {
        errno = EINVAL;
        return NULL;
    }

    int source_index = find_index(g, from_id);
    int destination_index = find_index(g, to_id);

    if (source_index == -1 || destination_index == -1) {
        errno = EINVAL;
        return NULL;
    }

    int initial_cap = g->edge_count * 2 + g->node_count * 2 + 4;
    int *dist      = malloc((size_t)g->node_count * sizeof(int));
    int *best_time = malloc((size_t)g->node_count * sizeof(int));
    int *hops      = malloc((size_t)g->node_count * sizeof(int));
    int *visited   = malloc((size_t)g->node_count * sizeof(int));
    int *previous  = malloc((size_t)g->node_count * sizeof(int));
    MinHeap heap   = { malloc((size_t)initial_cap * sizeof(HeapNode)), 0, initial_cap };

    if (dist == NULL || best_time == NULL || hops == NULL ||
        visited == NULL || previous == NULL || heap.data == NULL) {
        free(dist);
        free(best_time);
        free(hops);
        free(visited);
        free(previous);
        free(heap.data);
        errno = ENOMEM;
        return NULL;
    }

    for (int i = 0; i < g->node_count; i++) {
        dist[i] = INT_MAX;
        best_time[i] = INT_MAX;
        hops[i] = INT_MAX;
        visited[i] = 0;
        previous[i] = -1;
    }
    dist[source_index] = 0;
    best_time[source_index] = 0;
    hops[source_index] = 0;
    push_heap(&heap, (HeapNode){ source_index, 0, 0, 0 });

    while (heap.size > 0) {
        int current = pop_heap(&heap).vertex;
        if (visited[current]) continue;
        visited[current] = 1;
        if (current == destination_index) break;

        if (g->adj_list != NULL) {
            for (AdjNode *adj = g->adj_list[current]; adj != NULL; adj = adj->next) {
                if (!relax_neighbor(&heap, dist, best_time, hops, previous, visited,
                                    g->node_count, current, adj->to_index,
                                    adj->distance_m, adj->avg_time_s)) {
                    free(heap.data);
                    free(dist);
                    free(best_time);
                    free(hops);
                    free(visited);
                    free(previous);
                    errno = ENOMEM;
                    return NULL;
                }
            }
        } else {
            int current_id = g->nodes[current].id;
            for (int e = 0; e < g->edge_count; e++) {
                int neighbor_id = -1;
                if (g->edges[e].from == current_id) {
                    neighbor_id = g->edges[e].to;
                } else if (g->edges[e].bidirectional && g->edges[e].to == current_id) {
                    neighbor_id = g->edges[e].from;
                }
                if (neighbor_id != -1) {
                    int neighbor_index = find_index(g, neighbor_id);
                    if (!relax_neighbor(&heap, dist, best_time, hops, previous, visited,
                                        g->node_count, current, neighbor_index,
                                        g->edges[e].distance_m, g->edges[e].avg_time_s)) {
                        free(heap.data);
                        free(dist);
                        free(best_time);
                        free(hops);
                        free(visited);
                        free(previous);
                        errno = ENOMEM;
                        return NULL;
                    }
                }
            }
        }
    }
    free(heap.data);
    if (dist[destination_index] == INT_MAX) {
        free(dist);
        free(best_time);
        free(hops);
        free(visited);
        free(previous);
        errno = ENOENT;
        return NULL;
    }

    Route *route = malloc(sizeof(Route));

    if (route == NULL) {
        free(dist);
        free(best_time);
        free(hops);
        free(visited);
        free(previous);
        errno = ENOMEM;
        return NULL;
    }
    int current = destination_index;

    route->path_length = 0;
    while (current != -1) {
        if (route->path_length >= (int)(sizeof(route->path_node_ids) / sizeof(route->path_node_ids[0]))) {
            free(route);
            free(dist);
            free(best_time);
            free(hops);
            free(visited);
            free(previous);
            errno = ERANGE;
            return NULL;
        }
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
    route->total_time_s = best_time[destination_index];
    free(dist);
    free(best_time);
    free(hops);
    free(visited);
    free(previous);
    return route;
}