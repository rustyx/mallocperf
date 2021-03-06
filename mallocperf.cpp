// jemalloc C++ threaded performance test
// Author: Rustam Abdullaev
// Public Domain

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <random>
#include <thread>
#include <vector>
#include <stdio.h>

using namespace std::chrono;
using namespace std::chrono_literals;

extern "C" {
  // Basic jemalloc API
  extern const char* je_malloc_conf;
  void* je_malloc(size_t);
  void je_free(void*);
  void je_malloc_stats_print(void(*)(void*, const char*), void*, const char*);
}

#define MALLOC je_malloc
#define FREE   je_free

int main(int argc, char** argv) {
  if (argc > 1) {
    je_malloc_conf = argv[1];
  }
  static const int sizes[] = { 16, 32, 60, 96, 120, 144, 255, 400, 670, 99999, 128*1024 };
  static const int numSizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
  std::vector<std::thread> workers;
  static const int numThreads = 4, numAllocsMax = 30000, numIter1 = 5;
  printf("Starting %d threads x %d x %d iterations...\n", numThreads, numAllocsMax, numIter1);
  FREE(MALLOC(1)); // warmup allocator
  std::atomic<bool> warmup = true;
  std::atomic<int> rc = 0; // to ensure warmup not optimized away
  for (int i = 0; i < numThreads; i++) {
    workers.emplace_back([tid = i, &warmup, &rc]() {
      std::uniform_int_distribution<int> sizeDist(0, numSizes - 1);
      std::minstd_rand rnd(tid * 17), rnd0 = rnd;
      uint8_t* ptrs[numAllocsMax];
      int ptrsz[numAllocsMax];
      int sum = 0;
      FREE(MALLOC(1)); // warmup allocator
      while (warmup) {
        sum += sizeDist(rnd0);
      }
      rc = sum;
      for (int i = 0; i < numIter1; ++i) {
        const int numAllocs = numAllocsMax - sizeDist(rnd);
        for (int j = 0; j < numAllocs; ++j) {
          const int x = sizeDist(rnd);
          const int sz = sizes[x];
          ptrsz[j] = sz;
          ptrs[j] = (uint8_t*)MALLOC(sz);
          if (!ptrs[j]) {
            printf("Unable to allocate %d bytes in thread %d, iter %d, alloc %d. %d\n", sz, tid, i, j, x);
            exit(1);
          }
          for (int k = 0; k < sz; k++)
            ptrs[j][k] = tid + k;
        }
        for (int j = 0; j < numAllocs; ++j) {
          for (int k = 0, sz = ptrsz[j]; k < sz; k++)
            if (ptrs[j][k] != (uint8_t)(tid + k)) {
              printf("Memory error in thread %d, iter %d, alloc %d @ %d : %02X!=%02X\n", tid, i, j, k, ptrs[j][k], (uint8_t)(tid + k));
              exit(1);
            }
          FREE(ptrs[j]);
        }
      }
    });
  }
  std::this_thread::sleep_for(2s); // warmup CPU
  warmup = false;
  auto tm1 = high_resolution_clock::now();
  for (auto& t : workers) {
    t.join();
  }
  auto tm2 = high_resolution_clock::now();
  printf("\nDone. Run time: %zd ms\n", (tm2 - tm1) / 1ms);
  je_malloc_stats_print(NULL, NULL, "blam");
  return rc == -1;
}
