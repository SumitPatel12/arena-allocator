/*
   Arena Allocator Benchmark

   This benchmark compares the performance of three arena allocator implementations:
   - Phase 1: Mutex-protected Arena with hint (allocation_hint in Bitmap)
   - Phase 2: Mutex-protected ArenaNoHint without hint (BitmapNoHint)
   - Phase 3: Lock-free ArenaLockFree (BitmapLockFree)

   Each phase spawns multiple threads that compete to allocate slots until the arena is full.

   Configuration:
   - Arena capacity: 200 MB
   - Slot size: 4 KB
   - Total slots: 51,200
   - Thread count: Configurable (default: 4)

   Metrics tracked:
   - Time to fill all slots in each phase
   - Number of slots allocated
   - CAS retry count (Phase 3 only)
   - Performance comparison across all three implementations

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
    printf("Phase 1 (Mutex-Protected with Hint): Running %d iterations...\n", NUM_ITERATIONS);
    
    double min1 = 1e9, max1 = 0, sum1 = 0;
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
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
        double time_ms = duration1.count() / 1000.0;
        
        sum1 += time_ms;
        if (time_ms < min1) min1 = time_ms;
        if (time_ms > max1) max1 = time_ms;
    }
    double avg1 = sum1 / NUM_ITERATIONS;
    printf("  Average: %.3f ms, Min: %.3f ms, Max: %.3f ms\n\n", avg1, min1, max1);

    // Phase 1b: Spin-Lock-Protected Arena with Hint
    printf("Phase 1b (Spin-Lock-Protected with Hint):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaSpinLock arena1s(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads1s;
    std::vector<uint32_t> thread_allocations1s(config.num_threads, 0);

    auto start1s = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads1s.emplace_back(worker_arena_spinlock, &arena1s, config.slot_size, &thread_allocations1s[i]);
    }

    for (auto& t : threads1s) {
        t.join();
    }

    auto end1s = std::chrono::high_resolution_clock::now();
    auto duration1s = std::chrono::duration_cast<std::chrono::microseconds>(end1s - start1s);

    printf("  Duration: %.3f ms\n", duration1s.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("\n");

    // Phase 2: Mutex-Protected Arena without Hint
    printf("Phase 2 (Mutex-Protected without Hint):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaNoHint arena2(config.arena_capacity, config.slot_size);
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

    printf("  Duration: %.3f ms\n", duration2.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("\n");

    // Phase 2b: Spin-Lock-Protected Arena without Hint
    printf("Phase 2b (Spin-Lock-Protected without Hint):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaNoHintSpinLock arena2s(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads2s;
    std::vector<uint32_t> thread_allocations2s(config.num_threads, 0);

    auto start2s = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads2s.emplace_back(worker_arena_nohint_spinlock, &arena2s, config.slot_size, &thread_allocations2s[i]);
    }

    for (auto& t : threads2s) {
        t.join();
    }

    auto end2s = std::chrono::high_resolution_clock::now();
    auto duration2s = std::chrono::duration_cast<std::chrono::microseconds>(end2s - start2s);

    printf("  Duration: %.3f ms\n", duration2s.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("\n");

    // Phase 3: Lock-Free Arena
    printf("Phase 3 (Lock-Free):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaLockFree arena3(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads3;
    std::vector<uint32_t> thread_allocations3(config.num_threads, 0);

    auto start3 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads3.emplace_back(worker_phase3, &arena3, config.slot_size, &thread_allocations3[i]);
    }

    for (auto& t : threads3) {
        t.join();
    }

    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 = std::chrono::duration_cast<std::chrono::microseconds>(end3 - start3);
    uint64_t cas_retries = arena3.get_cas_retries();

    printf("  Duration: %.3f ms\n", duration3.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("  CAS retries: %llu\n", (unsigned long long)cas_retries);
    printf("\n");

    // Phase 4: Lock-Free Arena with Hint
    printf("Phase 4 (Lock-Free with Hint):\n");
    global_allocated_count.store(0, std::memory_order_relaxed);

    ArenaLockFreeHint arena4(config.arena_capacity, config.slot_size);
    std::vector<std::thread> threads4;
    std::vector<uint32_t> thread_allocations4(config.num_threads, 0);

    auto start4 = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < config.num_threads; ++i) {
        threads4.emplace_back(worker_phase4, &arena4, config.slot_size, &thread_allocations4[i]);
    }

    for (auto& t : threads4) {
        t.join();
    }

    auto end4 = std::chrono::high_resolution_clock::now();
    auto duration4 = std::chrono::duration_cast<std::chrono::microseconds>(end4 - start4);
    uint64_t cas_retries_hint = arena4.get_cas_retries();

    printf("  Duration: %.3f ms\n", duration4.count() / 1000.0);
    printf("  Slots allocated: %u\n", global_allocated_count.load(std::memory_order_relaxed));
    printf("  CAS retries: %llu\n", (unsigned long long)cas_retries_hint);
    printf("\n");

    // Performance Summary Table
    printf("=== Performance Summary Table ===\n");
    double phase1_ms = duration1.count() / 1000.0;
    double phase1s_ms = duration1s.count() / 1000.0;
    double phase2_ms = duration2.count() / 1000.0;
    double phase2s_ms = duration2s.count() / 1000.0;
    double phase3_ms = duration3.count() / 1000.0;
    double phase4_ms = duration4.count() / 1000.0;

    // Find the best performer
    double best_time = phase1_ms;
    if (phase1s_ms < best_time) best_time = phase1s_ms;
    if (phase2_ms < best_time) best_time = phase2_ms;
    if (phase2s_ms < best_time) best_time = phase2s_ms;
    if (phase3_ms < best_time) best_time = phase3_ms;
    if (phase4_ms < best_time) best_time = phase4_ms;

    printf("\n");
    printf("┌────────┬─────────────────────────────────┬──────────────┬──────────────┬──────────────┐\n");
    printf("│ Phase  │ Implementation                  │ Time (ms)    │ vs Best      │ CAS Retries  │\n");
    printf("├────────┼─────────────────────────────────┼──────────────┼──────────────┼──────────────┤\n");
    
    printf("│   1    │ Mutex with Hint                 │ %9.3f    │     %7.2fx │            - │\n", 
           phase1_ms, phase1_ms / best_time);
    printf("│   1b   │ Spin-Lock with Hint             │ %9.3f    │     %7.2fx │            - │\n", 
           phase1s_ms, phase1s_ms / best_time);
    printf("│   2    │ Mutex without Hint              │ %9.3f    │     %7.2fx │            - │\n", 
           phase2_ms, phase2_ms / best_time);
    printf("│   2b   │ Spin-Lock without Hint          │ %9.3f    │     %7.2fx │            - │\n", 
           phase2s_ms, phase2s_ms / best_time);
    printf("│   3    │ Lock-Free without Hint          │ %9.3f    │     %7.2fx │ %12llu │\n", 
           phase3_ms, phase3_ms / best_time, (unsigned long long)cas_retries);
    printf("│   4    │ Lock-Free with Hint             │ %9.3f    │     %7.2fx │ %12llu │\n", 
           phase4_ms, phase4_ms / best_time, (unsigned long long)cas_retries_hint);
    
    printf("└────────┴─────────────────────────────────┴──────────────┴──────────────┴──────────────┘\n");

    printf("\n=== Direct Comparisons ===\n");
    printf("Mutex vs Spin-Lock (with Hint):     %.2fx %s\n", phase1s_ms / phase1_ms,
           phase1_ms < phase1s_ms ? "faster with mutex" : "faster with spin-lock");
    printf("Mutex vs Spin-Lock (without Hint):  %.2fx %s\n", phase2s_ms / phase2_ms,
           phase2_ms < phase2s_ms ? "faster with mutex" : "faster with spin-lock");
    printf("Hint vs No-Hint (Mutex):             %.2fx %s\n", phase2_ms / phase1_ms,
           phase1_ms < phase2_ms ? "faster with hint" : "faster without hint");
    printf("Hint vs No-Hint (Spin-Lock):         %.2fx %s\n", phase2s_ms / phase1s_ms,
           phase1s_ms < phase2s_ms ? "faster with hint" : "faster without hint");
    printf("Hint vs No-Hint (Lock-Free):         %.2fx %s (CAS: %llu vs %llu)\n", phase3_ms / phase4_ms,
           phase4_ms < phase3_ms ? "faster with hint" : "faster without hint", 
           (unsigned long long)cas_retries_hint, (unsigned long long)cas_retries);

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
