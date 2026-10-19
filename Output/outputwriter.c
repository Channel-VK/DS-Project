#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "outputwriter.h"

/* Round to 1 decimal place (values here are never negative) */
static double round1(double x) {
    return (double)((long)(x * 10.0 + 0.5)) / 10.0;
}

int write_results_json(const char* path, const Metrics* m,
                       const RequestOutcome* outcomes, int outcome_count) {
    int i;
    cJSON* root = cJSON_CreateObject();
    cJSON* summary = cJSON_CreateObject();
    cJSON* per_request = cJSON_CreateArray();
    char* text;
    FILE* f;

    if (!root || !summary || !per_request) return 0;

    cJSON_AddItemToObject(root, "summary", summary);
    cJSON_AddStringToObject(summary, "algorithm", m->algorithm);
    cJSON_AddStringToObject(summary, "routing", m->routing);
    cJSON_AddNumberToObject(summary, "total_requests", m->total_requests);
    cJSON_AddNumberToObject(summary, "served", m->served);
    cJSON_AddNumberToObject(summary, "unserved", m->unserved);
    cJSON_AddNumberToObject(summary, "avg_wait_time_s", round1(m->avg_wait_time_s));
    cJSON_AddNumberToObject(summary, "avg_buggy_utilization_pct",
                            round1(m->avg_buggy_utilization_pct));

    cJSON_AddItemToObject(root, "per_request", per_request);
    for (i = 0; i < outcome_count; i++) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", outcomes[i].request_id);
        cJSON_AddStringToObject(o, "assigned_buggy", outcomes[i].assigned_buggy);
        cJSON_AddNumberToObject(o, "wait_time_s", outcomes[i].wait_time_s);
        cJSON_AddNumberToObject(o, "eta_s", outcomes[i].eta_s);
        cJSON_AddItemToArray(per_request, o);
    }

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return 0;

    f = fopen(path, "w");
    if (!f) { free(text); return 0; }
    fputs(text, f);
    fputc('\n', f);
    fclose(f);
    free(text);
    return 1;
}

int write_results_csv(const char* path, const Metrics* m,
                      const RequestOutcome* outcomes, int outcome_count) {
    int i;
    FILE* f = fopen(path, "w");
    if (!f) return 0;

    fprintf(f, "request_id,assigned_buggy,wait_time_s,eta_s\n");
    for (i = 0; i < outcome_count; i++) {
        fprintf(f, "%s,%s,%d,%d\n", outcomes[i].request_id,
                outcomes[i].assigned_buggy, outcomes[i].wait_time_s,
                outcomes[i].eta_s);
    }

    fprintf(f, "\nmetric,value\n");
    fprintf(f, "algorithm,%s\n", m->algorithm);
    fprintf(f, "routing,%s\n", m->routing);
    fprintf(f, "total_requests,%d\n", m->total_requests);
    fprintf(f, "served,%d\n", m->served);
    fprintf(f, "unserved,%d\n", m->unserved);
    fprintf(f, "avg_wait_time_s,%.1f\n", round1(m->avg_wait_time_s));
    fprintf(f, "avg_buggy_utilization_pct,%.1f\n",
            round1(m->avg_buggy_utilization_pct));

    fclose(f);
    return 1;
}