//
//  cpufreq.c
//  ios-cpu-tool
//
//  Created by vvid on 13/10/2017.
//  Copyright © 2017 vvid. All rights reserved.
//

//iphone SE/8 timebase 41,6ns

#define CYCLES_PER_MS    2000000  //base scale

#define CALIB_UNROLL     20 //(unrolled 20x internally)
#define CALIB_REPEAT     (CYCLES_PER_MS/CALIB_UNROLL/2)  //1 msec
#define CALIB_ATTEMPTS   10

#define INITIAL_WARMUP   2000000
#define WORKLOAD_REPEAT  50000 //*100

#define MAX_THREADS      8
#define NUM_FREQ_SAMPLES 1024
#define NUM_FREQ_POINTS  1024

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

//asm prototypes
extern void dummy_call(void);
extern void* calibration(void);
extern int  cpu_workload(int count) __attribute__ ((noinline));
extern void calib_seq_add(uint64_t count);
extern void calib_seq_nop(uint64_t count);
extern void calib_signature(uint64_t count);
extern void sync_threads(uint64_t thread_id, uint64_t num_threads, int *stop_event,  int *lock_ptr);
//

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
uint64_t start_of_time = 0;

void warmup()
{
  cpu_workload(INITIAL_WARMUP);

  int i;
  for (i = 0; i < CALIB_ATTEMPTS; i++)
  {
    uint64_t tb, te;
    tb = hpctime();
    dummy_call();
    te = hpctime();

    //FIXME: access from multiple threads
    if (min_overhead > te - tb)
        min_overhead = te - tb;
  }
};

double measure_freq()
{
    uint64_t tb, te;
    uint64_t min_time = ~0;

    for (int i = 0; i < CALIB_ATTEMPTS; i++)
    {
        tb = hpctime();
        calib_seq_add(CALIB_UNROLL * CALIB_REPEAT);
        te = hpctime();
        if (min_time > te - tb)
            min_time = te - tb;
    }
    if (min_time > min_overhead)
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
        for (int j=0; j<64; j++)
          idx = idx * mem[i + j & 255];
        fi *= (float)idx + 1.1 / (float)i + 10;
        mem[i & 255] = idx;
    };
    return (int)fi + mem[0];
};

struct FreqPoint
{
    int freq;
    int signature;
    int count;
};

struct FreqSample
{
    //in MHz
    int min_freq;
    int max_freq;
    int signature; //cycles per iteration
    int index;
    uint64_t timestamp_b;
    uint64_t timestamp_e;
};

pthread_t measure_threads[MAX_THREADS];

double measure_thread_freq[MAX_THREADS];
double measure_thread_min_freq[MAX_THREADS];
double measure_thread_max_freq[MAX_THREADS];

struct FreqSample filtered_samples[MAX_THREADS][NUM_FREQ_SAMPLES];
struct FreqPoint frequency_points[NUM_FREQ_POINTS];

struct FreqSample *thread_samples;
struct FreqSample *thread_samples_aligned;
int thread_sample_index[MAX_THREADS];
int num_thread_samples = 1000;
int num_thread_samples_aligned = 1000;

int num_freq_points = 0;
int num_threads = 0;
int thread_stop_event = 0;

int thread_lock_var = 0;

int time_from_start_ms(uint64_t time)
{
    return (int)(hpctime_to_ns(time - start_of_time) / 1000000.0);
}

int freq_mhz(double freq)
{
    return (int)((freq + 499999) / 1000000);
}

int min(int a, int b)
{
    return a > b ? b : a;
}
int max(int a, int b)
{
    return a > b ? a : b;
}

struct FreqSample* get_sample(int thread_id, int sample_index)
{
  return &thread_samples[thread_id * num_thread_samples + sample_index];
}
struct FreqSample* get_sample_aligned(int thread_id, int sample_index)
{
    return &thread_samples_aligned[thread_id * num_thread_samples + sample_index];
}

int intersect(uint64_t al, uint64_t ar, uint64_t bl, uint64_t br)
{
  return ((al < bl && ar > bl) ||
          (al > bl && ar < br) ||
          (al < bl && ar > br) ||
          (bl < al && br > ar));
}

void dump_freq_points()
{
    for (int i = 0; i<num_freq_points; i++)
    {
        printf("fp %d f:%d s:%d c:%d\n", i,
               frequency_points[i].freq,
               frequency_points[i].signature,
               frequency_points[i].count);
    }
}

