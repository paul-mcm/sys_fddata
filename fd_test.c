/* Test child proc's or pthread's ability to write(2) to file through
 * specific descriptor AFTER:
 *	1) parent process/main thread has opened the file, and
 *	2) parent forks or spawns new thread, and
 *	3) parent calls open(2) enough times to max out its descriptor table
 *	   causing kernel to invoke fdgrowtable() and either free the old
 *	   descriptor table or put it on the proc's free list (fd_free)
 *	   in filedesc0.
 *
 *	sleep(3) calls are used to ensure proper sequence of actions
 *	between parent (or main thread) and child (or new thread).
 *
 * Args:
 * 	-c parent calls rfork with RFPROC|RFFDG flags (equivalent of
 *	   standard fork(2) - child gets copy of parent's descriptor table).
 *
 * 	-f <file> - Required. filename to open/test write(2) call.  If
 *	   file already exists its length will be truncated.
 *
 * 	-n Number of descriptors to open(2), causing kernel to call
 *	   fdgrowtable() and either free a descriptor table or put it on
 *	   the proc's free list.  Defaults to 61 - the minimum to get a
 *	   table onto free list.  Program will repeatedly open the same
 *	   file (/var/log/messages) to max out available descriptors in
 *	   table.
 *
 *	-s Parent calls rfork with RFPROC flag (child shares desciptor
 *	   table of parent).
 *
 *	-t Spawn a pthread.
 *
 * -f <file> is required.  One of either -c, -s or -t is required.  
 *
 * If the fddata() syscall is available (via fddata_syscall.ko module)
 * then it is called 3 times.  Once before opening any files; again after
 * the kernel has called fdgrowtable() to allocate a new descriptor
 * table, and again by the child process.  fddata() writes the following
 * details about the calling proc's state to the console and logging
 * facility:
 *	p_pid
 *	p_numthreads
 *	fd_refcnt
 *	fd_nfiles
 *	fd_files address
 *	ft_table (if any tables are on the free list)
 *
 * The information about the proc's state should be enough to determine
 * that old descriptor table can be safely freed if:
 * 	 1) proc has only a single thread; (e.g. proc's p_numthread == 1)
 *	 2) proc has not shared filedesc struct w/ another process via
 *	    rfork (e.g., proc's filedesc->fd_refcnt == 1)
 *
 */

#include <sys/types.h>
#include <sys/module.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int	syscall_n(const char *);
void	test_write(void);
void	open_nfiles(int);
void	do_rfork(int);
void	do_pthread_create(void);
void	*pthread_func(void *);
void	fatal_sys(char *, int);
void	fatal(char *);

enum syscall_stat {
	FALSE,
	TRUE,
} sys_fddata;

char	   	test_buff[] = "testdata write\n";
char		*file;
int		fd;
int		sysnum;

int main(int argc, char *argv[])
{
	int		rfork_flgs, ch;
	pid_t		pid;
	struct  rlimit	lim;
	long		nfiles;
	int		test_type;

	nfiles = 61;
	test_type = 0;

	if (argc < 4)
	    fatal("Wrong # of args");

	if (getrlimit(RLIMIT_NOFILE, &lim) != 0)
	    fatal_sys("getrlimit error", errno);

	while ((ch = getopt(argc, argv, "cn:stf:")) != -1) {
	    switch (ch) {
		case 'c':
		    rfork_flgs = RFPROC|RFFDG; /* copy parent's descriptor table */
		    test_type = 1;
		    break;

		case 'f':
		    file = optarg;
		    break;

		case 'n':
		    if (strcmp(optarg, "0") == 0) 
			nfiles = 0;
		    else if (strcmp(optarg, "0") < 0)
			fatal("arg to -n must be >= to 0");
		    else if ((nfiles = strtol(optarg, NULL, 0)) == 0)
			fatal("bad val to -n flag");

		    if (nfiles > lim.rlim_cur)
			fatal("Number of open files exceeds rlimits");

		    break;

		case 's':	/* descriptor table shared w/ parent */
		    rfork_flgs = RFPROC;
		    test_type = 1;
		    break;

		case 't':	/* start a pthread */
		    test_type = 2;		
		    break;

		case '?':
		default:
		    printf("bad args\n");
		    exit(-1);
	    }
	}

	argc -= optind;
	argv += optind;

	if (test_type == 0)
	    fatal("Test type required; either -c, -s, or -t");

	if (file == NULL)
	    fatal("Filename required");

	if ((sysnum = syscall_n("sys/fddata")) != -1)
	    sys_fddata = TRUE;

	if (sys_fddata == TRUE)
	    syscall(sysnum);

	if ((fd = open(file, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) < 0)
	    fatal_sys("open() error", errno);

	if (test_type == 1)
	    do_rfork(rfork_flgs);

	if (test_type == 2)
	    do_pthread_create();

	printf("Parent pid %d opening %ld file descriptors\n", getpid(), nfiles);
	open_nfiles(nfiles);

	if (sys_fddata == TRUE)
	    syscall(sysnum);

	sleep(8);	/* let thread/child proc finish before exiting */
	return 0;
}

/* Call rfork */
void
do_rfork(int f)
{
	pid_t 	pid;
	int	r;

	if ((pid = rfork(f)) == 0) {	/* child */

	    sleep(3);			/* Let parent run first */

	    if (sys_fddata == TRUE)
		syscall(sysnum);

	    printf("Child pid %d testing write to file %s\n", getpid(), file);

	    test_write();	   

	    exit(0);

	} else if (pid < 0)
	    fatal_sys("rfork error", errno);
}

void
test_write()
{
	ssize_t	r;
	int 	nw = 0;

	while (nw < sizeof(test_buff)) {
	    if ((r = write(fd, test_buff, sizeof(test_buff))) < 0)
		fatal_sys("Test write() failed", errno);
	    else
		nw += r;
	}
	
	printf("Successfully wrote %d bytes\n", nw);
}

void
open_nfiles(int n)
{
	int 	i, fd;
	char	*file = "/var/log/messages";		

	for (i = 0; i < n; i++)
	    if ((fd = open(file, O_RDONLY)) < 0)
		fatal_sys("Error in open_nfiles", errno);
}

/* Retrieve and return kmod's syscall number if loaded */
int
syscall_n(const char *kmod)
{
        struct module_stat mstat;       

        mstat.version = sizeof(mstat);
        if (modstat(modfind(kmod), &mstat) != 0)
	    return(-1);
        else
	    return mstat.data.intval;
}

void
do_pthread_create()
{
	pthread_t	tid;
	int		r;

	if ((r = pthread_create(&tid, NULL, pthread_func, NULL)) < 0)
	    fatal_sys("pthread_create() error", r);
}

void *
pthread_func(void * arg)
{
	int		r;

	sleep(3);	/* let parent continue first */

	if (sys_fddata == TRUE)
	    syscall(sysnum);

	printf("pthread_func() testing write to file %s\n", file);
	test_write();

	pthread_exit(NULL);
}

void 
fatal_sys(char *s, int e)
{
	fprintf(stderr, "%s %s\n", s, strerror(e));
	exit(-1);
}

void
fatal(char *s)
{
	fprintf(stderr, "%s\n", s);
	exit(-1);
}
