void warmup();

void calculate_freq_start(int num_cores, int steps);
void calculate_freq_stop();

void measure_boost_start(int num_cores, int length_ms, int pause_ms, int attempts);
void measure_boost_stop();

double get_thread_freq(int core_id);
double get_thread_min_freq(int core_id);
double get_thread_max_freq(int core_id);

