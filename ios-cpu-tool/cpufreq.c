//
//  cpufreq.c
//  ios-cpu-tool
//
//  Created by vvid on 13/10/2017.
//  Copyright © 2017 vvid. All rights reserved.
//

#define CALIB_REPEAT     1000000 //unrolled 20x internally
#define CALIB_UNROLL     20

#define WORKLOAD_WARMUP 1000000
#define WORKLOAD_REPEAT 50000000
#define ATTEMPTS 10
#define MAX_THREADS 8



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "TargetConditionals.h"
#if TARGET_OS_IPHONE && TARGET_IPHONE_SIMULATOR
  #include <x86intrin.h>
#elif TARGET_OS_IPHONE
    // define something for iphone  
#else
    #define TARGET_OS_OSX 1
    // define something for OSX
#endif

#include <mach/mach_time.h>
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <pthread.h>
#include <assert.h>

#define N (80 * 1000 * 100U)

extern void dummy_call(void);
extern void* calibration(void);
extern int cpu_workload(int count) __attribute__ ((noinline));
extern void calib_seq_add(uint64_t count);
extern void calib_seq_nop(uint64_t count);

static inline uint64_t hpctime()
{
  return mach_absolute_time();
}

static double timebase_double = 0.0;
static mach_timebase_info_data_t timebase = { 0 };

double hpctime_to_ns(uint64_t time)
{
  if (!timebase.denom)
  {
    mach_timebase_info(&timebase);
    printf("acquiring timebase %d:%d\n", timebase.numer, timebase.denom);
    timebase_double = (double)timebase.numer / timebase.denom;
  }
  return ((double)time * timebase_double);
}

double hpctime_to_s(uint64_t time)
{
  return hpctime_to_ns(time) / 1000000000.0;
}

#define countof(arr) (sizeof(arr) / sizeof(arr[0]))


uint64_t timescale = 1;
uint64_t min_overhead = ~0;

void warmup()
{
  cpu_workload(WORKLOAD_WARMUP);

  int i;
  for (i = 0; i < ATTEMPTS; i++)
  {
    uint64_t tb, te;
    tb = hpctime();
    dummy_call();
    te = hpctime();
  
    if (min_overhead > te - tb)
        min_overhead = te - tb;  
  }
};

double measure_freq()
{
    uint64_t tb, te;
    uint64_t min_time = ~0;

    int i;
    for (i = 0; i < ATTEMPTS; i++)
    {
        tb = hpctime();
        calib_seq_add(CALIB_REPEAT);
        te = hpctime();
        if (min_time > te - tb)
            min_time = te - tb;
    }
    min_time -= min_overhead;
    return CALIB_UNROLL * CALIB_REPEAT / hpctime_to_s(min_time);
}

uint32_t mem[256];

//do some stupid crunching int/fpu/mem
//int cpu_workload(int iterations) __attribute__ ((noinline));
int cpu_workload(int iterations)
{
    float fi = 1;
    uint32_t idx = mem[0];
    for (int i=0; i<iterations; i++)
    {
        for (int j=0; j<4; j++)
          idx = idx * mem[i + j & 255];
        fi *= (float)idx + 1.1 / (float)i + 10;
        mem[i & 255] = idx;
    };
    return (int)fi + mem[0];
};

double measure_workload()
{
    double t1 = measure_freq();
    cpu_workload(WORKLOAD_REPEAT);  //burn cycles
    return fmax(t1, measure_freq());
}

pthread_t measure_threads[MAX_THREADS];

double measure_thread_freq[MAX_THREADS];
double measure_thread_min_freq[MAX_THREADS];
double measure_thread_max_freq[MAX_THREADS];

int num_threads = 0;
int thread_stop_event = 0;

void * measure_thread(void *thread_id)
{
    long tid;
    tid = (long)thread_id;
    //printf("Hello World! It's me, thread #%ld!\n", tid);

    while (!thread_stop_event)
    {
        double freq = measure_workload();
        measure_thread_freq[tid] = freq;
        if (measure_thread_min_freq[tid] > freq)
            measure_thread_min_freq[tid] = freq;
        if (measure_thread_max_freq[tid] < freq)
            measure_thread_max_freq[tid] = freq;
    }
    pthread_exit(NULL);
}

void start_threads(int num_cores)
{
    assert(num_cores <= MAX_THREADS);
    num_threads = num_cores;
    thread_stop_event = 0;
    int res = 0;
    for (intptr_t i = 0; i < num_threads; i++)
    {
        measure_thread_freq[i] = 0.;
        measure_thread_min_freq[i] = 1E12;
        measure_thread_max_freq[i] = 0.;

        res |= pthread_create(&measure_threads[i], NULL, measure_thread, (void *)i);
        mach_port_t port = pthread_mach_thread_np(measure_threads[i]);
        thread_affinity_policy_data_t policy_data = { (int)(1 << i) };
        thread_policy_set(port, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy_data, 1);
        thread_resume(port);
    }
}

void stop_threads()
{
    thread_stop_event = 1;
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(measure_threads[i], NULL);
    }
}
double get_thread_freq(int core_id)
{
    return measure_thread_freq[core_id];
}
double get_thread_min_freq(int core_id)
{
    return measure_thread_min_freq[core_id];
}
double get_thread_max_freq(int core_id)
{
    return measure_thread_max_freq[core_id];
}


