/*
   Arena Allocator Benchmark

   This benchmark compares the performance of mutex-protected Arena vs lock-free ArenaLockFree allocators.

   The benchmark has two phases:
   - Phase 1: Uses mutex-protected Arena allocator
   - Phase 2: Uses lock-free ArenaLockFree allocator

   Each phase spawns multiple threads that compete to allocate slots until the arena is full.

   Configuration:
   - Arena capacity: 200 MB
   - Slot size: 4 KB
   - Total slots: 51,200
   - Thread count: Configurable (default: 4)

   Metrics tracked:
   - Time to fill all slots in each phase
   - Number of slots allocated
   - CAS retry count (Phase 2 only)
   - Speedup comparison (Phase 1 time / Phase 2 time)

   How to compile:
     g++ -std=c++20 -O2 -pthread -o benchmark benchmark.cpp arena_allocator.cpp

   How to run:
     ./benchmark           # Run with default 4 threads
     ./benchmark 2         # Run with 2 threads
     ./benchmark 8         # Run with 8 threads
     ./benchmark 16        # Run with 16 threads
*/

#include "arena_allocator.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

// Benchmark configuration
struct BenchmarkConfig {
    size_t arena_capacity;
    size_t slot_size;
    uint32_t num_threads;
};

// Shared counter for allocated slots across all threads
std::atomic<uint32_t> global_allocated_count{0};

// Worker function for Phase 1 (mutex-protected Arena)
void worker_phase1(Arena* arena, size_t slot_size, uint32_t* thread_allocations) {
    uint32_t local_count = 0;
    while (true) {
        char* slot = arena->allocate(slot_size);
        if (slot == NULL) {
            break; // Arena is full
        }
        local_count++;
        global_allocated_count.fetch_add(1, std::memory_order_relaxed);
    }
    *thread_allocations = local_count;
}

// Worker function for Phase 2 (lock-free ArenaLockFree)
void worker_phase2(ArenaLockFree* arena, size_t slot_size, uint32_t* thread_allocations) {
    uint32_t local_count = 0;
    while (true) {
        char* slot = arena->allocate(slot_size);
        if (slot == NULL) {
            break; // Arena is full
        }
        local_count++;
        global_allocated_count.fetch_add(1, std::memory_order_relaxed);
    }
    *thread_allocations = local_count;
}

void run_benchmark(const BenchmarkConfig& config) {
    printf("=== Arena Allocator Benchmark ===\n");
    printf("Arena Capacity: %zu MB\n", config.arena_capacity / (1024 * 1024));
    printf("Slot Size: %zu KB\n", config.slot_size / 1024);

    // Calculate total slots
    uint32_t total_slots = (config.arena_capacity + config.slot_size - 1) / config.slot_size;
    total_slots = (total_slots < 64) ? 64 : total_slots;
    if (total_slots % 64 != 0) {
        total_slots = ((total_slots / 64) + 1) * 64;
    }
    printf("Total Slots: %u\n", total_slots);
    printf("Threads: %u\n\n", config.num_threads);

    // Phase 1: Mutex-Protected Arena
    printf("Phase 1 (Mutex-Protected):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    Arena arena1(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads1;
    std::vector<uint32_t> thread_allocations1(config.num_threads, 0);

    auto start1 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads1.emplace_back(worker_phase1, &arena1, config.slot_size, &thread_allocations1[i]);
    }

    for (auto& t : threads1) {
        t.join();
    }

    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);

    printf("  Duration: %.3f ms\n", duration1.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("\n");

    // Phase 2: Lock-Free Arena
    printf("Phase 2 (Lock-Free):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaLockFree arena2(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads2;
    std::vector<uint32_t> thread_allocations2(config.num_threads, 0);

    auto start2 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads2.emplace_back(worker_phase2, &arena2, config.slot_size, &thread_allocations2[i]);
    }

    for (auto& t : threads2) {
        t.join();
    }

    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);
    uint64_t cas_retries = arena2.get_cas_retries();

    printf("  Duration: %.3f ms\n", duration2.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("  CAS retries: %llu\n", (unsigned long long)cas_retries);
    printf("\n");

    // Comparison
    printf("Comparison:\n");
    double phase1_ms = duration1.count() / 1000.0;
    double phase2_ms = duration2.count() / 1000.0;

    if (phase2_ms > 0) {
        double speedup = phase1_ms / phase2_ms;
        printf("  Lock-free speedup: %.3fx\n", speedup);
        if (speedup < 1.0) {
            printf("  (Lock-free was %.3fx slower)\n", 1.0 / speedup);
        }
    }

    printf("\n");
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    config.arena_capacity = 200 * 1024 * 1024; // 200 MB
    config.slot_size = 4 * 1024;               // 4 KB
    config.num_threads = 4;                    // Default to 4 threads

    // Allow user to specify number of threads via command line
    if (argc > 1) {
        config.num_threads = static_cast<uint32_t>(atoi(argv[1]));
        if (config.num_threads == 0) {
            printf("Invalid number of threads. Using default: 4\n");
            config.num_threads = 4;
        }
    }

    run_benchmark(config);

    return 0;
}
