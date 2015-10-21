#include "xjob.h"
#include <linux/moduleloader.h>

struct queue *head, *tail;
struct mutex qmutex; /* protect job queue & wait queue */
int qlen;
int qmax;
wait_queue_head_t pwq;
wait_queue_head_t cwq;
struct task_struct **cthreads;
bool should_stop;
unsigned curr_id;
int num_consumer;

static spinlock_t job_id_lock;

void destroy_job(struct job *job)
{
	if (job->infile && !IS_ERR(job->infile))
		putname(job->infile);

	if (job->outfile && !IS_ERR(job->outfile))
		putname(job->outfile);

	kfree(job);
}

/* create a global unique job id
 * XXX may use random generateor to make it safer */
static int new_job_id(void)
{
	int ret;

	spin_lock(&job_id_lock);
	ret = ++curr_id;
	if (curr_id == INT_MAX)
		curr_id = 0;
	spin_unlock(&job_id_lock);

	return ret;
}

static int init_job(struct job *job, struct xargs *xarg)
{
	job->state = STATE_NEW;
	job->id = xarg->id;
	job->oflags = xarg->oflags;
	job->category = xarg->category;
	job->algo = xarg->algo;
	job->infile = NULL;
	job->outfile = NULL;
	job->pid = current->pid;

	if (job->id <= 0) {
		INFO("Invalid job id");
		return -EINVAL;
	}

	if (job->category == CATEGORY_UNDEFINED
			|| job->category >= CATEGORY_LAST) {
		INFO("Invalid job category");
		return -EINVAL;
	}

	/* XXX should check algorithm here? maybe do not need algo? */
	if (job->algo == ALGORITHM_UNDEFINED
			|| job->algo >= ALGORITHM_LAST) {
		INFO("Invalid algorithm");
		return -EINVAL;
	}

	job->infile = getname(xarg->infile);
	if (IS_ERR(job->infile)) {
		INFO("infile getname failed");
		return -EFAULT;
	}

	job->outfile = getname(xarg->outfile);
	if (IS_ERR(job->outfile)) {
		INFO("outfile getname failed");
		return -EFAULT;
	}

	return 0;
}

static int remove_queued_jobs(void)
{
	struct queue *q;

	mutex_lock(&qmutex);
	INFO("removing all...");

	while (head) {
		q = remove_first_job();
		qlen--;
		destroy_job(q->job);
		kfree(q);
	}
	wake_up_all(&pwq);

	mutex_unlock(&qmutex);

	return 0;
}

static int remove_queued_job(int id)
{
	struct queue *q = NULL;
	int err = -ENXIO;

	if (id < 0)
		return -EINVAL;

	mutex_lock(&qmutex);
	INFO("removing job[%d]", id);

	if (qlen > 0) {
		q = remove_job(id);
		if (q) {
			qlen--;
			destroy_job(q->job);
			kfree(q);
			err = 0;
			INFO("job [%d] removed", id);
			wake_up_all(&pwq);
		}
	}

	mutex_unlock(&qmutex);

	return err;
}

/* On success 1 is returned. On end of job lists, 0 is returned */
static int list_queued_jobs(__user struct jobent *list_buf,
			    int list_len)
{
	int count = 0;
	struct queue *q;
	int err = 0;
	int name_len;
	__user struct jobent *ent;

	if (list_len <= 0)
		return -EINVAL;
	if (!access_ok(VERIFY_WRITE, list_buf,
				list_len * sizeof(struct jobent)))
		return -EFAULT;

	mutex_lock(&qmutex);
	INFO("listing...");

	q = head;
	while (q && count < list_len) {
		ent = &list_buf[count];
		name_len = strlen(q->job->infile);
		if (name_len > NAME_MAX)
			name_len = NAME_MAX;

		__put_user(q->job->id, &ent->id);
		__put_user(q->job->pid, &ent->pid);
		__put_user(q->job->category, &ent->category);
		__put_user(name_len, &ent->name_len);
		if (__copy_to_user(ent->infile, q->job->infile,
				   name_len)) {
			err = -EFAULT; /* unknown err */
			goto out;
		}
		__put_user(0, ent->infile + name_len);

		q = q->next;
		count++;
	}

	if (q)
		err = 1;

	/* set id = 0 to suggest end of list */
	if (count < list_len)
		__put_user(0, &list_buf[count].id);
out:
	mutex_unlock(&qmutex);
	return err;
}