void load_debug_freq_points()
{
    int freqs[] = {};
    int signs[] = {};
    int counts[] = {};

    num_freq_points = countof(freqs);
    for (int i = 0; i<num_freq_points; i++)
    {
        frequency_points[i].freq = freqs[i];
        frequency_points[i].signature = signs[i];
        frequency_points[i].count = counts[i];
    }
}

void cleanup_freq_points()
{
    int max_counter = 0;
    for (int i = 0; i<num_freq_points; i++)
        max_counter = max(max_counter, frequency_points[i].count);

    for (int i = 0; i<num_freq_points; i++)
    {
        max_counter = max(max_counter, frequency_points[i].count);
    }
}

int freq_comp(const void *a, const void *b)
{
    struct FreqSample *as = (struct FreqSample*)a;
    struct FreqSample *bs = (struct FreqSample*)b;
    if (as->max_freq > bs->max_freq) return  1;
    if (as->max_freq < bs->max_freq) return -1;
    return 0;
}

int freq_point_comp(const void *a, const void *b)
{
    struct FreqPoint *as = (struct FreqPoint*)a;
    struct FreqPoint *bs = (struct FreqPoint*)b;
    if (as->signature < bs->signature && as->freq > bs->freq) return -1;
    if (as->signature > bs->signature && as->freq < bs->freq) return  1;
    return 0;
}

