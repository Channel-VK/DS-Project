#ifndef DISPATCH_H
#define DISPATCH_H

#include "../GraphRouting/graph.h"

#define STATUS_NOT_AVAILABLE 0
#define STATUS_AVAILABLE     1

typedef struct {
    char id[16];
    int capacity;
    int current_location;
    int status;          /* 1 = AVAILABLE, 0 = NOT_AVAILABLE */
    double speed_mps;
} Buggy;

typedef struct {
    char id[16];
    int pickup;
    int destination;
    int passengers;
    int urgency;
    int request_time;
    int assigned;        /* 0 = pending, 1 = assigned */
} Request;

typedef struct {
    int success;                /* 1 = assignment made, 0 = no assignment */
    char assigned_buggy[16];
    char request_id[16];
    int wait_time_s;
    int eta_s;
} AssignmentResult;

/* Function declarations */
Buggy* load_fleet(const char* buggies_path, int* count_out);
Request* load_requests(const char* requests_path, int* count_out);
void free_fleet(Buggy* fleet);
void free_requests(Request* requests);

double compute_priority(Request* r, double urgency_weight,
                         double aging_weight, int current_time);

AssignmentResult run_dispatch_tick(Buggy* fleet, int fleet_count,
                                    Request* pending, int pending_count,
                                    Graph* g, int current_time,
                                    double urgency_weight,
                                    double aging_weight);

#endif /* DISPATCH_H */