asmlinkage static long xjob(__user void *args, int argslen)
{
	struct xargs *xarg = NULL;
	struct job *job = NULL;
	int err = 0;

	/* check and copy user argument */
	if (!args)
		return -EINVAL;

	/* unless modifing syscall prototype to two arguments
	 * if (sizeof(struct xargs) != argslen)
	 *	return -EINVAL;
	 */
	argslen = sizeof(struct xargs);

	xarg = kmalloc(argslen, GFP_KERNEL);
	if (!xarg)
		return -ENOMEM;
	if (copy_from_user(xarg, args, argslen)) {
		INFO("invalid user address passed");
		err = -EFAULT;
		goto out;
	}

	if (xarg->action == ACTION_SETUP) {
		err = new_job_id();
		goto out;
	} else if (xarg->action == ACTION_REMOVE_ONE) {
		err = remove_queued_job(xarg->id);
		goto out;
	} else if (xarg->action == ACTION_REMOVE_ALL) {
		err = remove_queued_jobs();
		goto out;
	} else if (xarg->action == ACTION_LIST) {
		err = list_queued_jobs(xarg->list_buf, xarg->list_len);
		goto out;
	} else if (xarg->action >= ACTION_LAST) {
		err = -EINVAL;
		goto out;
	}

	/* init job */
	job = kmalloc(sizeof(struct job), GFP_KERNEL);
	if (!job) {
		err = -ENOMEM;
		goto out;
	}
	err = init_job(job, xarg);
	if (err)
		goto out_err;

	err = produce(job);
	if (err)
		goto out_err;

	err = job->id;

	/* submit successfully, do not free job */
	goto out;

out_err:
	destroy_job(job);
out:
	kfree(xarg);

	return err;
}

static void init_global(void)
{
	int i;
	qlen = 0;
	qmax = Q_MAX_SIZE;
	head = NULL;
	tail = NULL;
	should_stop = false;
	curr_id = 0;
	mutex_init(&qmutex);

	init_waitqueue_head(&pwq);
	init_waitqueue_head(&cwq);

	if (nr_cpu_ids > Q_MAX_SIZE)
		num_consumer = nr_cpu_ids;
	else if (nr_cpu_ids == 1)
		num_consumer = 2; /* for demo */
	else
		num_consumer = nr_cpu_ids;
	INFO("initializing %d consumers", num_consumer);

	cthreads = kmalloc(num_consumer * sizeof(struct task_struct *),
			   GFP_KERNEL);
	for (i = 0; i < num_consumer; i++)
		cthreads[i] = kthread_run(consume, (void *)i, "Consumer/%d", i);
}

static void destroy_global(void)
{
	struct queue *q;
	int i;

	INFO("destorying...");

	mutex_lock(&qmutex);

	should_stop = true;
	wake_up_all(&pwq); /* wake up all producers */

	/* wake up and stop all consumers */
	for (i = 0; i < num_consumer; i++)
		kthread_stop(cthreads[i]);

	while (head) {
		q = remove_first_job();
		qlen--;
		destroy_job(q->job);
		kfree(q);
	}

	kfree(cthreads);

	mutex_unlock(&qmutex);
}

static int __init init_sys_xjob(void)
{
	INFO("installed new sys_xjob module");

	init_global();

	if (sysptr == NULL)
		sysptr = xjob;

	return 0;
}

static void  __exit exit_sys_xjob(void)
{
	if (sysptr != NULL)
		sysptr = NULL;

	destroy_global();

	INFO("removed sys_xjob module");
}

module_init(init_sys_xjob);
module_exit(exit_sys_xjob);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kelong Wang, Jing Yang");
MODULE_DESCRIPTION("CSE-506-S14 HW3");

