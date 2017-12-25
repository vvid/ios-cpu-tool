void warmup();

void calibrate_start_threads(int num_cores, int steps);
void calibrate_stop_threads();

double get_thread_freq(int core_id);
double get_thread_min_freq(int core_id);
double get_thread_max_freq(int core_id);
