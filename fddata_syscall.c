/* fddata() syscall - take no args
 *
 * Outputs to log facility and console the values for the following 
 * fields in calling proc's kernel structs:
 *      1. p_pid
 *      2. p_numthreads
 *      3. fd_refcnt
 *      4. fd_nfiles
 *      5. address of fd_files
 *      6. address(es) of any descriptor tables in procs free list.
 *
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>

#define NDFILE 20
#define NDENTRIES       (NDSLOTSIZE * __CHAR_BIT)
#define NDSLOTSIZE      sizeof(NDSLOTTYPE)
#define NDSLOTS(x)      (((x) + NDENTRIES - 1) / NDENTRIES)

struct freetable {
	struct fdescenttbl *ft_table;
	SLIST_ENTRY(freetable) ft_next;
};

struct fdescenttbl0 { 
	int     fdt_nfiles;
	struct  filedescent fdt_ofiles[NDFILE];
};

struct filedesc0 {
	struct filedesc         fd_fd;
	SLIST_HEAD(, freetable) fd_free;
	struct  fdescenttbl0    fd_dfiles;
	NDSLOTTYPE fd_dmap[NDSLOTS(NDFILE)];
};

static int 
fddata(struct thread *td, void *args)
{
	struct proc 		*p;
	struct filedesc		*fdesc;
	struct filedesc0	*fdp0;
	struct freetable	*ft;

	p = td->td_proc;
	fdesc = p->p_fd;

	FILEDESC_SLOCK(fdesc);
	PROC_LOCK(p);

	printf("fddata() - PID: %d, p_numthreads: %d, fd_refcnt %d, fd_nfiles: %d, *fd_files addr: %p\n", \
	    p->p_pid, p->p_numthreads, fdesc->fd_refcnt, fdesc->fd_nfiles, fdesc->fd_files);

	fdp0 = (struct filedesc0 *) p->p_fd;

	SLIST_FOREACH(ft, &fdp0->fd_free, ft_next)
	    printf("fddata() - PID: %d, Free Table(s) - *ft_table addr: %p\n", \
	    p->p_pid, ft->ft_table);

	PROC_UNLOCK(p);
	FILEDESC_SUNLOCK(p->p_fd);

	return(0);
}

struct fddata_args {
	int	dummy;
};

static struct sysent fddata_sysent = {
	0,
	fddata
};

static int offset = NO_SYSCALL;

static int
load(struct module *module, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		uprintf("Module loaded.  Syscall offset: %d\n", offset);
		break;

	case MOD_UNLOAD:
		uprintf("Module unloaded\n");
		break;
	
	default:
		error = EOPNOTSUPP;
		break;
	}

	return(error);
}

SYSCALL_MODULE(fddata, &offset, &fddata_sysent, load, NULL);
