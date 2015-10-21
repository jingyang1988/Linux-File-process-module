#ifndef _XJOB_H_
#define _XJOB_H_

#include <linux/linkage.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/signal.h>

#include "common.h"

#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

#undef XDEBUG
#ifdef XDEBUG
#define INFO(fmt, ...) \
	pr_info("[%s:%s:%d] " fmt "\n", __FILE__, __func__, __LINE__,\
		##__VA_ARGS__)
#else
#define INFO(fmt, ...) \
	pr_info(fmt "\n", ##__VA_ARGS__)
#endif

#define Q_MAX_SIZE 5

enum job_state_class {
	STATE_NEW,
	STATE_PENDING,
	STATE_PROCESSING,
	STATE_SUCCESS,
	STATE_ABORTE,
	STATE_FAILED,
};

struct job {
	int id;
	pid_t pid;
	int state;
	unsigned int category;
	unsigned int algo;
	unsigned int oflags;
	const char *infile;
	const char *outfile;
};

struct queue {
	struct job *job;
	struct queue *next;
};

asmlinkage extern long (*sysptr)(__user void *args, int argslen);
extern int consume(void *);
extern int produce(struct job *);
extern void add2queue(struct queue *);
extern struct queue *remove_first_job(void);
extern struct queue *remove_job(int id);
extern void destroy_job(struct job *);
extern void check(struct job *);
extern int checksum(int, const char *, const char *, int);

/* global shared variables */
extern struct queue *head, *tail;
extern struct mutex qmutex; /* protect job queue & wait queue */
extern int qlen;
extern int qmax;
extern wait_queue_head_t pwq;
extern wait_queue_head_t cwq;
extern int num_consumer;
extern struct task_struct **cthreads;
extern bool should_stop;
extern unsigned int curr_id;

#endif	/* not _XJOB_H_ */

