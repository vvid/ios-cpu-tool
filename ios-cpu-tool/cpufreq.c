//
//  cpufreq.c
//  ios-cpu-tool
//
//  Created by vvid on 13/10/2017.
//  Copyright © 2017 vvid. All rights reserved.
//

//iphone SE/8 timebase 24MHz = 125/3 = 41,[6]ns


//- config --------------------
#define ANALYZE_DVFS_BOOST 0
//-----------------------------

#define CYCLES_PER_MS    2000000  //base scale

#define CALIB_UNROLL     20 //(unrolled 20x internally)
#define CALIB_REPEAT     (CYCLES_PER_MS/CALIB_UNROLL/8)  //1/N msec @ 2GHz
#define CALIB_ATTEMPTS   5

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
  #define ARCH_X64 1
  #define ARCH_A64 0
#elif TARGET_OS_IPHONE
  // define something for iphone  
  #define ARCH_X64 0
  #define ARCH_A64 1
#else
  #define TARGET_OS_OSX 1
  #define ARCH_X64 1
  #define ARCH_A64 0
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

#define DBG if (0)

//asm prototypes
extern void dummy_call(void);
extern int  cpu_workload(int count) __attribute__ ((noinline));

// returns delta raw timer delta
extern uint64_t calib_seq_add(uint64_t count);
extern uint64_t calib_signature(uint64_t count);

extern void calib_seq_add_x64(uint64_t count);
extern void calib_signature_x64(uint64_t count);

extern void calib_seq_nop(uint64_t count);
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

struct Array
{
    void *data;
    size_t count;
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

    uint8_t index;   //position in sample
    uint8_t sig_id;  //classified signature
    uint8_t freq_id; //classified freq
    uint8_t pad;

    uint64_t timestamp_b;
    uint64_t timestamp_e;
};

pthread_t measure_threads[MAX_THREADS];

double measure_thread_freq[MAX_THREADS];
double measure_thread_min_freq[MAX_THREADS];
double measure_thread_max_freq[MAX_THREADS];

struct FreqSample final_samples[NUM_FREQ_SAMPLES][MAX_THREADS];
int final_samples_count[NUM_FREQ_SAMPLES];
int num_final_samples = 0;

struct FreqPoint frequency_points[NUM_FREQ_POINTS];

int valid_samples[NUM_FREQ_SAMPLES];
int num_valid_samples = 0;

struct DataPoint data_pnts_in[NUM_FREQ_POINTS];
struct DataPoint data_pnts[NUM_FREQ_POINTS];

struct Cluster signature_clusters[NUM_FREQ_POINTS];
struct Cluster frequency_clusters[NUM_FREQ_POINTS];

int num_signature_clusters;
int num_frequency_clusters;

int thread_sample_index[MAX_THREADS];

struct FreqSample *thread_samples;
int num_thread_samples = 1000;

int num_freq_points = 0;
int num_threads = 0;
int thread_stop_event = 0;
int thread_lock_var = 0;

uint64_t timescale = 1;
uint64_t min_overhead = ~0;
uint64_t start_of_time = 0;

#define MAX_DVFS_SAMPLES 1024

uint16_t dvfs_freq_table[MAX_THREADS][MAX_DVFS_SAMPLES][2]; //first sample freq, second is time_in_sec * 65536


//Freq measurement state

struct CpuCoreInfo
{
    int freq;
    int signature;

    int min_freq;
    int max_freq;
    int min_signature;
    int max_signature;
    
    int sig_id;
    int freq_id;
    //caches
    //extra info

};

struct CpuCoreInfo cpu_cores[MAX_THREADS];
int num_cpu_cores = 0;

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

int find_cluster_id(struct Cluster *clusters, int cluster_size, int value)
{
    for (int i=0; i<cluster_size; i++)
    {
      if (value >= clusters[i].min_value &&
          value <= clusters[i].max_value)
          return i;
    }
    return -1;
}

