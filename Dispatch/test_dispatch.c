#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "dispatch.h"
#include "../GraphRouting/graph.h"

#define EPSILON 0.0001

static void test_compute_priority_basic(void) {
    printf("[TEST] compute_priority() basic calculation...\n");
    Request r;
    strcpy(r.id, "R_TEST");
    r.pickup = 1;
    r.destination = 4;
    r.passengers = 2;
    r.urgency = 2;
    r.request_time = 30;
    r.assigned = 0;

    double u_weight = 1.0;
    double a_weight = 0.5;
    int current_time = 50;

    /* wait_time = 50 - 30 = 20
     * priority = (1.0 * 2) + (0.5 * 20) = 2.0 + 10.0 = 12.0
     */
    double prio = compute_priority(&r, u_weight, a_weight, current_time);
    assert(fabs(prio - 12.0) < EPSILON);
    printf("  -> PASS (prio = %.2f, expected 12.00)\n", prio);
}

static void test_starvation_scenario(void) {
    printf("[TEST] Starvation scenario (aging gives priority to older low-urgency request)...\n");
    /*
     * Request A: urgency = 3, arrived at current_time (wait = 0)
     * Request B: urgency = 1, arrived 100s ago (wait = 100)
     * u_weight = 1.0, a_weight = 0.5
     * Score A = (1.0 * 3) + (0.5 * 0) = 3.0
     * Score B = (1.0 * 1) + (0.5 * 100) = 51.0
     * Request B should win.
     */
    Request r_a;
    strcpy(r_a.id, "RA");
    r_a.urgency = 3;
    r_a.request_time = 100;
    r_a.passengers = 2;
    r_a.pickup = 1;
    r_a.destination = 4;
    r_a.assigned = 0;

    Request r_b;
    strcpy(r_b.id, "RB");
    r_b.urgency = 1;
    r_b.request_time = 0;
    r_b.passengers = 2;
    r_b.pickup = 1;
    r_b.destination = 4;
    r_b.assigned = 0;

    int current_time = 100;
    double score_a = compute_priority(&r_a, 1.0, 0.5, current_time);
    double score_b = compute_priority(&r_b, 1.0, 0.5, current_time);

    assert(fabs(score_a - 3.0) < EPSILON);
    assert(fabs(score_b - 51.0) < EPSILON);
    assert(score_b > score_a);
    printf("  -> PASS (Score A = %.2f, Score B = %.2f, older request B prioritized)\n", score_a, score_b);
}

static void test_json_loading(void) {
    printf("[TEST] Loading real JSON files (Data/campus_buggies.json, Data/requests.json)...\n");
    int buggy_count = 0;
    Buggy* fleet = load_fleet("../Data/campus_buggies.json", &buggy_count);
    assert(fleet != NULL);
    assert(buggy_count == 5);
    assert(strcmp(fleet[0].id, "BG1") == 0);
    assert(fleet[0].capacity == 6);
    assert(fleet[0].status == STATUS_AVAILABLE);
    assert(fleet[2].status == STATUS_NOT_AVAILABLE); /* BG3 is NOT_AVAILABLE in json */
    printf("  -> Fleet loaded successfully: %d buggies\n", buggy_count);

    int req_count = 0;
    Request* reqs = load_requests("../Data/requests.json", &req_count);
    assert(reqs != NULL);
    assert(req_count == 3);
    assert(strcmp(reqs[0].id, "R1") == 0);
    assert(reqs[0].pickup == 1);
    assert(reqs[0].passengers == 2);
    printf("  -> Requests loaded successfully: %d requests\n", req_count);

    free_fleet(fleet);
    free_requests(reqs);
    printf("  -> PASS\n");
}

