/* bonnie.cpp - Bonnie-equivalent sequential benchmark (steady-state ready)
 * Usage:
 *   ./bonnie <dataset_MB> [write_passes]
 *
 * Example:
 *   ./bonnie 150
 *   ./bonnie 200 1
 *   ./bonnie 450 1
 */

#include "ssd.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

using namespace ssd;

static inline double max2(double a, double b){ return (a > b) ? a : b; }

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <dataset_MB> [write_passes]\n", argv[0]);
        return 1;
    }

    int dataset_mb = atoi(argv[1]);
    int write_passes = (argc >= 3) ? atoi(argv[2]) : 1;   // paper-like: 1 pass is fine
    if (write_passes < 1) write_passes = 1;

    load_config();
    print_config(NULL);
    printf("\n");

    const uint64_t total_pages = (uint64_t)dataset_mb * 1024ULL * 1024ULL / (uint64_t)PAGE_SIZE;

    Ssd ssd;

    // timing model: fixed arrival gap to avoid fully serializing requests
    const double ARRIVAL_GAP_US = 1.0;

    double now = 0.0;
    double end_time = 0.0;

    uint64_t writes = 0, reads = 0;
    double sum_write_lat = 0.0, sum_read_lat = 0.0;

    printf("Bonnie-equivalent Sequential Write (passes=%d)\n", write_passes);
    for (int pass = 0; pass < write_passes; pass++) {
        for (uint64_t lpn = 0; lpn < total_pages; lpn++) {
            double lat = ssd.event_arrive(WRITE, (ulong)lpn, 1, now);
            sum_write_lat += lat;
            writes++;
            end_time = max2(end_time, now + lat);
            now += ARRIVAL_GAP_US;
        }
    }

    printf("Bonnie-equivalent Sequential Read\n");
    for (uint64_t lpn = 0; lpn < total_pages; lpn++) {
        double lat = ssd.event_arrive(READ, (ulong)lpn, 1, now);
        sum_read_lat += lat;
        reads++;
        end_time = max2(end_time, now + lat);
        now += ARRIVAL_GAP_US;
    }

    // Throughput based on completed simulated time
    double avg_resp =  (sum_read_lat + sum_write_lat) /  (reads + writes);
    const double sim_time_us = end_time; // end_time is last completion timestamp
    const double total_bytes = (double)(writes + reads) * (double)PAGE_SIZE;
    const double throughput_MBps = (sim_time_us > 0.0)
        ? (total_bytes / (1024.0 * 1024.0)) / (sim_time_us / 1e6)
        : 0.0;

    printf("\n==== Bonnie Results ====\n");
    printf("Dataset: %d MB (%llu pages)\n", dataset_mb, (unsigned long long)total_pages);
    printf("Avg write latency: %.2f us\n", (writes ? (sum_write_lat / (double)writes) : 0.0));
    printf("Avg read latency : %.2f us\n", (reads  ? (sum_read_lat  / (double)reads ) : 0.0));
    printf("Avg response time: %.2f us\n", avg_resp);
    printf("Sim end time     : %.2f us (%.6f s)\n", sim_time_us, sim_time_us / 1e6);
    printf("Throughput       : %.2f MB/s\n", throughput_MBps);

    ssd.print_statistics();
    return 0;
}