int fsample_sig_up_comp(const void *a, const void *b)
{
    struct FreqSample *as = (struct FreqSample*)a;
    struct FreqSample *bs = (struct FreqSample*)b;
    if (as->signature < bs->signature) return -1;
    if (as->signature > bs->signature) return  1;
    return 0;
}
int freq_comp(const void *a, const void *b)
{
    struct FreqSample *as = (struct FreqSample*)a;
    struct FreqSample *bs = (struct FreqSample*)b;
    if (as->max_freq < bs->max_freq) return -1;
    if (as->max_freq > bs->max_freq) return  1;
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
#define SIG_FREQ_SIG_TOLERANCE      (100/2)

int clusterize(struct Cluster *clusters, struct DataPoint *dp, int num_points)
{
    //debug
    static int debug_pnts[] = {
        1680,1680, 2304,2304, 2305,2305
    };
//    num_points = 6;

    for (int i=0; i<num_points; i++)
    {
        //        dp[i].value = debug_pnts[i]; 
        dp[i].distance = 0;
        dp[i].index = i;
        dp[i].cluster = -1;
    }

    qsort(dp, num_points, sizeof(struct DataPoint), dpoint_up_comp);

    //calc_distance(dp, num_points);

    //create clusters

    int num_clusters = 0;
    for (int p=0; p<num_points; p++)
    {
        int found = 0;
        for (int c=0; c<num_clusters; c++)
        {
            if (dp[p].value == clusters[c].max_value)
            {
                found = 1;
                clusters[c].count++;
                clusters[c].min_idx = min(p, clusters[c].min_idx);
                clusters[c].max_idx = max(p, clusters[c].max_idx);
                dp[p].cluster = c;
            }
        }
        if (!found)
        {
            dp[p].cluster = num_clusters;
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

//    printf("clusterize: sorted data points\n");
//    dump_data_points(dp, num_points);
//    printf("clusterize: create clusters\n");
//    dump_clusters(clusters, num_clusters);

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

        for (int c=0; c<num_clusters; c++)
        {
            if (c != to &&
                    clusters[c].count > 0 &&
                    clusters[c].done == 0 &&
                    check_tolerance(clusters[to].ref_value, clusters[c].ref_value, SIG_CLUSTER_MERGE_TOLERANCE))
            {
                if (clusters[c].count > 1) //ignore single occurrences
                {
                    clusters[to].count += clusters[c].count;
                    clusters[to].min_value = min(clusters[to].min_value, clusters[c].min_value); 
                    clusters[to].max_value = max(clusters[to].max_value, clusters[c].max_value); 
                    clusters[to].min_idx   = min(clusters[to].min_idx,   clusters[c].min_idx); 
                    clusters[to].max_idx   = max(clusters[to].max_idx,   clusters[c].max_idx); 
                }
                clusters[c].count = 0;
            }
        }

        clusters[to].done = 1;
    };

    //pack clusters
    int ci = 0;
    for (int c=0; c<num_clusters; c++)
    {
        if (clusters[c].count > 0)
        {
            if (c != ci)
                clusters[ci] = clusters[c];
            ci++;
        }
    }
    num_clusters = ci;

//    printf("clusterize: cluster merge\n");
//    dump_clusters(clusters, num_clusters);
//    printf("clusterize: finished\n");

    return num_clusters;
}

//
void analyze_freq(void *report, int report_size)
{
    int min_length = 1000000;
    for (int i = 0; i<num_threads; i++)
        min_length = min(min_length, thread_sample_index[i]);

    num_freq_points = 0;
    num_valid_samples= 0;

    //collect freq points from samples
    for (int s = 0; s<min_length && num_valid_samples < NUM_FREQ_SAMPLES; s++)
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

        if (valid) //add freq point
        {
            valid_samples[num_valid_samples++] = s;

            printf("sample %d: ", s);

            for (int t = 0; t<num_threads; t++)
            {
                struct FreqSample *smp = get_sample(t, s);
                uint64_t tb = smp->timestamp_b;
                uint64_t te = smp->timestamp_e;

                printf("%d/%d(+%d:%f) ", smp->max_freq, smp->signature, time_from_start_ms(tb), hpctime_to_ms(te - tb));
  
                if (num_freq_points < NUM_FREQ_POINTS)
                {
                    frequency_points[num_freq_points].signature = smp->signature; 
                    frequency_points[num_freq_points].freq      = smp->max_freq;
                    frequency_points[num_freq_points].time      = te - tb;
                    frequency_points[num_freq_points].count     = 1;
                    num_freq_points++;
                }
            }
            printf("\n");
        }
    }

    DBG dump_freq_points();

    //find clusters based on signature
    for (int i=0; i<num_freq_points; i++)
        data_pnts_in[i].value = frequency_points[i].signature;
    num_signature_clusters = clusterize(signature_clusters, data_pnts_in, num_freq_points);

    //find frequency clusters
    for (int i=0; i<num_freq_points; i++)
        data_pnts_in[i].value = frequency_points[i].freq;
    num_frequency_clusters = clusterize(frequency_clusters, data_pnts_in, num_freq_points);

    struct FreqSample tmp_samples[MAX_THREADS];

    //classify samples and find most common values
    num_final_samples = 0;
    for (int vs=0; vs<num_valid_samples; vs++)
    {
        int classify_ok = 1;
        int vs_index = valid_samples[vs];
        for (int t=0; t<num_threads; t++)
        {
            tmp_samples[t] = *get_sample(t, vs_index);
            int sig_id = find_cluster_id(signature_clusters, num_signature_clusters, tmp_samples[t].signature);
            if (sig_id < 0)
                classify_ok = 0;
            else 
            {
                int freq_id = find_cluster_id(frequency_clusters, num_frequency_clusters, tmp_samples[t].max_freq);
                if (freq_id < 0)
                    classify_ok = 0;
                else
                {
                    tmp_samples[t].sig_id = sig_id;
                    tmp_samples[t].freq_id = freq_id;
                }
            }
        }

        if (classify_ok == 0)
            continue;

        qsort(tmp_samples, num_threads, sizeof(struct FreqSample), fsample_sig_up_comp);

        int found_cnt = 0;
        for (int fs=0; fs<num_final_samples; fs++)
        {
            found_cnt = 0;
            for (int t=0; t<num_threads; t++)
            {
                if (final_samples[fs][t].sig_id  == tmp_samples[t].sig_id &&
                    final_samples[fs][t].freq_id == tmp_samples[t].freq_id)
                found_cnt++;
            }
            if (found_cnt == num_threads)
            {
                final_samples_count[fs]++;
                break;
            }
        }

        if (found_cnt != num_threads) //new one
        {
            DBG printf("fs: %d ", num_final_samples);
            final_samples_count[num_final_samples] = 1;
            for (int t=0; t<num_threads; t++)
            {
               DBG printf(" %d/%d", tmp_samples[t].max_freq, tmp_samples[t].signature);
               final_samples[num_final_samples][t] = tmp_samples[t];
            }
            DBG printf("\n");
            num_final_samples++;
        }
    }

    num_cpu_cores = 0;

    //select max
    {
        int maxcnt = 0;
        int index = -1;
        for (int fs=0; fs<num_final_samples; fs++)
        {
            if (maxcnt < final_samples_count[fs])
            {
                maxcnt = final_samples_count[fs];
                index = fs;
            }
        }

        if (index >= 0) //export if have samples
        {
            for (int t=0; t<num_threads; t++)
            {
                struct FreqSample *smp = &final_samples[index][t];
                measure_thread_freq[t]     = 1000000.0 * frequency_clusters[smp->freq_id].ref_value;
                measure_thread_min_freq[t] = 1000000.0 * signature_clusters[smp->sig_id].ref_value;
                measure_thread_max_freq[t] = 1000000.0 * time_from_start_ms(smp->timestamp_b);

                cpu_cores[t].signature     = signature_clusters[smp->sig_id].ref_value;
                cpu_cores[t].min_signature = signature_clusters[smp->sig_id].min_value;
                cpu_cores[t].max_signature = signature_clusters[smp->sig_id].max_value;
                cpu_cores[t].freq     = frequency_clusters[smp->freq_id].ref_value;
                cpu_cores[t].min_freq = frequency_clusters[smp->freq_id].min_value;
                cpu_cores[t].max_freq = frequency_clusters[smp->freq_id].max_value;
                cpu_cores[t].sig_id  = smp->sig_id;
                cpu_cores[t].freq_id = smp->freq_id;
            }

            num_cpu_cores = num_threads;

            //print cores to console
            printf("Detected %d cluster(s), %d core(s) at +%d ms\n", num_signature_clusters, num_cpu_cores,
                   (int)time_from_start_ms(final_samples[index][0].timestamp_b));
            for (int t=0; t<num_cpu_cores; t++)
              printf("core #%d %d MHz sig %d:%d\n", t, cpu_cores[t].freq, cpu_cores[t].sig_id, cpu_cores[t].signature);
        }
    }
}

#if ARCH_X64
uint64_t calib_seq_add(uint64_t count)
{
    uint64_t tb = hpctime();
    calib_seq_add_x64(count);
    uint64_t te = hpctime();
    return te - tb;
}
uint64_t calib_signature(uint64_t count)
{
    uint64_t tb = hpctime();
    calib_signature_x64(count);
    uint64_t te = hpctime();
    return te - tb;
}
#endif

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


void measure_dvfs_boost(int thread)
{
    int sample = 0;
    uint64_t tb, te, start_time;

#define DVFS_TEST_REPEAT (CYCLES_PER_MS/CALIB_UNROLL/4)

    start_time = hpctime();

    for (int i=0; i<MAX_DVFS_SAMPLES; i++)
    {
        uint64_t tb, te;
        uint64_t min_time = ~0;

        //sample per ~ 1/4 msec
        for (int i = 0; i < 2; i++)
        {
            tb = hpctime();
            calib_seq_add(CALIB_UNROLL * DVFS_TEST_REPEAT);
            te = hpctime();
            if (min_time > te - tb)
                min_time = te - tb;
        }

        if (min_time > min_overhead)
            min_time -= min_overhead;

        uint64_t time_from_start = tb - start_time;
        double time_from_s = hpctime_to_s(time_from_start);

        double freq = CALIB_UNROLL * DVFS_TEST_REPEAT / hpctime_to_s(min_time);
        dvfs_freq_table[thread][i][0] = freq_mhz(freq); 
        dvfs_freq_table[thread][i][1] = min(65535, 65536 * time_from_s);
    }
}

void analyze_dvfs_boost()
{
  for (int t=0; t<num_threads; t++)
  {
    const int columns = 8;
    printf("dvfs table #%d", t);
    for (int i=0; i<MAX_DVFS_SAMPLES/columns; i++)
    {
        printf("\n%3d (+%6d us): ", i*columns, (int)(dvfs_freq_table[t][i*columns+0][1] / 65536.0 * 1000000));
        for (int j=0; j<columns; j++)
        {
          printf("%4d ", dvfs_freq_table[t][i*columns+j][0]);
        }
    }
    printf("\n");
  }
}


double measure_freq()
{
    uint64_t tb, te;
    uint64_t min_time = ~0;

    for (int i = 0; i < CALIB_ATTEMPTS; i++)
    {
        tb = hpctime();
        uint64_t tf = calib_seq_add(CALIB_UNROLL * CALIB_REPEAT);
        te = hpctime();
        if (min_time > te - tb)
            min_time = te - tb;
#if ARCH_A64
        if (min_time > tf)
            min_time = tf;
#endif
    }
#if !ARCH_A64
    if (min_time > min_overhead)
        min_time -= min_overhead;
#endif
    return CALIB_UNROLL * CALIB_REPEAT / hpctime_to_s(min_time);
}

double measure_signature(double freq)
{
    uint64_t tb, te;
    uint64_t min_time = ~0;

    for (int i = 0; i < 5; i++)
    {
        tb = hpctime();
        uint64_t tf = calib_signature(CALIB_REPEAT);
        te = hpctime();
        if (min_time > te - tb)
            min_time = te - tb;
#if ARCH_A64
        if (min_time > tf)
            min_time = tf;
#endif
    }
#if !ARCH_A64
    min_time -= min_overhead;
#endif

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
    if (check_tolerance(is1, is2, (100/10)) && check_tolerance(if1, if2, (100/5)))
        smp->signature = min(is1, is2);
    else
        smp->signature = 0;

    smp->max_freq = max(if1, if2);
    smp->min_freq = min(if1, if2);
}

void * measure_thread(void *thread_id)
{
    long tid = (long)thread_id;

#if ANALYZE_DVFS_BOOST
    measure_dvfs_boost((int)tid);
#else
    warmup();
#endif

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

void calibrate_start_threads(int num_cores, int steps)
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

void calibrate_stop_threads()
{
    thread_stop_event = 1;
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(measure_threads[i], NULL);
    }

#if ANALYZE_DVFS_BOOST
    analyze_dvfs_boost();
#endif
    analyze_freq(NULL, 0);
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

