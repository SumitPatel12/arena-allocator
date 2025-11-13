/*
   Arena Allocator Benchmark

   This benchmark compares the performance of six arena allocator implementations:
   - Phase 1: Mutex-protected Arena with hint (allocation_hint in Bitmap)
   - Phase 1b: Spin-lock-protected ArenaSpinLock with hint
   - Phase 2: Mutex-protected ArenaNoHint without hint (BitmapNoHint)
   - Phase 2b: Spin-lock-protected ArenaNoHintSpinLock without hint
   - Phase 3: Lock-free ArenaLockFree (BitmapLockFree)
   - Phase 4: Lock-free ArenaLockFreeHint with hint

   Each phase spawns multiple threads that compete to allocate slots until the arena is full.
   Each phase is run 100 times to get average, min, and max timings.

   Configuration:
   - Arena capacity: 200 MB
   - Slot size: 4 KB
   - Total slots: 51,200
   - Thread count: Configurable (default: 4)
   - Iterations: 100 per phase

   Metrics tracked:
   - Average, min, max time to fill all slots in each phase
   - Number of slots allocated
   - CAS retry count (Phases 3 & 4 only)
   - Performance comparison across all implementations

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

// Worker function for Phase 2 (mutex-protected ArenaNoHint)
void worker_phase2(ArenaNoHint* arena, size_t slot_size, uint32_t* thread_allocations) {
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

// Worker function for Phase 3 (lock-free ArenaLockFree)
void worker_phase3(ArenaLockFree* arena, size_t slot_size, uint32_t* thread_allocations) {
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

// Worker function for Phase 4 (lock-free ArenaLockFreeHint)
void worker_phase4(ArenaLockFreeHint* arena, size_t slot_size, uint32_t* thread_allocations) {
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

// Worker function for ArenaSpinLock
void worker_arena_spinlock(ArenaSpinLock* arena, size_t slot_size, uint32_t* thread_allocations) {
    uint32_t local_count = 0;
    while (true) {
        char* slot = arena->allocate(slot_size);
        if (slot == NULL) {
            break;
        }
        local_count++;
        global_allocated_count.fetch_add(1, std::memory_order_relaxed);
    }
    *thread_allocations = local_count;
}

// Worker function for ArenaNoHintSpinLock
void worker_arena_nohint_spinlock(ArenaNoHintSpinLock* arena, size_t slot_size, uint32_t* thread_allocations) {
    uint32_t local_count = 0;
    while (true) {
        char* slot = arena->allocate(slot_size);
        if (slot == NULL) {
            break;
        }
        local_count++;
        global_allocated_count.fetch_add(1, std::memory_order_relaxed);
    }
    *thread_allocations = local_count;
}

void run_benchmark(const BenchmarkConfig& config) {
    const int NUM_ITERATIONS = 100;
    
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
    printf("Threads: %u\n", config.num_threads);
    printf("Iterations per phase: %d\n\n", NUM_ITERATIONS);

    // Phase 1: Mutex-Protected Arena with Hint
    printf("Phase 1 (Mutex with Hint): Running...");
    fflush(stdout);
    double min1 = 1e9, max1 = 0, sum1 = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        Arena arena1(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads1;
        std::vector<uint32_t> thread_allocations1(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads1.emplace_back(worker_phase1, &arena1, config.slot_size, &thread_allocations1[i]);
        }
        for (auto& t : threads1) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum1 += time_ms;
        if (time_ms < min1) min1 = time_ms;
        if (time_ms > max1) max1 = time_ms;
    }
    double avg1 = sum1 / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms\n\n", avg1, min1, max1);

    // Phase 1b: Spin-Lock with Hint
    printf("Phase 1b (Spin-Lock with Hint): Running...");
    fflush(stdout);
    double min1b = 1e9, max1b = 0, sum1b = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        ArenaSpinLock arena1s(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads1s;
        std::vector<uint32_t> thread_allocations1s(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads1s.emplace_back(worker_arena_spinlock, &arena1s, config.slot_size, &thread_allocations1s[i]);
        }
        for (auto& t : threads1s) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum1b += time_ms;
        if (time_ms < min1b) min1b = time_ms;
        if (time_ms > max1b) max1b = time_ms;
    }
    double avg1b = sum1b / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms\n\n", avg1b, min1b, max1b);

    // Phase 2: Mutex without Hint
    printf("Phase 2 (Mutex without Hint): Running...");
    fflush(stdout);
    double min2 = 1e9, max2 = 0, sum2 = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        ArenaNoHint arena2(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads2;
        std::vector<uint32_t> thread_allocations2(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads2.emplace_back(worker_phase2, &arena2, config.slot_size, &thread_allocations2[i]);
        }
        for (auto& t : threads2) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum2 += time_ms;
        if (time_ms < min2) min2 = time_ms;
        if (time_ms > max2) max2 = time_ms;
    }
    double avg2 = sum2 / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms\n\n", avg2, min2, max2);

    // Phase 2b: Spin-Lock without Hint
    printf("Phase 2b (Spin-Lock without Hint): Running...");
    fflush(stdout);
    double min2b = 1e9, max2b = 0, sum2b = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        ArenaNoHintSpinLock arena2s(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads2s;
        std::vector<uint32_t> thread_allocations2s(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads2s.emplace_back(worker_arena_nohint_spinlock, &arena2s, config.slot_size, &thread_allocations2s[i]);
        }
        for (auto& t : threads2s) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum2b += time_ms;
        if (time_ms < min2b) min2b = time_ms;
        if (time_ms > max2b) max2b = time_ms;
    }
    double avg2b = sum2b / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms\n\n", avg2b, min2b, max2b);

    // Phase 3: Lock-Free without Hint
    printf("Phase 3 (Lock-Free without Hint): Running...");
    fflush(stdout);
    double min3 = 1e9, max3 = 0, sum3 = 0;
    uint64_t total_cas_retries3 = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        ArenaLockFree arena3(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads3;
        std::vector<uint32_t> thread_allocations3(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads3.emplace_back(worker_phase3, &arena3, config.slot_size, &thread_allocations3[i]);
        }
        for (auto& t : threads3) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum3 += time_ms;
        if (time_ms < min3) min3 = time_ms;
        if (time_ms > max3) max3 = time_ms;
        total_cas_retries3 += arena3.get_cas_retries();
    }
    double avg3 = sum3 / NUM_ITERATIONS;
    uint64_t avg_cas_retries3 = total_cas_retries3 / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms, CAS Retries: %llu\n\n", 
           avg3, min3, max3, (unsigned long long)avg_cas_retries3);

    // Phase 4: Lock-Free with Hint
    printf("Phase 4 (Lock-Free with Hint): Running...");
    fflush(stdout);
    double min4 = 1e9, max4 = 0, sum4 = 0;
    uint64_t total_cas_retries4 = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        global_allocated_count.store(0, std::memory_order_relaxed);
        ArenaLockFreeHint arena4(config.arena_capacity, config.slot_size);
        std::vector<std::thread> threads4;
        std::vector<uint32_t> thread_allocations4(config.num_threads, 0);

        auto start = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < config.num_threads; ++i) {
            threads4.emplace_back(worker_phase4, &arena4, config.slot_size, &thread_allocations4[i]);
        }
        for (auto& t : threads4) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        sum4 += time_ms;
        if (time_ms < min4) min4 = time_ms;
        if (time_ms > max4) max4 = time_ms;
        total_cas_retries4 += arena4.get_cas_retries();
    }
    double avg4 = sum4 / NUM_ITERATIONS;
    uint64_t avg_cas_retries4 = total_cas_retries4 / NUM_ITERATIONS;
    printf(" Done\n");
    printf("  Avg: %.3f ms, Min: %.3f ms, Max: %.3f ms, CAS Retries: %llu\n\n", 
           avg4, min4, max4, (unsigned long long)avg_cas_retries4);

    // Performance Summary Table
    printf("=== Performance Summary Table (Average Times) ===\n");

    // Find the best performer
    double best_time = avg1;
    if (avg1b < best_time) best_time = avg1b;
    if (avg2 < best_time) best_time = avg2;
    if (avg2b < best_time) best_time = avg2b;
    if (avg3 < best_time) best_time = avg3;
    if (avg4 < best_time) best_time = avg4;

    printf("\n");
    printf("┌────────┬─────────────────────────────────┬──────────────┬──────────────┬──────────────┐\n");
    printf("│ Phase  │ Implementation                  │ Avg (ms)     │ vs Best      │ CAS Retries  │\n");
    printf("├────────┼─────────────────────────────────┼──────────────┼──────────────┼──────────────┤\n");
    
    printf("│   1    │ Mutex with Hint                 │ %9.3f    │     %7.2fx │            - │\n", 
           avg1, avg1 / best_time);
    printf("│   1b   │ Spin-Lock with Hint             │ %9.3f    │     %7.2fx │            - │\n", 
           avg1b, avg1b / best_time);
    printf("│   2    │ Mutex without Hint              │ %9.3f    │     %7.2fx │            - │\n", 
           avg2, avg2 / best_time);
    printf("│   2b   │ Spin-Lock without Hint          │ %9.3f    │     %7.2fx │            - │\n", 
           avg2b, avg2b / best_time);
    printf("│   3    │ Lock-Free without Hint          │ %9.3f    │     %7.2fx │ %12llu │\n", 
           avg3, avg3 / best_time, (unsigned long long)avg_cas_retries3);
    printf("│   4    │ Lock-Free with Hint             │ %9.3f    │     %7.2fx │ %12llu │\n", 
           avg4, avg4 / best_time, (unsigned long long)avg_cas_retries4);
    
    printf("└────────┴─────────────────────────────────┴──────────────┴──────────────┴──────────────┘\n");

    printf("\n=== Min/Max Times ===\n");
    printf("Phase 1 (Mutex with Hint):         Min: %.3f ms, Max: %.3f ms\n", min1, max1);
    printf("Phase 1b (Spin-Lock with Hint):    Min: %.3f ms, Max: %.3f ms\n", min1b, max1b);
    printf("Phase 2 (Mutex without Hint):      Min: %.3f ms, Max: %.3f ms\n", min2, max2);
    printf("Phase 2b (Spin-Lock without Hint): Min: %.3f ms, Max: %.3f ms\n", min2b, max2b);
    printf("Phase 3 (Lock-Free without Hint):  Min: %.3f ms, Max: %.3f ms\n", min3, max3);
    printf("Phase 4 (Lock-Free with Hint):     Min: %.3f ms, Max: %.3f ms\n", min4, max4);

    printf("\n=== Direct Comparisons (Average Times) ===\n");
    printf("Mutex vs Spin-Lock (with Hint):     %.2fx %s\n", avg1b / avg1,
           avg1 < avg1b ? "faster with mutex" : "faster with spin-lock");
    printf("Mutex vs Spin-Lock (without Hint):  %.2fx %s\n", avg2b / avg2,
           avg2 < avg2b ? "faster with mutex" : "faster with spin-lock");
    printf("Hint vs No-Hint (Mutex):             %.2fx %s\n", avg2 / avg1,
           avg1 < avg2 ? "faster with hint" : "faster without hint");
    printf("Hint vs No-Hint (Spin-Lock):         %.2fx %s\n", avg2b / avg1b,
           avg1b < avg2b ? "faster with hint" : "faster without hint");
    printf("Hint vs No-Hint (Lock-Free):         %.2fx %s (CAS: %llu vs %llu)\n", avg3 / avg4,
           avg4 < avg3 ? "faster with hint" : "faster without hint", 
           (unsigned long long)avg_cas_retries4, (unsigned long long)avg_cas_retries3);

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
