//
//  cpufreq.c
//  ios-cpu-tool
//
//  Created by vvid on 13/10/2017.
//  Copyright © 2017 vvid. All rights reserved.
//

#define CALIB_REPEAT     1000000 //unrolled 20x internally
#define WORKLOAD_REPEAT 20000000
#define ATTEMPTS 10
#define MAX_THREADS 6



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
  uint64_t tb, te;
  tb = hpctime();
  calib_seq_add(CALIB_REPEAT);  //warmup
  te = hpctime();

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
  return 20.0 * CALIB_REPEAT / hpctime_to_s(min_time);
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


