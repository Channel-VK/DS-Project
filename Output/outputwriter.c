#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "cJSON.h"
#include "outputwriter.h"

/* Round to 1 decimal place (values here are never negative) */
static double round1(double x) {
    return (double)((long)(x * 10.0 + 0.5)) / 10.0;
}

int write_results_json(const char* path, const Metrics* m,
                       const RequestOutcome* outcomes, int outcome_count) {
    int i;
    cJSON* root;
    cJSON* summary;
    cJSON* per_request;
    char* text;
    FILE* f;

    if (!path || !m || outcome_count < 0 || (outcome_count > 0 && !outcomes)) {
        errno = EINVAL;
        return 0;
    }

    root = cJSON_CreateObject();
    summary = cJSON_CreateObject();
    per_request = cJSON_CreateArray();

    if (!root || !summary || !per_request) {
        cJSON_Delete(root);
        cJSON_Delete(summary);
        cJSON_Delete(per_request);
        errno = ENOMEM;
        return 0;
    }

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
        if (!o) {
            cJSON_Delete(root);
            errno = ENOMEM;
            return 0;
        }
        cJSON_AddStringToObject(o, "id", outcomes[i].request_id);
        cJSON_AddStringToObject(o, "assigned_buggy", outcomes[i].assigned_buggy);
        cJSON_AddNumberToObject(o, "wait_time_s", outcomes[i].wait_time_s);
        cJSON_AddNumberToObject(o, "eta_s", outcomes[i].eta_s);
        cJSON_AddItemToArray(per_request, o);
    }

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        errno = ENOMEM;
        return 0;
    }

    f = fopen(path, "w");
    if (!f) {
        int err = errno ? errno : EIO;
        free(text);
        errno = err;
        return 0;
    }
    if (fputs(text, f) == EOF || fputc('\n', f) == EOF || ferror(f)) {
        int err = errno ? errno : EIO;
        fclose(f);
        free(text);
        errno = err;
        return 0;
    }
    if (fclose(f) != 0) {
        int err = errno ? errno : EIO;
        free(text);
        errno = err;
        return 0;
    }
    free(text);
    return 1;
}

int write_results_csv(const char* path, const Metrics* m,
                      const RequestOutcome* outcomes, int outcome_count) {
    int i;
    FILE* f;

    if (!path || !m || outcome_count < 0 || (outcome_count > 0 && !outcomes)) {
        errno = EINVAL;
        return 0;
    }

    f = fopen(path, "w");
    if (!f) {
        if (errno == 0) errno = EIO;
        return 0;
    }

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

    if (ferror(f)) {
        int err = errno ? errno : EIO;
        fclose(f);
        errno = err;
        return 0;
    }
    if (fclose(f) != 0) {
        if (errno == 0) errno = EIO;
        return 0;
    }
    return 1;
}