#include "xjob.h"

static int __produce(struct job *job, wait_queue_t *wait)
{
	struct queue *q;
	int err = 0;

	mutex_lock(&qmutex);

	if (qlen >= qmax) {
		prepare_to_wait_exclusive(&pwq, wait, TASK_UNINTERRUPTIBLE);
		err = -EAGAIN;
		goto out;
	}

	q = kmalloc(sizeof(struct queue), GFP_KERNEL);
	if (q == NULL) {
		err = -ENOMEM;
		goto out;
	}
	q->job = job;
	INFO("add new job[%u] to task queue", job->id);
	add2queue(q);
	qlen++;

	wake_up(&cwq); /* wake up one consumer */
out:
	mutex_unlock(&qmutex);
	return err;
}

int produce(struct job *job)
{
	int err = 0;
	DEFINE_WAIT(wait);
	while (1) {
		err = __produce(job, &wait);
		if (err != -EAGAIN)
			break;
		/* entered the wait queue */
		INFO("Producer: waiting");
		schedule();
		INFO("Producer: awaking");
		/* during rmmod, we should stop producing or sleeping */
		if (should_stop) {
			err = -EBUSY;
			break;
		}
	}

	return err;
}

static void notify_user(struct job *job, int err, int cid)
{
	struct siginfo sinfo;
	struct task_struct *task;

	memset(&sinfo, 0, sizeof(struct siginfo));
	sinfo.si_code = SI_QUEUE; /* important, make si_int available */
	sinfo.si_int = job->id;
	sinfo.si_signo = SIGUSR1;
	sinfo.si_uid = current_uid();
	if (job->state == STATE_SUCCESS)
		sinfo.si_errno = 0;
	else if (job->state == STATE_FAILED)
		sinfo.si_errno = -err; /* make it positive */
	else
		return;

	task = pid_task(find_vpid(job->pid), PIDTYPE_PID);
	if (!task) {
		INFO("Consumer/%d: pid[%d] not found", cid, job->pid);
		return;
	}

	send_sig_info(SIGUSR1, &sinfo, task);
}

static int __process_job(struct job *job)
{
	if (job->category == CATEGORY_CHECKSUM)
		return checksum(job->algo, job->infile, job->outfile,
				job->oflags);

	return -ENOTSUPP;
}

static int process_job(struct job *job, int cid)
{
	int err = 0;
	job->state = STATE_PROCESSING;

	INFO("%s", job->infile);

	err = __process_job(job);
	if (err)
		INFO("__process_job return %d", err);
	job->state = err ? STATE_FAILED : STATE_SUCCESS;

	notify_user(job, err, cid);
	destroy_job(job);

	return 0;
}

static int __consume(wait_queue_t *wait, int cid)
{
	struct queue *q;
	int ret;

	mutex_lock(&qmutex);

	if (qlen == 0) {
		INFO("Consumer/%d: waiting", cid);
		prepare_to_wait_exclusive(&cwq, wait, TASK_INTERRUPTIBLE);
		mutex_unlock(&qmutex);
		return 0;
	}
	q = remove_first_job();
	qlen--;

	if (qlen < qmax && waitqueue_active(&pwq))
		wake_up(&pwq); /* wake up one producer */

	mutex_unlock(&qmutex);

	INFO("Consumer/%d: processing job[%u]", cid, q->job->id);
	ret = process_job(q->job, cid);
	INFO("Consumer/%d: done", cid);
	kfree(q);

	return ret;
}

int consume(void *cid)
{
	DEFINE_WAIT(wait);
	INFO("Consumer/%d: Initialized!", (int)cid);
	while (1) {
		__consume(&wait, (int)cid);
		schedule();
		/* if wake up by kthread_stop, do not fall asleep again
		 * absence of "should_stop" will cause problem during rmmod */
		if (should_stop || kthread_should_stop())
			break;
	}

	return 0;
}

/* to invoke the queue functions below, grab qmutex first */
void add2queue(struct queue *q)
{
	q->next = NULL;

	if (!tail) {
		tail = q;
		head = q;
		return;
	}

	tail->next = q;
	tail = q;
}

struct queue *remove_first_job(void)
{
	struct queue *ret = head;
	head = head->next;
	if (!head)
		tail = NULL;

	return ret;
}

/* only remove its first occurrence */
struct queue *remove_job(int id)
{
	struct queue *prev = NULL;
	struct queue *curr = head;

	if (!curr)
		return NULL;

	while (curr->job->id != id) {
		prev = curr;
		curr = curr->next;
		if (!curr)
			break;
	}

	/* found */
	if (curr) {
		if (prev) /* curr is not head */
			prev->next = curr->next;
		else
			head = curr->next;

		if (curr == tail)
			tail = prev;
	}

	return curr;
}

/*
DEFINE_SPINLOCK(sl);
static void dump_queue(void)
{
	struct queue *q;
	int i = 0;
	spin_lock(&sl);
	INFO("-----begin dump----");
	INFO("%d", qlen);
	INFO("head: %p", head);
	if (head)
		INFO("head->next: %p", head->next);
	INFO("tail: %p", tail);
	if (tail)
		INFO("tail->next: %p", tail->next);

	q = head;
	while (q) {
		INFO("%d: %p", i, q);
		i++;
		q = q->next;
	}
	spin_unlock(&sl);

	return;
}
*/

