void warmup();
double measure_freq();
double measure_workload();

void start_threads(int num_cores);
void stop_threads();

double get_thread_freq(int core_id);
double get_thread_min_freq(int core_id);
double get_thread_max_freq(int core_id);
