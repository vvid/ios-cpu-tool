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

#define countof(arr) (sizeof(arr) / sizeof(arr[0]))

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

double hpctime_to_ms(uint64_t time)
{
  return hpctime_to_ns(time) / 1000000.0;
}
double hpctime_to_s(uint64_t time)
{
  return hpctime_to_ns(time) / 1000000000.0;
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

struct DataPoint
{
    int value;
    int distance;
    int index;
    int cluster;
};

struct Cluster
{
  int ref_value;
  int min_value;
  int max_value;
  int count;
  int done;
  int min_idx;
  int max_idx;
};

struct FreqPoint
{
    int freq;
    int signature;
    int count;
    uint64_t time;
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

struct DataPoint data_pnts_in[NUM_FREQ_POINTS];
struct DataPoint data_pnts[NUM_FREQ_POINTS];
struct Cluster clusters[NUM_FREQ_POINTS];

int thread_sample_index[MAX_THREADS];

struct FreqSample *thread_samples;
struct FreqSample *thread_samples_aligned;
int num_thread_samples = 1000;
int num_thread_samples_aligned = 1000;

int num_freq_points = 0;
int num_threads = 0;
int thread_stop_event = 0;
int thread_lock_var = 0;

uint64_t timescale = 1;
uint64_t min_overhead = ~0;
uint64_t start_of_time = 0;

//Freq measurement state

struct CpuClusterInfo
{
  int min_freq;
  int max_freq;
  int signature;
};

struct CpuCoreInfo
{
  int max_freq;

  //caches


};

struct CpuClusterInfo cpu_clusters[MAX_THREADS];
struct CpuCoreInfo    cpu_cores[MAX_THREADS];

//------------------------------------------
//


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

//delta between high-low is less than (100/tolerance)% of high
int check_tolerance(int high, int low, int tolerance)
{
  return (abs(high - low) * tolerance < high) ? 1 : 0;
}

void dump_freq_points()
{
    for (int i = 0; i<num_freq_points; i++)
    {
        printf("fp %d f:%d s:%d t:%f ms c:%d\n", i,
               frequency_points[i].freq,
               frequency_points[i].signature,
               hpctime_to_ms(frequency_points[i].time),
               frequency_points[i].count);
    }
}

void dump_data_points(struct DataPoint *dp, int count)
{
    for (int i = 0; i<count; i++)
    {
        printf("dp %d v:%d d:%d i:%d c:%d\n", i,
               dp[i].value,
               dp[i].distance,
               dp[i].index,
               dp[i].cluster);
    }
}

void dump_clusters(struct Cluster *clusters, int count)
{
    for (int i = 0; i<count; i++)
    {
        if (clusters[i].count == 0)
          continue;
        printf("cluster %d f:%d (%d-%d) cnt:%d <i:%d i:%d>\n", i,
               clusters[i].ref_value,
               clusters[i].min_value,
               clusters[i].max_value,
               clusters[i].count,
               clusters[i].min_idx,
               clusters[i].max_idx);
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

int fpoint_sig_comp(const void *a, const void *b)
{
    struct FreqPoint *as = (struct FreqPoint*)a;
    struct FreqPoint *bs = (struct FreqPoint*)b;
    if (as->signature < bs->signature) return -1;
    if (as->signature > bs->signature) return  1;
    return 0;
}

int dpoint_up_comp(const void *a, const void *b)
{
    struct DataPoint *as = (struct DataPoint*)a;
    struct DataPoint *bs = (struct DataPoint*)b;
    if (as->value < bs->value) return -1;
    if (as->value > bs->value) return  1;
    return 0;
}

int freq_valid_samples_list[NUM_FREQ_SAMPLES];
int freq_valid_samples_list_count = 0;

void calc_distance(struct DataPoint *dp, int num_points)
{
    dp[0].distance = abs(dp[1].value - dp[0].value);
    for (int i=1; i<num_points-1; i++)
    {
        dp[i].distance = max(abs(dp[i-1].value - dp[i].value),
                             abs(dp[i+1].value - dp[i].value));
    }
    dp[num_points-1].distance = abs(dp[num_points-2].value - dp[num_points-1].value);
}

#define SIG_CLUSTER_MERGE_TOLERANCE (100/5)

int clusterize(struct DataPoint *dp, int num_points)
{
    for (int i=0; i<num_freq_points; i++)
    {
        dp[i].distance = 0;
        dp[i].index = i;
        dp[i].cluster = -1;
    }

    qsort(dp, num_points, sizeof(struct DataPoint), dpoint_up_comp);

    calc_distance(dp, num_points);

    //create clusters

    int num_clusters = 0;
    for (int p=0; p<num_points; p++)
    {
        int found = 0;
        for (int c=0; c<num_clusters; c++)
        {
            if (dp[p].value == clusters[c].max_value) // &&
 //               dp[p].distance == 0)
            {
              found = 1;
              clusters[c].count++;
              clusters[c].min_idx = min(p, clusters[c].min_idx);
              clusters[c].max_idx = max(p, clusters[c].max_idx);
              dp[p].cluster = c;
            }
        }

        if (!found) // && dp[p].distance == 0)
        {
            clusters[num_clusters].count = 1;
            clusters[num_clusters].ref_value = dp[p].value;
            clusters[num_clusters].min_value = dp[p].value;
            clusters[num_clusters].max_value = dp[p].value;
            clusters[num_clusters].min_idx = p;
            clusters[num_clusters].max_idx = p;
            clusters[num_clusters].done = 0;
            num_clusters++;
        }
    }

    printf("clusterize: data points\n");
    dump_data_points(dp, num_points);
    
    printf("clusterize: create clusters\n");
    dump_clusters(clusters, num_clusters);

    //merge clusters
    while (1)
    {
      //find max
      int maxcnt = 0;
      int to = -1;
      for (int c=0; c<num_clusters; c++)
      {
        if (maxcnt < clusters[c].count && clusters[c].done == 0)
        {
          maxcnt = clusters[c].count;
          to = c;
        }
      }

      if (to < 0)
        break;

      int merged = 0;
      for (int c=0; c<num_clusters; c++)
      {
        if (c != to &&
            clusters[c].count > 0 &&
            clusters[c].done == 0 &&
            check_tolerance(clusters[to].ref_value, clusters[c].ref_value, SIG_CLUSTER_MERGE_TOLERANCE))
        {
          clusters[to].count += clusters[c].count;
          clusters[to].min_value = min(clusters[to].min_value, clusters[c].min_value); 
          clusters[to].max_value = max(clusters[to].max_value, clusters[c].max_value); 
          clusters[to].min_idx   = min(clusters[to].min_idx,   clusters[c].min_idx); 
          clusters[to].max_idx   = max(clusters[to].max_idx,   clusters[c].max_idx); 
          clusters[c].count = 0;
          merged = 1;
        }
      }

      clusters[to].done = 1;

      if (merged == 0)
        break;
    };

    printf("clusterize: cluster merge\n");
    dump_clusters(clusters, num_clusters);
    printf("clusterize: finished\n");

    return num_clusters;
}

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
    uint64_t thread_time[MAX_THREADS];
    
    freq_valid_samples_list_count = 0;
    for (int s = 0; s<min_length && freq_valid_samples_list_count < NUM_FREQ_SAMPLES; s++)
    {
        uint64_t tb0 = 0;
        uint64_t te0 = 0;
        int valid = 1;
        for (int t = 0; t<num_threads; t++)
        {
            struct FreqSample *smp = get_sample(t, s);
            if (smp->signature == 0)
                valid = 0;

            uint64_t tb = smp->timestamp_b;
            uint64_t te = smp->timestamp_e;
            if (t == 0)
            {
                tb0 = tb;
                te0 = te;
            }
            else
            {
              if (!intersect(tb0, te0, tb, te))
                valid = 0;
            }
        }

        if (valid)
        {
            freq_valid_samples_list[freq_valid_samples_list_count++] = s;

            printf("pos %d: ", s);

            //add freq point
            for (int t = 0; t<num_threads; t++)
            {
                struct FreqSample *smp = get_sample(t, s);
                printf("%d/%d(+%d:%f) ", smp->max_freq, smp->signature, time_from_start_ms(smp->timestamp_b), hpctime_to_ms(thread_time[t]));

		uint64_t tb = smp->timestamp_b;
		uint64_t te = smp->timestamp_e;
		thread_freq[t] = smp->max_freq;
		thread_sig[t]  = smp->signature;
		thread_time[t] = te - tb;
		thread_idx[t]  = t;

                if (freq_valid_samples_list_count == 1)
                {
                    //export first value for gui
                    measure_thread_freq[t]     = 1000000.0 * smp->max_freq;
                    measure_thread_min_freq[t] = 1000000.0 * smp->signature;
                    measure_thread_max_freq[t] = 1000000.0 * time_from_start_ms(smp->timestamp_b);
                }

                int found = 0;
                if (0) for (int p = 0; p < num_freq_points; p++)
                {
                    if (frequency_points[p].freq      == thread_freq[t] &&
                        frequency_points[p].signature == thread_sig[t])
                    {
                        frequency_points[p].time = (frequency_points[p].time + thread_time[t]) / 2;
                        frequency_points[p].count++;
                        found = 1;
                        break;
                    }
                };

                if (!found && num_freq_points < NUM_FREQ_POINTS)
                {
                    frequency_points[num_freq_points].signature = thread_sig[t];
                    frequency_points[num_freq_points].freq     = thread_freq[t];
                    frequency_points[num_freq_points].time     = thread_time[t];
                    frequency_points[num_freq_points].count = 1;
                    num_freq_points++;
                }
            }
            printf("\n");
        }
    }

    dump_freq_points();

    for (int i=0; i<num_freq_points; i++)
        data_pnts_in[i].value = frequency_points[i].signature;

    int num_clusters = clusterize(data_pnts_in, num_freq_points);
    int clusters_out = 0;
    for (int c=0; c<num_clusters; c++)
    {
      if (clusters[c].count > 0 /* && clusters[c].done*/ )
      {
        cpu_clusters[clusters_out].signature = clusters[c].ref_value;
        printf("Cluster: %d sig=%d\n", clusters_out, cpu_clusters[clusters_out].signature); 
        //TODO: collect frequencies
        clusters_out++;
      }
    }


    //qsort(frequency_points, num_freq_points, sizeof(struct FreqPoint), fpoint_sig_comp);

    printf("modified freqpoints\n");
    cleanup_freq_points();
    dump_freq_points();

    struct FreqSample tmp_samples[MAX_THREADS];

    //filter
    for (int s = 0; s<freq_valid_samples_list_count; s++)
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

void warmup()
{
    cpu_workload(INITIAL_WARMUP);

    for (int i = 0; i < CALIB_ATTEMPTS; i++)
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

    smp->index = sample_index;
    //cross check: accept signature if deltas in 10/5% range
    if (check_tolerance(is1, is2, 10) && check_tolerance(if1, if2, 20))
        smp->signature = min(is1, is2);
    else
        smp->signature = 0;

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

