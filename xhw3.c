#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>

#define __user
#define NAME_MAX 255

#include "common.h"

#define __NR_xjob	349	/* our private syscall number */
#define JOB_LIST_LEN	20
#define O_EXCL		00000200

int job_id = -1;
int job_errno;

void signal_handler(int signo, siginfo_t *sinfo, void *ucontext)
{
	if (signo != SIGUSR1)
		return;

	/* XXX should check if signal comes from kernel */
	job_id = sinfo->si_int;
	job_errno = sinfo->si_errno;
}

void usage()
{
	printf("usage: xhw3 [flags] infile\n");
	printf("\n");
	printf(" flags:\n");
	printf(" -o: output file\n");
	printf(" -w: overwrite existing output file\n");
	printf(" -a ALGO: set checksum algorithm(md5, sha1)\n");
	printf(" -C: calculate the checksum of infile\n");
	printf(" -R: remove all queued jobs\n");
	printf(" -r: remove queued job by id\n");
	printf(" -L: list all(%d) queued jobs\n", JOB_LIST_LEN);
	printf(" -n: do not block after creating job\n");
	printf(" -h: print this usage\n");
}

char *get_outfile_path(char *outfile)
{
	char *path;
	char *cwd;

	if (outfile[0] == '/')
		return strdup(outfile);

	cwd = get_current_dir_name();
	if (!cwd)
		return NULL;

	path = malloc(strlen(cwd) + strlen(outfile) + 2);
	if (!path) {
		free(cwd);
		return NULL;
	}
	sprintf(path, "%s/%s", cwd, outfile);
	free(cwd);

	return path;
}

int main(int argc, char *argv[])
{
	int rc;
	int ch;
	int oflags = O_EXCL; /* by default, do not overwrite */
	int err = 0;
	int id = 0;
	int action = ACTION_SUBMIT;
	int category = CATEGORY_UNDEFINED;
	int algo = ALGORITHM_MD5; /* make md5 default hash algo */
	int block = 1; /* do not wait for the signal */
	char *outfile = NULL;
	char *infile = NULL;

	while ((ch = getopt(argc, argv, "CLRr:a:hnwo:")) != -1) {
		switch (ch) {
		case 'C':
			category = CATEGORY_CHECKSUM;
			break;
		case 'L':
			action = ACTION_LIST;
			break;
		case 'R':
			action = ACTION_REMOVE_ALL;
			break;
		case 'r':
			action = ACTION_REMOVE_ONE;
			id = strtol(optarg, 0, 10);
			break;
		case 'a':
			if (strcmp(optarg, "md5") == 0)
				algo = ALGORITHM_MD5;
			else if (strcmp(optarg, "sha1") == 0)
				algo = ALGORITHM_SHA1;
			else
				algo = ALGORITHM_UNDEFINED;
			break;
		case 'o':
			outfile = get_outfile_path(optarg);
			if (!outfile) {
				printf("invalid output file path\n");
				exit(1);
			}
			break;
		case 'w':
			oflags &= ~O_EXCL;
			break;
		case 'n':
			block = 0;
			break;
		case 'h':
		case '?':
		default:
			usage();
			exit(1);
			break;
		}
	}

	/* more validation */
	if (action == ACTION_SUBMIT && algo == ALGORITHM_UNDEFINED) {
		printf("Please specify a valid algorithm\n");
		usage();
		err = 1;
		goto out;
	}

	if (action == ACTION_SUBMIT && category == CATEGORY_UNDEFINED) {
		printf("Please specify a valid category, e.g. -C\n");
		usage();
		err = 1;
		goto out;
	}

	/* packing syscall args */
	struct xargs args;
	args.id = id;
	args.infile = NULL;
	args.outfile = outfile;
	args.oflags = oflags;
	args.action = action;
	args.category = category;
	args.algo = algo;
	args.list_buf = NULL;
	args.list_len = 0;

	if (optind < argc) {
		infile = realpath(argv[optind], NULL);
		if (!infile) {
			perror("invalid infile");
			err = 1;
			goto out;
		}
		args.infile = infile;
	} else if (action == ACTION_SUBMIT) {
		usage();
		err = 1;
		goto out;
	}

	if (action == ACTION_LIST) {
		args.list_len = JOB_LIST_LEN;
		args.list_buf = malloc(JOB_LIST_LEN * sizeof(struct jobent));
		if (!args.list_buf) {
			printf("malloc failed\n");
			err = 1;
			goto out;
		}
	}

	/* register signal */
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = signal_handler;
	if (sigaction(SIGUSR1, &act, NULL) == -1) {
		perror("sigaction() failed");
		err = 1;
		goto out;
	}

	if (action == ACTION_SUBMIT) {
		/* before submitting an actual job, get a unique id first.
		 * because the signal may come back faster than the syscall.
		 * through this id, we can identify the job. */
		args.action = ACTION_SETUP;
		rc = syscall(__NR_xjob, (void *)&args, sizeof(struct xargs));
		if (rc < 0) {
			perror("job setup error");
			err = 1;
			goto out;
		}
		args.id = rc;
		args.action = ACTION_SUBMIT;
		/* then we can submit with this unique id */
	}

	rc = syscall(__NR_xjob, (void *)&args, sizeof(struct xargs));

	if (rc < 0) {
		perror("message");
		err = 1;
		goto out;
	}

	if (action == ACTION_LIST) {
		int i = 0;
		struct jobent *ent;
		printf("Job_ID\tPID\tCategory\tInput\n");
		printf("------\t---\t--------\t-----\n");
		while (i < args.list_len) {
			ent = &args.list_buf[i];
			if (ent->id == 0)
				break;
			printf("%d\t%d\t%s\t%s\n", ent->id, ent->pid,
				get_category_name(ent->category), ent->infile);
			i++;
		}
		printf("Total: %d queued job%s\n", i, i > 1 ? "s" : "");
		goto out;
	}

	/* waiting for signal */
	if (action == ACTION_SUBMIT) {
		printf("Job[%d] submited.\n", args.id);
		if (block) {
			/* job may be already completed (job_id >= 0)*/
			printf("waiting for result...\n");
			if (job_id < 0)
				pause(); /* sleep until signal comes */
			printf("Job[%d] result: %s.\n",
					job_id, strerror(job_errno));
		}
	} else {
		printf("syscall returns %d\n", rc);
	}

out:
	free(infile);
	free(outfile);
	free(args.list_buf);

	return err;
}

