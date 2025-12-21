/* postmark.cpp - Postmark-equivalent random R/W workload (steady-state)
 * Usage:
 *   ./postmark <dataset_MB> <write_ratio> [ops_multiplier] [warmup_multiplier] [seed]
 *
 * Recommended:
 *   ops_multiplier    = 20  (measured ops = working_set_pages * 20)
 *   warmup_multiplier =  2  (warmup ops   = working_set_pages * 2)
 *
 * Example:
 *   ./postmark 150 0.8786
 *   ./postmark 200 0.9914
 *   ./postmark 450 0.9879
 */

#include "ssd.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

using namespace ssd;

static inline double max2(double a, double b){ return (a > b) ? a : b; }

static inline double urand01() {
    return (double)rand() / (double)RAND_MAX;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s <dataset_MB> <write_ratio> [ops_multiplier] [warmup_multiplier] [seed]\n", argv[0]);
        return 1;
    }

    int dataset_mb = atoi(argv[1]);
    double write_ratio = atof(argv[2]);
    if (write_ratio < 0.0) write_ratio = 0.0;
    if (write_ratio > 1.0) write_ratio = 1.0;

    int ops_mul    = (argc >= 4) ? atoi(argv[3]) : 20;
    int warmup_mul = (argc >= 5) ? atoi(argv[4]) : 2;
    int seed       = (argc >= 6) ? atoi(argv[5]) : 1;

    if (ops_mul < 1) ops_mul = 1;
    if (warmup_mul < 0) warmup_mul = 0;

    load_config();
    print_config(NULL);
    printf("\n");

    srand(seed);
    Ssd ssd;

    const uint64_t working_set_pages = (uint64_t)dataset_mb * 1024ULL * 1024ULL / (uint64_t)PAGE_SIZE;

    // Measured ops must be >> working set to reach overwrite steady-state
    const uint64_t warmup_ops  = working_set_pages * (uint64_t)warmup_mul;
    const uint64_t measured_ops= working_set_pages * (uint64_t)ops_mul;

    const double ARRIVAL_GAP_US = 1.0;
    double now = 0.0;
    double end_time = 0.0;

    printf("Postmark-equivalent Random R/W workload\n");
    printf("Working set: %d MB (%llu pages)\n", dataset_mb, (unsigned long long)working_set_pages);
    printf("write_ratio=%.4f, warmup_ops=%llu, measured_ops=%llu, seed=%d\n",
           write_ratio,
           (unsigned long long)warmup_ops,
           (unsigned long long)measured_ops,
           seed);

    // 0) Prefill: ensure every page has been written at least once
    printf("Prefill (sequential write once)...\n");
    for (uint64_t lpn = 0; lpn < working_set_pages; lpn++) {
        double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
        end_time = max2(end_time, now + lat);
        now += ARRIVAL_GAP_US;
    }

    // 1) Warm-up (not measured): drive mapping/GC into steady state
    printf("Warm-up (not measured)...\n");
    for (uint64_t i = 0; i < warmup_ops; i++) {
        uint64_t lpn = (uint64_t)(rand() % (int)working_set_pages);
        if (urand01() < write_ratio) {
            double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
            end_time = max2(end_time, now + lat);
        } else {
            double lat = ssd.event_arrive(READ, (ulong)lpn, 1, now);
            end_time = max2(end_time, now + lat);
        }
        now += ARRIVAL_GAP_US;
    }

    // 2) Measured phase
    uint64_t writes = 0, reads = 0;
    double sum_write_lat = 0.0, sum_read_lat = 0.0;

    printf("Measured phase...\n");
    for (uint64_t i = 0; i < measured_ops; i++) {
        uint64_t lpn = (uint64_t)(rand() % (int)working_set_pages);
        if (urand01() < write_ratio) {
            double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
            sum_write_lat += lat;
            writes++;
            end_time = max2(end_time, now + lat);
        } else {
            double lat = ssd.event_arrive(READ, (ulong)lpn, 1, now);
            sum_read_lat += lat;
            reads++;
            end_time = max2(end_time, now + lat);
        }
        now += ARRIVAL_GAP_US;
    }

    double avg_resp =  (sum_read_lat + sum_write_lat) /  (reads + writes);
    const double sim_time_us = end_time;
    const double total_bytes = (double)(writes + reads) * (double)PAGE_SIZE;
    const double throughput_MBps = (sim_time_us > 0.0)
        ? (total_bytes / (1024.0 * 1024.0)) / (sim_time_us / 1e6)
        : 0.0;

    printf("\n==== Postmark Results (Measured Phase) ====\n");
    if (writes) printf("Avg write latency: %.2f us\n", sum_write_lat / (double)writes);
    if (reads)  printf("Avg read latency : %.2f us\n", sum_read_lat  / (double)reads );
    printf("Avg response time: %.2f us\n", avg_resp);
    printf("Measured ops: R=%llu W=%llu\n", (unsigned long long)reads, (unsigned long long)writes);
    printf("Sim end time: %.2f us (%.6f s)\n", sim_time_us, sim_time_us / 1e6);
    printf("Throughput  : %.2f MB/s\n", throughput_MBps);

    ssd.print_statistics();
    return 0;
}