// align samples, strip first/last
//
void analyze_freq()
{
    //this is not yet used
    thread_samples_aligned = (struct FreqSample*)malloc(sizeof(struct FreqSample) * num_thread_samples * num_threads);

    int min_length = 1000000;
    for (int i = 0; i<num_threads; i++)
        min_length = min(min_length, thread_sample_index[i]);

    num_freq_points = 0;

    int thread_freq[MAX_THREADS];
    int thread_sig[MAX_THREADS];
    int thread_idx[MAX_THREADS];
    uint64_t thread_dist[MAX_THREADS];
    
    int sd = 0;
    for (int s = 0; s<min_length && sd < NUM_FREQ_SAMPLES; s++)
    {
        uint64_t tb0 = get_sample(0, s)->timestamp_b;
        uint64_t te0 = get_sample(0, s)->timestamp_e;
        get_sample(0, s)->index = s;

        int valid = 1;
        for (int t = 0; t<num_threads; t++)
        {
          struct FreqSample *smp = get_sample(t, s);

          thread_freq[t] = smp->max_freq;
          thread_sig[t]  = smp->signature;
          thread_dist[t] = smp->timestamp_e - smp->timestamp_b;
          thread_idx[t]  = t;

          if (t > 0)
          {
            uint64_t tb = get_sample(t, s)->timestamp_b;
            uint64_t te = get_sample(t, s)->timestamp_e;
            if (!intersect(tb0, te0, tb, te) || smp->signature == 0)
              valid = 0;
          }
        }

        if (valid)
        {
            sd++;

            printf("pos %d: ", s);

            //add freq point
            for (int t = 0; t<num_threads; t++)
            {
              struct FreqSample *smp = get_sample(t, s);
              printf("%d/%d(+%d) ", smp->max_freq, smp->signature, time_from_start_ms(smp->timestamp_b));

                if (sd == 1)
                {
                    measure_thread_freq[t]     = 1000000.0 * smp->max_freq;
                    measure_thread_min_freq[t] = 1000000.0 * smp->signature;
                    measure_thread_max_freq[t] = 1000000.0 * time_from_start_ms(smp->timestamp_b);
                }

                struct FreqSample *fs = get_sample(t, sd-1);
 //               printf("+fp tb %lld te %lld %lld msec\n", fs->timestamp_b, fs->timestamp_e,
 //                                                         (fs->timestamp_e - fs->timestamp_b) / 1000000);

                int found = 0;
                for (int p = 0; p < num_freq_points; p++)
                {
                    if (frequency_points[p].freq      == thread_freq[t] &&
                        frequency_points[p].signature == thread_sig[t])
                    {
                        frequency_points[p].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && num_freq_points < NUM_FREQ_POINTS)
                {
                    frequency_points[num_freq_points].freq     = thread_freq[t];
                    frequency_points[num_freq_points].signature = thread_sig[t];
                    frequency_points[num_freq_points].count = 1;
                    num_freq_points++;
                }
            }
            printf("\n");
        }
    }

    load_debug_freq_points();

    dump_freq_points();

    qsort(frequency_points, num_freq_points, sizeof(struct FreqPoint), freq_point_comp);

    printf("after sort \n");

    cleanup_freq_points();
    dump_freq_points();

    struct FreqSample tmp_samples[MAX_THREADS];

    //filter
    for (int s = 0; s<sd; s++)
    {
        //sort by frequency
        for (int t = 0; t<num_threads; t++)
        {
            tmp_samples[t] = *get_sample_aligned(t, s);
        }
        qsort(tmp_samples, num_threads, sizeof(struct FreqSample), freq_comp);
    }

    free(thread_samples_aligned);
}

double measure_signature(double freq)
{
    uint64_t tb, te;
    uint64_t min_time = ~0;

    for (int i = 0; i < 5; i++)
    {
        tb = hpctime();
        calib_signature(CALIB_REPEAT);
        te = hpctime();
        if (min_time > te - tb)
            min_time = te - tb;
    }
    min_time -= min_overhead;

    //freq (cycles/sec) * time_s_per_iter (sec/repeat) = cycles/repeat
    double time_per_iteration = 1000 * hpctime_to_s(min_time) / CALIB_REPEAT;
    return time_per_iteration * freq;
}

void measure_workload(int thread_id, int sample_index)
{
    struct FreqSample *smp = get_sample(thread_id, sample_index);

    cpu_workload(WORKLOAD_REPEAT);  //burn cycles

    sync_threads(thread_id, num_threads, &thread_stop_event, &thread_lock_var);

    smp->timestamp_b = hpctime();
    double f1 = measure_freq();
    double s1 = measure_signature(f1);
    double f2 = measure_freq();
    double s2 = measure_signature(f2);
    smp->timestamp_e = hpctime();

    int if1 = freq_mhz(f1);
    int if2 = freq_mhz(f2);
    int is1 = (int)s1;
    int is2 = (int)s2;

    smp->index = -1;
    //cross check: accept signature if deltas in 20/10% range
    if (abs(is2-is1) * 10 < is1 && abs(if2-if1) * 20 < if1)
        smp->signature = min(is1, is2);
    else
        smp->signature = 0;//-is1;

    smp->max_freq = max(if1, if2);
    smp->min_freq = min(if1, if2);
}

void * measure_thread(void *thread_id)
{
    long tid = (long)thread_id;

    warmup();

    while (!thread_stop_event)
    {
        measure_workload((int)tid, thread_sample_index[tid]);
        if (thread_sample_index[tid] < num_thread_samples)
            thread_sample_index[tid]++;
        else
            thread_stop_event = 1; //end of buffer reached, abort
    }

    pthread_exit(NULL);
}

void start_threads(int num_cores, int steps)
{
    assert(num_cores <= MAX_THREADS);
    num_threads = num_cores;
    num_thread_samples = steps;
    thread_stop_event = 0;
    thread_lock_var = num_cores;

    thread_samples = (struct FreqSample*)malloc(sizeof(struct FreqSample) * num_thread_samples * num_cores);

    memset(thread_samples, 0, sizeof(struct FreqSample) * num_thread_samples * num_cores);

    mach_port_t ports[MAX_THREADS];

    int res = 0;
    for (intptr_t i = 0; i < num_threads; i++)
    {
        measure_thread_freq[i] = 0.;
        measure_thread_min_freq[i] = 1E12;
        measure_thread_max_freq[i] = 0.;
        thread_sample_index[i] = 0;

        res |= pthread_create_suspended_np(&measure_threads[i], NULL, measure_thread, (void *)i);
        ports[i] = pthread_mach_thread_np(measure_threads[i]);
        thread_affinity_policy_data_t policy_data = { (int)i };
        thread_policy_set(ports[i], THREAD_AFFINITY_POLICY, (thread_policy_t)&policy_data, THREAD_AFFINITY_POLICY_COUNT);
    }

    start_of_time = hpctime();

    for (intptr_t i = 0; i < num_threads; i++)
    {
        thread_resume(ports[i]);
    }
}

void stop_threads()
{
    thread_stop_event = 1;
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(measure_threads[i], NULL);
    }
    analyze_freq();
    free(thread_samples);
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