static void test_dispatch_tick_assignment(void) {
    printf("[TEST] run_dispatch_tick() closest buggy assignment & capacity constraint...\n");
    Buggy fleet[3];
    /* Buggy 0: at node 10, capacity 2, AVAILABLE */
    strcpy(fleet[0].id, "BG1");
    fleet[0].capacity = 2;
    fleet[0].current_location = 10;
    fleet[0].status = STATUS_AVAILABLE;
    fleet[0].speed_mps = 5.0;

    /* Buggy 1: at node 2, capacity 6, AVAILABLE */
    strcpy(fleet[1].id, "BG2");
    fleet[1].capacity = 6;
    fleet[1].current_location = 2;
    fleet[1].status = STATUS_AVAILABLE;
    fleet[1].speed_mps = 5.0;

    /* Buggy 2: at node 1, capacity 6, NOT_AVAILABLE */
    strcpy(fleet[2].id, "BG3");
    fleet[2].capacity = 6;
    fleet[2].current_location = 1;
    fleet[2].status = STATUS_NOT_AVAILABLE;
    fleet[2].speed_mps = 5.0;

    Request reqs[2];
    /* Request 0: 4 passengers (BG1 cannot take this!), pickup at node 1 */
    strcpy(reqs[0].id, "R1");
    reqs[0].pickup = 1;
    reqs[0].destination = 5;
    reqs[0].passengers = 4;
    reqs[0].urgency = 2;
    reqs[0].request_time = 0;
    reqs[0].assigned = 0;

    /* Request 1: 1 passenger, pickup at node 10, but arrived later (lower prio at time 0) */
    strcpy(reqs[1].id, "R2");
    reqs[1].pickup = 10;
    reqs[1].destination = 5;
    reqs[1].passengers = 1;
    reqs[1].urgency = 1;
    reqs[1].request_time = 0;
    reqs[1].assigned = 0;

    /* Graph mock pointer: NULL will use stub_graph heuristic */
    AssignmentResult res = run_dispatch_tick(fleet, 3, reqs, 2, NULL, 0, 1.0, 0.5);

    assert(res.success == 1);
    /* R1 has higher priority (urgency 2 vs 1).
     * Only BG2 has capacity >= 4 (BG1 has capacity 2, BG3 is NOT_AVAILABLE).
     * Therefore, BG2 must be assigned to R1!
     */
    assert(strcmp(res.request_id, "R1") == 0);
    assert(strcmp(res.assigned_buggy, "BG2") == 0);
    assert(fleet[1].status == STATUS_NOT_AVAILABLE);
    assert(reqs[0].assigned == 1);
    printf("  -> PASS (Assigned %s to %s, wait_time: %ds, eta: %ds)\n", res.assigned_buggy, res.request_id, res.wait_time_s, res.eta_s);

    /* Second tick: R1 is assigned, now R2 should get BG1 (at node 10, same as R2's pickup -> eta = 0) */
    AssignmentResult res2 = run_dispatch_tick(fleet, 3, reqs, 2, NULL, 10, 1.0, 0.5);
    assert(res2.success == 1);
    assert(strcmp(res2.request_id, "R2") == 0);
    assert(strcmp(res2.assigned_buggy, "BG1") == 0);
    assert(res2.eta_s == 0); /* BG1 is at node 10, pickup is 10 */
    assert(res2.wait_time_s == 10);
    printf("  -> PASS (Second tick: Assigned %s to %s with eta %ds)\n", res2.assigned_buggy, res2.request_id, res2.eta_s);

    /* Third tick: All available buggies are now NOT_AVAILABLE */
    AssignmentResult res3 = run_dispatch_tick(fleet, 3, reqs, 2, NULL, 20, 1.0, 0.5);
    assert(res3.success == 0);
    printf("  -> PASS (Third tick: Correctly handled no available buggies)\n");
}

int main(void) {
    printf("====================================================\n");
    printf(" RUNNING DISPATCH MODULE UNIT TESTS \n");
    printf("====================================================\n");

    test_compute_priority_basic();
    test_starvation_scenario();
    test_json_loading();
    test_dispatch_tick_assignment();

    printf("\n====================================================\n");
    printf(" ALL DISPATCH TESTS PASSED SUCCESSFULLY! \n");
    printf("====================================================\n");
    return 0;
}
