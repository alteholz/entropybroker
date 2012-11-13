#define MEM_POOL_COUNTS VAR_DIR "/mem_pool_counts.dat"
#define MEM_POOL_BIT_COUNT_COUNTS VAR_DIR "/mem_pool_bit_count_counts.dat"
#define DSK_POOL_COUNTS VAR_DIR "/dsk_pool_counts.dat"
#define DSK_POOL_BIT_COUNT_COUNTS VAR_DIR "/dsk_pool_bit_count_counts.dat"
#define CONNECTION_COUNTS VAR_DIR "/connection_counts.dat"

class data_logger
{
private:
	pthread_mutex_t mem_pool_lck;
	data_store_int *mem_pool_counts;

	pthread_mutex_t dsk_pool_lck;
	data_store_int *dsk_pool_counts;

	pthread_mutex_t connection_counts_lck;
	data_store_int *connection_counts;

	pthread_mutex_t mem_pool_bit_count_lck;
	data_store_int *mem_pool_bit_count_counts;

	pthread_mutex_t dsk_pool_bit_count_lck;
	data_store_int *dsk_pool_bit_count_counts;

	pthread_t thread;

	pthread_mutex_t terminate_flag_lck;
	bool abort;

	///
	pools *ppools;

	std::vector<client_t *> *clients;
	pthread_mutex_t *clients_mutex;

	void dump_data();

public:
	data_logger(pools *ppools_in, std::vector<client_t *> *clients_in, pthread_mutex_t *clients_mutex_in);
	~data_logger();

	void get_mem_pool_counts(long int **t, double **v, int *n);
	void get_dsk_pool_counts(long int **t, double **v, int *n);
	void get_connection_counts(long int **t, double **v, int *n);
	void get_pools_bitcounts(long int **t, double **v, int *n);
	void get_disk_pools_bitcounts(long int **t, double **v, int *n);

	void run();
};
