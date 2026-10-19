#ifndef OUTPUT_WRITER_H
#define OUTPUT_WRITER_H

/* One row per SERVED request (matches the per_request block in results.json) */
typedef struct {
    char request_id[16];
    char assigned_buggy[16];
    int wait_time_s;
    int eta_s;
} RequestOutcome;

/* Matches the "summary" block in results.json */
typedef struct {
    const char* algorithm;
    const char* routing;
    int total_requests;
    int served;
    int unserved;
    double avg_wait_time_s;
    double avg_buggy_utilization_pct;
} Metrics;

/* Both return 1 on success, 0 on failure */
int write_results_json(const char* path, const Metrics* m,
                       const RequestOutcome* outcomes, int outcome_count);
int write_results_csv(const char* path, const Metrics* m,
                      const RequestOutcome* outcomes, int outcome_count);

#endif /* OUTPUT_WRITER_H */