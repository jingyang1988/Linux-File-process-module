#define MD5_HASH_SIZE 16
#define SHA1_HASH_SIZE 20

enum job_action_class {
	ACTION_SETUP = 0,
	ACTION_SUBMIT,
	ACTION_LIST,
	ACTION_REMOVE_ONE,
	ACTION_REMOVE_ALL,
	ACTION_LAST,
};

enum job_category_class {
	CATEGORY_UNDEFINED = 0,
	CATEGORY_CHECKSUM,
	CATEGORY_COMPRESS,
	CATEGORY_LAST,
};

enum job_algorithm_class {
	ALGORITHM_UNDEFINED = 0,
	ALGORITHM_MD5,
	ALGORITHM_SHA1,
	ALGORITHM_LAST,
};

static inline char *get_category_name(enum job_category_class category)
{
	static char *names[] = {"UNDEFINED", "CHECKSUM",
					     "COMPRESS", ""};
	return names[category];
}

static inline char *get_algo_name(enum job_algorithm_class algo)
{
	static char *names[] = {"", "md5", "sha1", ""};
	return names[algo];
}

static inline int get_hash_size(enum job_algorithm_class algo)
{
	static const int size[] = {0, MD5_HASH_SIZE, SHA1_HASH_SIZE, 0};
	return size[algo];
}

static inline int get_digest_size(enum job_algorithm_class algo)
{
	return get_hash_size(algo) * 2;
}

struct jobent {
	int id;
	pid_t pid;
	unsigned int category;
	unsigned int name_len; /* length of infile, may truncated to NAME_MAX */
	char infile[NAME_MAX+1]; /* null terminated */
};

struct xargs {
	int id;
	unsigned int action;
	unsigned int category;
	unsigned int algo;
	unsigned int oflags;
	__user const char *infile;	/* should be absolute path */
	__user const char *outfile;	/* should be absolute path */
	__user struct jobent *list_buf;
	unsigned int list_len;
};

