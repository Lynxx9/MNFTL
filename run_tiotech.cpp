/* tiotech.cpp - Tiobench-equivalent interleaved workload (steady-state)
 * Usage:
 *   ./tiotech <num_threads> [dataset_MB] [write_ratio] [ops_multiplier] [warmup_multiplier] [seed]
 *
 * Defaults:
 *   dataset_MB        = 200
 *   write_ratio       = 0.5
 *   ops_multiplier    = 20
 *   warmup_multiplier = 2
 *
 * Example:
 *   ./tiotech 4
 *   ./tiotech 6
 *   ./tiotech 6 200 0.5 20 2 1
 */

#include "ssd.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

using namespace ssd;

static inline double max2(double a, double b){ return (a > b) ? a : b; }
static inline double urand01() { return (double)rand() / (double)RAND_MAX; }

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <num_threads> [dataset_MB] [write_ratio] [ops_multiplier] [warmup_multiplier] [seed]\n", argv[0]);
        return 1;
    }

    int threads = atoi(argv[1]);
    int dataset_mb = (argc >= 3) ? atoi(argv[2]) : 200;
    double write_ratio = (argc >= 4) ? atof(argv[3]) : 0.5;
    int ops_mul = (argc >= 5) ? atoi(argv[4]) : 20;
    int warmup_mul = (argc >= 6) ? atoi(argv[5]) : 2;
    int seed = (argc >= 7) ? atoi(argv[6]) : 1;

    if (threads < 1) threads = 1;
    if (write_ratio < 0.0) write_ratio = 0.0;
    if (write_ratio > 1.0) write_ratio = 1.0;
    if (ops_mul < 1) ops_mul = 1;
    if (warmup_mul < 0) warmup_mul = 0;

    load_config();
    print_config(NULL);
    printf("\n");

    srand(seed);
    Ssd ssd;

    const uint64_t dataset_pages = (uint64_t)dataset_mb * 1024ULL * 1024ULL / (uint64_t)PAGE_SIZE;

    // Per-thread region size; allow some overlap (more contention like real fs)
    const double OVERLAP_RATIO = 0.5; // 0.0 = disjoint, 0.5 = half overlap
    uint64_t region_pages = dataset_pages / (uint64_t)threads;
    if (region_pages < 1) region_pages = 1;

    // ops per thread scaled up to drive steady-state overwrite
    const uint64_t warmup_rounds  = (region_pages * (uint64_t)warmup_mul);
    const uint64_t measured_rounds= (region_pages * (uint64_t)ops_mul);

    const double ARRIVAL_GAP_US = 1.0;
    double now = 0.0;
    double end_time = 0.0;

    printf("Tiobench-equivalent Interleaved workload\n");
    printf("threads=%d, dataset=%dMB (%llu pages), write_ratio=%.2f\n",
           threads, dataset_mb, (unsigned long long)dataset_pages, write_ratio);
    printf("region_pages/thread=%llu, overlap_ratio=%.2f\n",
           (unsigned long long)region_pages, OVERLAP_RATIO);
    printf("warmup_rounds=%llu, measured_rounds=%llu, seed=%d\n",
           (unsigned long long)warmup_rounds,
           (unsigned long long)measured_rounds,
           seed);

    // 0) Prefill entire dataset once to make random reads valid
    printf("Prefill (sequential write once)...\n");
    for (uint64_t lpn = 0; lpn < dataset_pages; lpn++) {
        double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
        end_time = max2(end_time, now + lat);
        now += ARRIVAL_GAP_US;
    }

    auto pick_lpn_for_thread = [&](int th) -> uint64_t {
        // stride reduced by overlap
        double stride = (double)region_pages * (1.0 - OVERLAP_RATIO);
        if (stride < 1.0) stride = 1.0;

        uint64_t start = (uint64_t)((double)th * stride);
        if (start >= dataset_pages) start = start % dataset_pages;

        uint64_t local = (uint64_t)(rand() % (int)region_pages);
        uint64_t lpn = start + local;
        if (lpn >= dataset_pages) lpn = lpn % dataset_pages;
        return lpn;
    };

    // 1) Warm-up (not measured)
    printf("Warm-up (not measured)...\n");
    for (uint64_t r = 0; r < warmup_rounds; r++) {
        for (int th = 0; th < threads; th++) {
            uint64_t lpn = pick_lpn_for_thread(th);
            if (urand01() < write_ratio) {
                double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
                end_time = max2(end_time, now + lat);
            } else {
                double lat = ssd.event_arrive(READ, (ulong)lpn, 1, now);
                end_time = max2(end_time, now + lat);
            }
            now += ARRIVAL_GAP_US;
        }
    }

    // 2) Measured
    uint64_t writes = 0, reads = 0;
    double sum_write_lat = 0.0, sum_read_lat = 0.0;

    printf("Measured phase...\n");
    for (uint64_t r = 0; r < measured_rounds; r++) {
        for (int th = 0; th < threads; th++) {
            uint64_t lpn = pick_lpn_for_thread(th);
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
    }

    double avg_resp =  (sum_read_lat + sum_write_lat) /  (reads + writes);
    const double sim_time_us = end_time;
    const double total_bytes = (double)(writes + reads) * (double)PAGE_SIZE;
    const double throughput_MBps = (sim_time_us > 0.0)
        ? (total_bytes / (1024.0 * 1024.0)) / (sim_time_us / 1e6)
        : 0.0;

    printf("\n==== Tiobench Results (Measured Phase) ====\n");
    if (writes) printf("Avg write latency: %.2f us\n", sum_write_lat / (double)writes);
    if (reads)  printf("Avg read latency : %.2f us\n", sum_read_lat  / (double)reads );
    printf("Avg response time: %.2f us\n", avg_resp);
    printf("Measured ops: R=%llu W=%llu\n", (unsigned long long)reads, (unsigned long long)writes);
    printf("Sim end time: %.2f us (%.6f s)\n", sim_time_us, sim_time_us / 1e6);
    printf("Throughput  : %.2f MB/s\n", throughput_MBps);

    ssd.print_statistics();
    return 0;
}
