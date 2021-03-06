/*-
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_proc.c	8.7 (Berkeley) 2/14/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"
#include "opt_ddb.h"
#include "opt_kdtrace.h"
#include "opt_ktrace.h"
#include "opt_kstack_pages.h"
#include "opt_stack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/loginclass.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/sbuf.h>
#include <sys/sysent.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/stack.h>
#include <sys/sysctl.h>
#include <sys/filedesc.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/sdt.h>
#include <sys/sx.h>
#include <sys/user.h>
#include <sys/jail.h>
#include <sys/vnode.h>
#include <sys/eventhandler.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/uma.h>

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_util.h>
#endif

SDT_PROVIDER_DEFINE(proc);
SDT_PROBE_DEFINE(proc, kernel, ctor, entry, entry);
SDT_PROBE_ARGTYPE(proc, kernel, ctor, entry, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, entry, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, entry, 2, "void *");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, entry, 3, "int");
SDT_PROBE_DEFINE(proc, kernel, ctor, return, return);
SDT_PROBE_ARGTYPE(proc, kernel, ctor, return, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, return, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, return, 2, "void *");
SDT_PROBE_ARGTYPE(proc, kernel, ctor, return, 3, "int");
SDT_PROBE_DEFINE(proc, kernel, dtor, entry, entry);
SDT_PROBE_ARGTYPE(proc, kernel, dtor, entry, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, dtor, entry, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, dtor, entry, 2, "void *");
SDT_PROBE_ARGTYPE(proc, kernel, dtor, entry, 3, "struct thread *");
SDT_PROBE_DEFINE(proc, kernel, dtor, return, return);
SDT_PROBE_ARGTYPE(proc, kernel, dtor, return, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, dtor, return, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, dtor, return, 2, "void *");
SDT_PROBE_DEFINE(proc, kernel, init, entry, entry);
SDT_PROBE_ARGTYPE(proc, kernel, init, entry, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, init, entry, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, init, entry, 2, "int");
SDT_PROBE_DEFINE(proc, kernel, init, return, return);
SDT_PROBE_ARGTYPE(proc, kernel, init, return, 0, "struct proc *");
SDT_PROBE_ARGTYPE(proc, kernel, init, return, 1, "int");
SDT_PROBE_ARGTYPE(proc, kernel, init, return, 2, "int");

MALLOC_DEFINE(M_PGRP, "pgrp", "process group header");
MALLOC_DEFINE(M_SESSION, "session", "session header");
static MALLOC_DEFINE(M_PROC, "proc", "Proc structures");
MALLOC_DEFINE(M_SUBPROC, "subproc", "Proc sub-structures");

static void doenterpgrp(struct proc *, struct pgrp *);
static void orphanpg(struct pgrp *pg);
static void fill_kinfo_aggregate(struct proc *p, struct kinfo_proc *kp);
static void fill_kinfo_proc_only(struct proc *p, struct kinfo_proc *kp);
static void fill_kinfo_thread(struct thread *td, struct kinfo_proc *kp,
    int preferthread);
static void pgadjustjobc(struct pgrp *pgrp, int entering);
static void pgdelete(struct pgrp *);
static int proc_ctor(void *mem, int size, void *arg, int flags);
static void proc_dtor(void *mem, int size, void *arg);
static int proc_init(void *mem, int size, int flags);
static void proc_fini(void *mem, int size);
static void pargs_free(struct pargs *pa);

/*
 * Other process lists
 */
struct pidhashhead *pidhashtbl;
u_long pidhash;
struct pgrphashhead *pgrphashtbl;
u_long pgrphash;
struct proclist allproc;
struct proclist zombproc;
struct sx allproc_lock;
struct sx proctree_lock;
struct mtx ppeers_lock;
uma_zone_t proc_zone;

int kstack_pages = KSTACK_PAGES;
SYSCTL_INT(_kern, OID_AUTO, kstack_pages, CTLFLAG_RD, &kstack_pages, 0,
    "Kernel stack size in pages");

CTASSERT(sizeof(struct kinfo_proc) == KINFO_PROC_SIZE);
#ifdef COMPAT_FREEBSD32
CTASSERT(sizeof(struct kinfo_proc32) == KINFO_PROC32_SIZE);
#endif

/*
 * Initialize global process hashing structures.
 */
void
procinit()
{

	sx_init(&allproc_lock, "allproc");
	sx_init(&proctree_lock, "proctree");
	mtx_init(&ppeers_lock, "p_peers", NULL, MTX_DEF);
	LIST_INIT(&allproc);
	LIST_INIT(&zombproc);
	pidhashtbl = hashinit(maxproc / 4, M_PROC, &pidhash);
	pgrphashtbl = hashinit(maxproc / 4, M_PROC, &pgrphash);
	proc_zone = uma_zcreate("PROC", sched_sizeof_proc(),
	    proc_ctor, proc_dtor, proc_init, proc_fini,
	    UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	uihashinit();
}

/*
 * Prepare a proc for use.
 */
static int
proc_ctor(void *mem, int size, void *arg, int flags)
{
	struct proc *p;

	p = (struct proc *)mem;
	SDT_PROBE(proc, kernel, ctor , entry, p, size, arg, flags, 0);
	EVENTHANDLER_INVOKE(process_ctor, p);
	SDT_PROBE(proc, kernel, ctor , return, p, size, arg, flags, 0);
	return (0);
}

/*
 * Reclaim a proc after use.
 */
static void
proc_dtor(void *mem, int size, void *arg)
{
	struct proc *p;
	struct thread *td;

	/* INVARIANTS checks go here */
	p = (struct proc *)mem;
	td = FIRST_THREAD_IN_PROC(p);
	SDT_PROBE(proc, kernel, dtor, entry, p, size, arg, td, 0);
	if (td != NULL) {
#ifdef INVARIANTS
		KASSERT((p->p_numthreads == 1),
		    ("bad number of threads in exiting process"));
		KASSERT(STAILQ_EMPTY(&p->p_ktr), ("proc_dtor: non-empty p_ktr"));
#endif
		/* Free all OSD associated to this thread. */
		osd_thread_exit(td);
	}
	EVENTHANDLER_INVOKE(process_dtor, p);
	if (p->p_ksi != NULL)
		KASSERT(! KSI_ONQ(p->p_ksi), ("SIGCHLD queue"));
	SDT_PROBE(proc, kernel, dtor, return, p, size, arg, 0, 0);
}

/*
 * Initialize type-stable parts of a proc (when newly created).
 */
static int
proc_init(void *mem, int size, int flags)
{
	struct proc *p;

	p = (struct proc *)mem;
	SDT_PROBE(proc, kernel, init, entry, p, size, flags, 0, 0);
	p->p_sched = (struct p_sched *)&p[1];
	bzero(&p->p_mtx, sizeof(struct mtx));
	mtx_init(&p->p_mtx, "process lock", NULL, MTX_DEF | MTX_DUPOK);
	mtx_init(&p->p_slock, "process slock", NULL, MTX_SPIN | MTX_RECURSE);
	cv_init(&p->p_pwait, "ppwait");
	cv_init(&p->p_dbgwait, "dbgwait");
	TAILQ_INIT(&p->p_threads);	     /* all threads in proc */
	EVENTHANDLER_INVOKE(process_init, p);
	p->p_stats = pstats_alloc();
	SDT_PROBE(proc, kernel, init, return, p, size, flags, 0, 0);
	return (0);
}

/*
 * UMA should ensure that this function is never called.
 * Freeing a proc structure would violate type stability.
 */
static void
proc_fini(void *mem, int size)
{
#ifdef notnow
	struct proc *p;

	p = (struct proc *)mem;
	EVENTHANDLER_INVOKE(process_fini, p);
	pstats_free(p->p_stats);
	thread_free(FIRST_THREAD_IN_PROC(p));
	mtx_destroy(&p->p_mtx);
	if (p->p_ksi != NULL)
		ksiginfo_free(p->p_ksi);
#else
	panic("proc reclaimed");
#endif
}

/*
 * Is p an inferior of the current process?
 */
int
inferior(p)
	register struct proc *p;
{

	sx_assert(&proctree_lock, SX_LOCKED);
	for (; p != curproc; p = p->p_pptr)
		if (p->p_pid == 0)
			return (0);
	return (1);
}

/*
 * Locate a process by number; return only "live" processes -- i.e., neither
 * zombies nor newly born but incompletely initialized processes.  By not
 * returning processes in the PRS_NEW state, we allow callers to avoid
 * testing for that condition to avoid dereferencing p_ucred, et al.
 */
struct proc *
pfind(pid)
	register pid_t pid;
{
	register struct proc *p;

	sx_slock(&allproc_lock);
	LIST_FOREACH(p, PIDHASH(pid), p_hash)
		if (p->p_pid == pid) {
			PROC_LOCK(p);
			if (p->p_state == PRS_NEW) {
				PROC_UNLOCK(p);
				p = NULL;
			}
			break;
		}
	sx_sunlock(&allproc_lock);
	return (p);
}

/*
 * Locate a process group by number.
 * The caller must hold proctree_lock.
 */
struct pgrp *
pgfind(pgid)
	register pid_t pgid;
{
	register struct pgrp *pgrp;

	sx_assert(&proctree_lock, SX_LOCKED);

	LIST_FOREACH(pgrp, PGRPHASH(pgid), pg_hash) {
		if (pgrp->pg_id == pgid) {
			PGRP_LOCK(pgrp);
			return (pgrp);
		}
	}
	return (NULL);
}

/*
 * Create a new process group.
 * pgid must be equal to the pid of p.
 * Begin a new session if required.
 */
int
enterpgrp(p, pgid, pgrp, sess)
	register struct proc *p;
	pid_t pgid;
	struct pgrp *pgrp;
	struct session *sess;
{
	struct pgrp *pgrp2;

	sx_assert(&proctree_lock, SX_XLOCKED);

	KASSERT(pgrp != NULL, ("enterpgrp: pgrp == NULL"));
	KASSERT(p->p_pid == pgid,
	    ("enterpgrp: new pgrp and pid != pgid"));

	pgrp2 = pgfind(pgid);

	KASSERT(pgrp2 == NULL,
	    ("enterpgrp: pgrp with pgid exists"));
	KASSERT(!SESS_LEADER(p),
	    ("enterpgrp: session leader attempted setpgrp"));

	mtx_init(&pgrp->pg_mtx, "process group", NULL, MTX_DEF | MTX_DUPOK);

	if (sess != NULL) {
		/*
		 * new session
		 */
		mtx_init(&sess->s_mtx, "session", NULL, MTX_DEF);
		PROC_LOCK(p);
		p->p_flag &= ~P_CONTROLT;
		PROC_UNLOCK(p);
		PGRP_LOCK(pgrp);
		sess->s_leader = p;
		sess->s_sid = p->p_pid;
		refcount_init(&sess->s_count, 1);
		sess->s_ttyvp = NULL;
		sess->s_ttydp = NULL;
		sess->s_ttyp = NULL;
		bcopy(p->p_session->s_login, sess->s_login,
			    sizeof(sess->s_login));
		pgrp->pg_session = sess;
		KASSERT(p == curproc,
		    ("enterpgrp: mksession and p != curproc"));
	} else {
		pgrp->pg_session = p->p_session;
		sess_hold(pgrp->pg_session);
		PGRP_LOCK(pgrp);
	}
	pgrp->pg_id = pgid;
	LIST_INIT(&pgrp->pg_members);

	/*
	 * As we have an exclusive lock of proctree_lock,
	 * this should not deadlock.
	 */
	LIST_INSERT_HEAD(PGRPHASH(pgid), pgrp, pg_hash);
	pgrp->pg_jobc = 0;
	SLIST_INIT(&pgrp->pg_sigiolst);
	PGRP_UNLOCK(pgrp);

	doenterpgrp(p, pgrp);

	return (0);
}

/*
 * Move p to an existing process group
 */
int
enterthispgrp(p, pgrp)
	register struct proc *p;
	struct pgrp *pgrp;
{

	sx_assert(&proctree_lock, SX_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_NOTOWNED);
	PGRP_LOCK_ASSERT(pgrp, MA_NOTOWNED);
	PGRP_LOCK_ASSERT(p->p_pgrp, MA_NOTOWNED);
	SESS_LOCK_ASSERT(p->p_session, MA_NOTOWNED);
	KASSERT(pgrp->pg_session == p->p_session,
		("%s: pgrp's session %p, p->p_session %p.\n",
		__func__,
		pgrp->pg_session,
		p->p_session));
	KASSERT(pgrp != p->p_pgrp,
		("%s: p belongs to pgrp.", __func__));

	doenterpgrp(p, pgrp);

	return (0);
}

/*
 * Move p to a process group
 */
static void
doenterpgrp(p, pgrp)
	struct proc *p;
	struct pgrp *pgrp;
{
	struct pgrp *savepgrp;

	sx_assert(&proctree_lock, SX_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_NOTOWNED);
	PGRP_LOCK_ASSERT(pgrp, MA_NOTOWNED);
	PGRP_LOCK_ASSERT(p->p_pgrp, MA_NOTOWNED);
	SESS_LOCK_ASSERT(p->p_session, MA_NOTOWNED);

	savepgrp = p->p_pgrp;

	/*
	 * Adjust eligibility of affected pgrps to participate in job control.
	 * Increment eligibility counts before decrementing, otherwise we
	 * could reach 0 spuriously during the first call.
	 */
	fixjobc(p, pgrp, 1);
	fixjobc(p, p->p_pgrp, 0);

	PGRP_LOCK(pgrp);
	PGRP_LOCK(savepgrp);
	PROC_LOCK(p);
	LIST_REMOVE(p, p_pglist);
	p->p_pgrp = pgrp;
	PROC_UNLOCK(p);
	LIST_INSERT_HEAD(&pgrp->pg_members, p, p_pglist);
	PGRP_UNLOCK(savepgrp);
	PGRP_UNLOCK(pgrp);
	if (LIST_EMPTY(&savepgrp->pg_members))
		pgdelete(savepgrp);
}

/*
 * remove process from process group
 */
int
leavepgrp(p)
	register struct proc *p;
{
	struct pgrp *savepgrp;

	sx_assert(&proctree_lock, SX_XLOCKED);
	savepgrp = p->p_pgrp;
	PGRP_LOCK(savepgrp);
	PROC_LOCK(p);
	LIST_REMOVE(p, p_pglist);
	p->p_pgrp = NULL;
	PROC_UNLOCK(p);
	PGRP_UNLOCK(savepgrp);
	if (LIST_EMPTY(&savepgrp->pg_members))
		pgdelete(savepgrp);
	return (0);
}

/*
 * delete a process group
 */
static void
pgdelete(pgrp)
	register struct pgrp *pgrp;
{
	struct session *savesess;
	struct tty *tp;

	sx_assert(&proctree_lock, SX_XLOCKED);
	PGRP_LOCK_ASSERT(pgrp, MA_NOTOWNED);
	SESS_LOCK_ASSERT(pgrp->pg_session, MA_NOTOWNED);

	/*
	 * Reset any sigio structures pointing to us as a result of
	 * F_SETOWN with our pgid.
	 */
	funsetownlst(&pgrp->pg_sigiolst);

	PGRP_LOCK(pgrp);
	tp = pgrp->pg_session->s_ttyp;
	LIST_REMOVE(pgrp, pg_hash);
	savesess = pgrp->pg_session;
	PGRP_UNLOCK(pgrp);

	/* Remove the reference to the pgrp before deallocating it. */
	if (tp != NULL) {
		tty_lock(tp);
		tty_rel_pgrp(tp, pgrp);
	}

	mtx_destroy(&pgrp->pg_mtx);
	free(pgrp, M_PGRP);
	sess_release(savesess);
}

static void
pgadjustjobc(pgrp, entering)
	struct pgrp *pgrp;
	int entering;
{

	PGRP_LOCK(pgrp);
	if (entering)
		pgrp->pg_jobc++;
	else {
		--pgrp->pg_jobc;
		if (pgrp->pg_jobc == 0)
			orphanpg(pgrp);
	}
	PGRP_UNLOCK(pgrp);
}

/*
 * Adjust pgrp jobc counters when specified process changes process group.
 * We count the number of processes in each process group that "qualify"
 * the group for terminal job control (those with a parent in a different
 * process group of the same session).  If that count reaches zero, the
 * process group becomes orphaned.  Check both the specified process'
 * process group and that of its children.
 * entering == 0 => p is leaving specified group.
 * entering == 1 => p is entering specified group.
 */
void
fixjobc(p, pgrp, entering)
	register struct proc *p;
	register struct pgrp *pgrp;
	int entering;
{
	register struct pgrp *hispgrp;
	register struct session *mysession;

	sx_assert(&proctree_lock, SX_LOCKED);
	PROC_LOCK_ASSERT(p, MA_NOTOWNED);
	PGRP_LOCK_ASSERT(pgrp, MA_NOTOWNED);
	SESS_LOCK_ASSERT(pgrp->pg_session, MA_NOTOWNED);

	/*
	 * Check p's parent to see whether p qualifies its own process
	 * group; if so, adjust count for p's process group.
	 */
	mysession = pgrp->pg_session;
	if ((hispgrp = p->p_pptr->p_pgrp) != pgrp &&
	    hispgrp->pg_session == mysession)
		pgadjustjobc(pgrp, entering);

	/*
	 * Check this process' children to see whether they qualify
	 * their process groups; if so, adjust counts for children's
	 * process groups.
	 */
	LIST_FOREACH(p, &p->p_children, p_sibling) {
		hispgrp = p->p_pgrp;
		if (hispgrp == pgrp ||
		    hispgrp->pg_session != mysession)
			continue;
		PROC_LOCK(p);
		if (p->p_state == PRS_ZOMBIE) {
			PROC_UNLOCK(p);
			continue;
		}
		PROC_UNLOCK(p);
		pgadjustjobc(hispgrp, entering);
	}
}

/*
 * A process group has become orphaned;
 * if there are any stopped processes in the group,
 * hang-up all process in that group.
 */
static void
orphanpg(pg)
	struct pgrp *pg;
{
	register struct proc *p;

	PGRP_LOCK_ASSERT(pg, MA_OWNED);

	LIST_FOREACH(p, &pg->pg_members, p_pglist) {
		PROC_LOCK(p);
		if (P_SHOULDSTOP(p)) {
			PROC_UNLOCK(p);
			LIST_FOREACH(p, &pg->pg_members, p_pglist) {
				PROC_LOCK(p);
				psignal(p, SIGHUP);
				psignal(p, SIGCONT);
				PROC_UNLOCK(p);
			}
			return;
		}
		PROC_UNLOCK(p);
	}
}

void
sess_hold(struct session *s)
{

	refcount_acquire(&s->s_count);
}

void
sess_release(struct session *s)
{

	if (refcount_release(&s->s_count)) {
		if (s->s_ttyp != NULL) {
			tty_lock(s->s_ttyp);
			tty_rel_sess(s->s_ttyp, s);
		}
		mtx_destroy(&s->s_mtx);
		free(s, M_SESSION);
	}
}

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(pgrpdump, pgrpdump)
{
	register struct pgrp *pgrp;
	register struct proc *p;
	register int i;

	for (i = 0; i <= pgrphash; i++) {
		if (!LIST_EMPTY(&pgrphashtbl[i])) {
			printf("\tindx %d\n", i);
			LIST_FOREACH(pgrp, &pgrphashtbl[i], pg_hash) {
				printf(
			"\tpgrp %p, pgid %ld, sess %p, sesscnt %d, mem %p\n",
				    (void *)pgrp, (long)pgrp->pg_id,
				    (void *)pgrp->pg_session,
				    pgrp->pg_session->s_count,
				    (void *)LIST_FIRST(&pgrp->pg_members));
				LIST_FOREACH(p, &pgrp->pg_members, p_pglist) {
					printf("\t\tpid %ld addr %p pgrp %p\n", 
					    (long)p->p_pid, (void *)p,
					    (void *)p->p_pgrp);
				}
			}
		}
	}
}
#endif /* DDB */

/*
 * Calculate the kinfo_proc members which contain process-wide
 * informations.
 * Must be called with the target process locked.
 */
static void
fill_kinfo_aggregate(struct proc *p, struct kinfo_proc *kp)
{
	struct thread *td;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	kp->ki_estcpu = 0;
	kp->ki_pctcpu = 0;
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		kp->ki_pctcpu += sched_pctcpu(td);
		kp->ki_estcpu += td->td_estcpu;
		thread_unlock(td);
	}
}

/*
 * Clear kinfo_proc and fill in any information that is common
 * to all threads in the process.
 * Must be called with the target process locked.
 */
static void
fill_kinfo_proc_only(struct proc *p, struct kinfo_proc *kp)
{
	struct thread *td0;
	struct tty *tp;
	struct session *sp;
	struct ucred *cred;
	struct sigacts *ps;

	PROC_LOCK_ASSERT(p, MA_OWNED);
	bzero(kp, sizeof(*kp));

	kp->ki_structsize = sizeof(*kp);
	kp->ki_paddr = p;
	kp->ki_addr =/* p->p_addr; */0; /* XXX */
	kp->ki_args = p->p_args;
	kp->ki_textvp = p->p_textvp;
#ifdef KTRACE
	kp->ki_tracep = p->p_tracevp;
	kp->ki_traceflag = p->p_traceflag;
#endif
	kp->ki_fd = p->p_fd;
	kp->ki_vmspace = p->p_vmspace;
	kp->ki_flag = p->p_flag;
	cred = p->p_ucred;
	if (cred) {
		kp->ki_uid = cred->cr_uid;
		kp->ki_ruid = cred->cr_ruid;
		kp->ki_svuid = cred->cr_svuid;
		kp->ki_cr_flags = 0;
		if (cred->cr_flags & CRED_FLAG_CAPMODE)
			kp->ki_cr_flags |= KI_CRF_CAPABILITY_MODE;
		/* XXX bde doesn't like KI_NGROUPS */
		if (cred->cr_ngroups > KI_NGROUPS) {
			kp->ki_ngroups = KI_NGROUPS;
			kp->ki_cr_flags |= KI_CRF_GRP_OVERFLOW;
		} else
			kp->ki_ngroups = cred->cr_ngroups;
		bcopy(cred->cr_groups, kp->ki_groups,
		    kp->ki_ngroups * sizeof(gid_t));
		kp->ki_rgid = cred->cr_rgid;
		kp->ki_svgid = cred->cr_svgid;
		/* If jailed(cred), emulate the old P_JAILED flag. */
		if (jailed(cred)) {
			kp->ki_flag |= P_JAILED;
			/* If inside the jail, use 0 as a jail ID. */
			if (cred->cr_prison != curthread->td_ucred->cr_prison)
				kp->ki_jid = cred->cr_prison->pr_id;
		}
		strlcpy(kp->ki_loginclass, cred->cr_loginclass->lc_name,
		    sizeof(kp->ki_loginclass));
	}
	ps = p->p_sigacts;
	if (ps) {
		mtx_lock(&ps->ps_mtx);
		kp->ki_sigignore = ps->ps_sigignore;
		kp->ki_sigcatch = ps->ps_sigcatch;
		mtx_unlock(&ps->ps_mtx);
	}
	if (p->p_state != PRS_NEW &&
	    p->p_state != PRS_ZOMBIE &&
	    p->p_vmspace != NULL) {
		struct vmspace *vm = p->p_vmspace;

		kp->ki_size = vm->vm_map.size;
		kp->ki_rssize = vmspace_resident_count(vm); /*XXX*/
		FOREACH_THREAD_IN_PROC(p, td0) {
			if (!TD_IS_SWAPPED(td0))
				kp->ki_rssize += td0->td_kstack_pages;
		}
		kp->ki_swrss = vm->vm_swrss;
		kp->ki_tsize = vm->vm_tsize;
		kp->ki_dsize = vm->vm_dsize;
		kp->ki_ssize = vm->vm_ssize;
	} else if (p->p_state == PRS_ZOMBIE)
		kp->ki_stat = SZOMB;
	if (kp->ki_flag & P_INMEM)
		kp->ki_sflag = PS_INMEM;
	else
		kp->ki_sflag = 0;
	/* Calculate legacy swtime as seconds since 'swtick'. */
	kp->ki_swtime = (ticks - p->p_swtick) / hz;
	kp->ki_pid = p->p_pid;
	kp->ki_nice = p->p_nice;
	kp->ki_start = p->p_stats->p_start;
	timevaladd(&kp->ki_start, &boottime);
	PROC_SLOCK(p);
	rufetch(p, &kp->ki_rusage);
	kp->ki_runtime = cputick2usec(p->p_rux.rux_runtime);
	calcru(p, &kp->ki_rusage.ru_utime, &kp->ki_rusage.ru_stime);
	PROC_SUNLOCK(p);
	calccru(p, &kp->ki_childutime, &kp->ki_childstime);
	/* Some callers want child times in a single value. */
	kp->ki_childtime = kp->ki_childstime;
	timevaladd(&kp->ki_childtime, &kp->ki_childutime);

	tp = NULL;
	if (p->p_pgrp) {
		kp->ki_pgid = p->p_pgrp->pg_id;
		kp->ki_jobc = p->p_pgrp->pg_jobc;
		sp = p->p_pgrp->pg_session;

		if (sp != NULL) {
			kp->ki_sid = sp->s_sid;
			SESS_LOCK(sp);
			strlcpy(kp->ki_login, sp->s_login,
			    sizeof(kp->ki_login));
			if (sp->s_ttyvp)
				kp->ki_kiflag |= KI_CTTY;
			if (SESS_LEADER(p))
				kp->ki_kiflag |= KI_SLEADER;
			/* XXX proctree_lock */
			tp = sp->s_ttyp;
			SESS_UNLOCK(sp);
		}
	}
	if ((p->p_flag & P_CONTROLT) && tp != NULL) {
		kp->ki_tdev = tty_udev(tp);
		kp->ki_tpgid = tp->t_pgrp ? tp->t_pgrp->pg_id : NO_PID;
		if (tp->t_session)
			kp->ki_tsid = tp->t_session->s_sid;
	} else
		kp->ki_tdev = NODEV;
	if (p->p_comm[0] != '\0')
		strlcpy(kp->ki_comm, p->p_comm, sizeof(kp->ki_comm));
	if (p->p_sysent && p->p_sysent->sv_name != NULL &&
	    p->p_sysent->sv_name[0] != '\0')
		strlcpy(kp->ki_emul, p->p_sysent->sv_name, sizeof(kp->ki_emul));
	kp->ki_siglist = p->p_siglist;
	kp->ki_xstat = p->p_xstat;
	kp->ki_acflag = p->p_acflag;
	kp->ki_lock = p->p_lock;
	if (p->p_pptr)
		kp->ki_ppid = p->p_pptr->p_pid;
}

/*
 * Fill in information that is thread specific.  Must be called with
 * target process locked.  If 'preferthread' is set, overwrite certain
 * process-related fields that are maintained for both threads and
 * processes.
 */
static void
fill_kinfo_thread(struct thread *td, struct kinfo_proc *kp, int preferthread)
{
	struct proc *p;

	p = td->td_proc;
	kp->ki_tdaddr = td;
	PROC_LOCK_ASSERT(p, MA_OWNED);

	if (preferthread)
		PROC_SLOCK(p);
	thread_lock(td);
	if (td->td_wmesg != NULL)
		strlcpy(kp->ki_wmesg, td->td_wmesg, sizeof(kp->ki_wmesg));
	else
		bzero(kp->ki_wmesg, sizeof(kp->ki_wmesg));
	strlcpy(kp->ki_tdname, td->td_name, sizeof(kp->ki_tdname));
	if (TD_ON_LOCK(td)) {
		kp->ki_kiflag |= KI_LOCKBLOCK;
		strlcpy(kp->ki_lockname, td->td_lockname,
		    sizeof(kp->ki_lockname));
	} else {
		kp->ki_kiflag &= ~KI_LOCKBLOCK;
		bzero(kp->ki_lockname, sizeof(kp->ki_lockname));
	}

	if (p->p_state == PRS_NORMAL) { /* approximate. */
		if (TD_ON_RUNQ(td) ||
		    TD_CAN_RUN(td) ||
		    TD_IS_RUNNING(td)) {
			kp->ki_stat = SRUN;
		} else if (P_SHOULDSTOP(p)) {
			kp->ki_stat = SSTOP;
		} else if (TD_IS_SLEEPING(td)) {
			kp->ki_stat = SSLEEP;
		} else if (TD_ON_LOCK(td)) {
			kp->ki_stat = SLOCK;
		} else {
			kp->ki_stat = SWAIT;
		}
	} else if (p->p_state == PRS_ZOMBIE) {
		kp->ki_stat = SZOMB;
	} else {
		kp->ki_stat = SIDL;
	}

	/* Things in the thread */
	kp->ki_wchan = td->td_wchan;
	kp->ki_pri.pri_level = td->td_priority;
	kp->ki_pri.pri_native = td->td_base_pri;
	kp->ki_lastcpu = td->td_lastcpu;
	kp->ki_oncpu = td->td_oncpu;
	kp->ki_tdflags = td->td_flags;
	kp->ki_tid = td->td_tid;
	kp->ki_numthreads = p->p_numthreads;
	kp->ki_pcb = td->td_pcb;
	kp->ki_kstack = (void *)td->td_kstack;
	kp->ki_slptime = (ticks - td->td_slptick) / hz;
	kp->ki_pri.pri_class = td->td_pri_class;
	kp->ki_pri.pri_user = td->td_user_pri;

	if (preferthread) {
		rufetchtd(td, &kp->ki_rusage);
		kp->ki_runtime = cputick2usec(td->td_rux.rux_runtime);
		kp->ki_pctcpu = sched_pctcpu(td);
		kp->ki_estcpu = td->td_estcpu;
	}

	/* We can't get this anymore but ps etc never used it anyway. */
	kp->ki_rqindex = 0;

	if (preferthread)
		kp->ki_siglist = td->td_siglist;
	kp->ki_sigmask = td->td_sigmask;
	thread_unlock(td);
	if (preferthread)
		PROC_SUNLOCK(p);
}

/*
 * Fill in a kinfo_proc structure for the specified process.
 * Must be called with the target process locked.
 */
void
fill_kinfo_proc(struct proc *p, struct kinfo_proc *kp)
{

	MPASS(FIRST_THREAD_IN_PROC(p) != NULL);

	fill_kinfo_proc_only(p, kp);
	fill_kinfo_thread(FIRST_THREAD_IN_PROC(p), kp, 0);
	fill_kinfo_aggregate(p, kp);
}

struct pstats *
pstats_alloc(void)
{

	return (malloc(sizeof(struct pstats), M_SUBPROC, M_ZERO|M_WAITOK));
}

/*
 * Copy parts of p_stats; zero the rest of p_stats (statistics).
 */
void
pstats_fork(struct pstats *src, struct pstats *dst)
{

	bzero(&dst->pstat_startzero,
	    __rangeof(struct pstats, pstat_startzero, pstat_endzero));
	bcopy(&src->pstat_startcopy, &dst->pstat_startcopy,
	    __rangeof(struct pstats, pstat_startcopy, pstat_endcopy));
}

void
pstats_free(struct pstats *ps)
{

	free(ps, M_SUBPROC);
}

/*
 * Locate a zombie process by number
 */
struct proc *
zpfind(pid_t pid)
{
	struct proc *p;

	sx_slock(&allproc_lock);
	LIST_FOREACH(p, &zombproc, p_list)
		if (p->p_pid == pid) {
			PROC_LOCK(p);
			break;
		}
	sx_sunlock(&allproc_lock);
	return (p);
}

#define KERN_PROC_ZOMBMASK	0x3
#define KERN_PROC_NOTHREADS	0x4

#ifdef COMPAT_FREEBSD32

/*
 * This function is typically used to copy out the kernel address, so
 * it can be replaced by assignment of zero.
 */
static inline uint32_t
ptr32_trim(void *ptr)
{
	uintptr_t uptr;

	uptr = (uintptr_t)ptr;
	return ((uptr > UINT_MAX) ? 0 : uptr);
}

#define PTRTRIM_CP(src,dst,fld) \
	do { (dst).fld = ptr32_trim((src).fld); } while (0)

static void
freebsd32_kinfo_proc_out(const struct kinfo_proc *ki, struct kinfo_proc32 *ki32)
{
	int i;

	bzero(ki32, sizeof(struct kinfo_proc32));
	ki32->ki_structsize = sizeof(struct kinfo_proc32);
	CP(*ki, *ki32, ki_layout);
	PTRTRIM_CP(*ki, *ki32, ki_args);
	PTRTRIM_CP(*ki, *ki32, ki_paddr);
	PTRTRIM_CP(*ki, *ki32, ki_addr);
	PTRTRIM_CP(*ki, *ki32, ki_tracep);
	PTRTRIM_CP(*ki, *ki32, ki_textvp);
	PTRTRIM_CP(*ki, *ki32, ki_fd);
	PTRTRIM_CP(*ki, *ki32, ki_vmspace);
	PTRTRIM_CP(*ki, *ki32, ki_wchan);
	CP(*ki, *ki32, ki_pid);
	CP(*ki, *ki32, ki_ppid);
	CP(*ki, *ki32, ki_pgid);
	CP(*ki, *ki32, ki_tpgid);
	CP(*ki, *ki32, ki_sid);
	CP(*ki, *ki32, ki_tsid);
	CP(*ki, *ki32, ki_jobc);
	CP(*ki, *ki32, ki_tdev);
	CP(*ki, *ki32, ki_siglist);
	CP(*ki, *ki32, ki_sigmask);
	CP(*ki, *ki32, ki_sigignore);
	CP(*ki, *ki32, ki_sigcatch);
	CP(*ki, *ki32, ki_uid);
	CP(*ki, *ki32, ki_ruid);
	CP(*ki, *ki32, ki_svuid);
	CP(*ki, *ki32, ki_rgid);
	CP(*ki, *ki32, ki_svgid);
	CP(*ki, *ki32, ki_ngroups);
	for (i = 0; i < KI_NGROUPS; i++)
		CP(*ki, *ki32, ki_groups[i]);
	CP(*ki, *ki32, ki_size);
	CP(*ki, *ki32, ki_rssize);
	CP(*ki, *ki32, ki_swrss);
	CP(*ki, *ki32, ki_tsize);
	CP(*ki, *ki32, ki_dsize);
	CP(*ki, *ki32, ki_ssize);
	CP(*ki, *ki32, ki_xstat);
	CP(*ki, *ki32, ki_acflag);
	CP(*ki, *ki32, ki_pctcpu);
	CP(*ki, *ki32, ki_estcpu);
	CP(*ki, *ki32, ki_slptime);
	CP(*ki, *ki32, ki_swtime);
	CP(*ki, *ki32, ki_runtime);
	TV_CP(*ki, *ki32, ki_start);
	TV_CP(*ki, *ki32, ki_childtime);
	CP(*ki, *ki32, ki_flag);
	CP(*ki, *ki32, ki_kiflag);
	CP(*ki, *ki32, ki_traceflag);
	CP(*ki, *ki32, ki_stat);
	CP(*ki, *ki32, ki_nice);
	CP(*ki, *ki32, ki_lock);
	CP(*ki, *ki32, ki_rqindex);
	CP(*ki, *ki32, ki_oncpu);
	CP(*ki, *ki32, ki_lastcpu);
	bcopy(ki->ki_tdname, ki32->ki_tdname, TDNAMLEN + 1);
	bcopy(ki->ki_wmesg, ki32->ki_wmesg, WMESGLEN + 1);
	bcopy(ki->ki_login, ki32->ki_login, LOGNAMELEN + 1);
	bcopy(ki->ki_lockname, ki32->ki_lockname, LOCKNAMELEN + 1);
	bcopy(ki->ki_comm, ki32->ki_comm, COMMLEN + 1);
	bcopy(ki->ki_emul, ki32->ki_emul, KI_EMULNAMELEN + 1);
	bcopy(ki->ki_loginclass, ki32->ki_loginclass, LOGINCLASSLEN + 1);
	CP(*ki, *ki32, ki_cr_flags);
	CP(*ki, *ki32, ki_jid);
	CP(*ki, *ki32, ki_numthreads);
	CP(*ki, *ki32, ki_tid);
	CP(*ki, *ki32, ki_pri);
	freebsd32_rusage_out(&ki->ki_rusage, &ki32->ki_rusage);
	freebsd32_rusage_out(&ki->ki_rusage_ch, &ki32->ki_rusage_ch);
	PTRTRIM_CP(*ki, *ki32, ki_pcb);
	PTRTRIM_CP(*ki, *ki32, ki_kstack);
	PTRTRIM_CP(*ki, *ki32, ki_udata);
	CP(*ki, *ki32, ki_sflag);
	CP(*ki, *ki32, ki_tdflags);
}

static int
sysctl_out_proc_copyout(struct kinfo_proc *ki, struct sysctl_req *req)
{
	struct kinfo_proc32 ki32;
	int error;

	if (req->flags & SCTL_MASK32) {
		freebsd32_kinfo_proc_out(ki, &ki32);
		error = SYSCTL_OUT(req, &ki32, sizeof(struct kinfo_proc32));
	} else
		error = SYSCTL_OUT(req, ki, sizeof(struct kinfo_proc));
	return (error);
}
#else
static int
sysctl_out_proc_copyout(struct kinfo_proc *ki, struct sysctl_req *req)
{

	return (SYSCTL_OUT(req, ki, sizeof(struct kinfo_proc)));
}
#endif

/*
 * Must be called with the process locked and will return with it unlocked.
 */
static int
sysctl_out_proc(struct proc *p, struct sysctl_req *req, int flags)
{
	struct thread *td;
	struct kinfo_proc kinfo_proc;
	int error = 0;
	struct proc *np;
	pid_t pid = p->p_pid;

	PROC_LOCK_ASSERT(p, MA_OWNED);
	MPASS(FIRST_THREAD_IN_PROC(p) != NULL);

	fill_kinfo_proc(p, &kinfo_proc);
	if (flags & KERN_PROC_NOTHREADS)
		error = sysctl_out_proc_copyout(&kinfo_proc, req);
	else {
		FOREACH_THREAD_IN_PROC(p, td) {
			fill_kinfo_thread(td, &kinfo_proc, 1);
			error = sysctl_out_proc_copyout(&kinfo_proc, req);
			if (error)
				break;
		}
	}
	PROC_UNLOCK(p);
	if (error)
		return (error);
	if (flags & KERN_PROC_ZOMBMASK)
		np = zpfind(pid);
	else {
		if (pid == 0)
			return (0);
		np = pfind(pid);
	}
	if (np == NULL)
		return (ESRCH);
	if (np != p) {
		PROC_UNLOCK(np);
		return (ESRCH);
	}
	PROC_UNLOCK(np);
	return (0);
}

static int
sysctl_kern_proc(SYSCTL_HANDLER_ARGS)
{
	int *name = (int*) arg1;
	u_int namelen = arg2;
	struct proc *p;
	int flags, doingzomb, oid_number;
	int error = 0;

	oid_number = oidp->oid_number;
	if (oid_number != KERN_PROC_ALL &&
	    (oid_number & KERN_PROC_INC_THREAD) == 0)
		flags = KERN_PROC_NOTHREADS;
	else {
		flags = 0;
		oid_number &= ~KERN_PROC_INC_THREAD;
	}
	if (oid_number == KERN_PROC_PID) {
		if (namelen != 1) 
			return (EINVAL);
		error = sysctl_wire_old_buffer(req, 0);
		if (error)
			return (error);		
		p = pfind((pid_t)name[0]);
		if (!p)
			return (ESRCH);
		if ((error = p_cansee(curthread, p))) {
			PROC_UNLOCK(p);
			return (error);
		}
		error = sysctl_out_proc(p, req, flags);
		return (error);
	}

	switch (oid_number) {
	case KERN_PROC_ALL:
		if (namelen != 0)
			return (EINVAL);
		break;
	case KERN_PROC_PROC:
		if (namelen != 0 && namelen != 1)
			return (EINVAL);
		break;
	default:
		if (namelen != 1)
			return (EINVAL);
		break;
	}
	
	if (!req->oldptr) {
		/* overestimate by 5 procs */
		error = SYSCTL_OUT(req, 0, sizeof (struct kinfo_proc) * 5);
		if (error)
			return (error);
	}
	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sx_slock(&allproc_lock);
	for (doingzomb=0 ; doingzomb < 2 ; doingzomb++) {
		if (!doingzomb)
			p = LIST_FIRST(&allproc);
		else
			p = LIST_FIRST(&zombproc);
		for (; p != 0; p = LIST_NEXT(p, p_list)) {
			/*
			 * Skip embryonic processes.
			 */
			PROC_LOCK(p);
			if (p->p_state == PRS_NEW) {
				PROC_UNLOCK(p);
				continue;
			}
			KASSERT(p->p_ucred != NULL,
			    ("process credential is NULL for non-NEW proc"));
			/*
			 * Show a user only appropriate processes.
			 */
			if (p_cansee(curthread, p)) {
				PROC_UNLOCK(p);
				continue;
			}
			/*
			 * TODO - make more efficient (see notes below).
			 * do by session.
			 */
			switch (oid_number) {

			case KERN_PROC_GID:
				if (p->p_ucred->cr_gid != (gid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_PGRP:
				/* could do this by traversing pgrp */
				if (p->p_pgrp == NULL ||
				    p->p_pgrp->pg_id != (pid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_RGID:
				if (p->p_ucred->cr_rgid != (gid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_SESSION:
				if (p->p_session == NULL ||
				    p->p_session->s_sid != (pid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_TTY:
				if ((p->p_flag & P_CONTROLT) == 0 ||
				    p->p_session == NULL) {
					PROC_UNLOCK(p);
					continue;
				}
				/* XXX proctree_lock */
				SESS_LOCK(p->p_session);
				if (p->p_session->s_ttyp == NULL ||
				    tty_udev(p->p_session->s_ttyp) != 
				    (dev_t)name[0]) {
					SESS_UNLOCK(p->p_session);
					PROC_UNLOCK(p);
					continue;
				}
				SESS_UNLOCK(p->p_session);
				break;

			case KERN_PROC_UID:
				if (p->p_ucred->cr_uid != (uid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_RUID:
				if (p->p_ucred->cr_ruid != (uid_t)name[0]) {
					PROC_UNLOCK(p);
					continue;
				}
				break;

			case KERN_PROC_PROC:
				break;

			default:
				break;

			}

			error = sysctl_out_proc(p, req, flags | doingzomb);
			if (error) {
				sx_sunlock(&allproc_lock);
				return (error);
			}
		}
	}
	sx_sunlock(&allproc_lock);
	return (0);
}

struct pargs *
pargs_alloc(int len)
{
	struct pargs *pa;

	pa = malloc(sizeof(struct pargs) + len, M_PARGS,
		M_WAITOK);
	refcount_init(&pa->ar_ref, 1);
	pa->ar_length = len;
	return (pa);
}

static void
pargs_free(struct pargs *pa)
{

	free(pa, M_PARGS);
}

void
pargs_hold(struct pargs *pa)
{

	if (pa == NULL)
		return;
	refcount_acquire(&pa->ar_ref);
}

void
pargs_drop(struct pargs *pa)
{

	if (pa == NULL)
		return;
	if (refcount_release(&pa->ar_ref))
		pargs_free(pa);
}

/*
 * This sysctl allows a process to retrieve the argument list or process
 * title for another process without groping around in the address space
 * of the other process.  It also allow a process to set its own "process 
 * title to a string of its own choice.
 */
static int
sysctl_kern_proc_args(SYSCTL_HANDLER_ARGS)
{
	int *name = (int*) arg1;
	u_int namelen = arg2;
	struct pargs *newpa, *pa;
	struct proc *p;
	int error = 0;

	if (namelen != 1) 
		return (EINVAL);

	p = pfind((pid_t)name[0]);
	if (!p)
		return (ESRCH);

	if ((error = p_cansee(curthread, p)) != 0) {
		PROC_UNLOCK(p);
		return (error);
	}

	if (req->newptr && curproc != p) {
		PROC_UNLOCK(p);
		return (EPERM);
	}

	pa = p->p_args;
	pargs_hold(pa);
	PROC_UNLOCK(p);
	if (pa != NULL)
		error = SYSCTL_OUT(req, pa->ar_args, pa->ar_length);
	pargs_drop(pa);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (req->newlen + sizeof(struct pargs) > ps_arg_cache_limit)
		return (ENOMEM);
	newpa = pargs_alloc(req->newlen);
	error = SYSCTL_IN(req, newpa->ar_args, req->newlen);
	if (error != 0) {
		pargs_free(newpa);
		return (error);
	}
	PROC_LOCK(p);
	pa = p->p_args;
	p->p_args = newpa;
	PROC_UNLOCK(p);
	pargs_drop(pa);
	return (0);
}

/*
 * This sysctl allows a process to retrieve the path of the executable for
 * itself or another process.
 */
static int
sysctl_kern_proc_pathname(SYSCTL_HANDLER_ARGS)
{
	pid_t *pidp = (pid_t *)arg1;
	unsigned int arglen = arg2;
	struct proc *p;
	struct vnode *vp;
	char *retbuf, *freebuf;
	int error, vfslocked;

	if (arglen != 1)
		return (EINVAL);
	if (*pidp == -1) {	/* -1 means this process */
		p = req->td->td_proc;
	} else {
		p = pfind(*pidp);
		if (p == NULL)
			return (ESRCH);
		if ((error = p_cansee(curthread, p)) != 0) {
			PROC_UNLOCK(p);
			return (error);
		}
	}

	vp = p->p_textvp;
	if (vp == NULL) {
		if (*pidp != -1)
			PROC_UNLOCK(p);
		return (0);
	}
	vref(vp);
	if (*pidp != -1)
		PROC_UNLOCK(p);
	error = vn_fullpath(req->td, vp, &retbuf, &freebuf);
	vfslocked = VFS_LOCK_GIANT(vp->v_mount);
	vrele(vp);
	VFS_UNLOCK_GIANT(vfslocked);
	if (error)
		return (error);
	error = SYSCTL_OUT(req, retbuf, strlen(retbuf) + 1);
	free(freebuf, M_TEMP);
	return (error);
}

static int
sysctl_kern_proc_sv_name(SYSCTL_HANDLER_ARGS)
{
	struct proc *p;
	char *sv_name;
	int *name;
	int namelen;
	int error;

	namelen = arg2;
	if (namelen != 1) 
		return (EINVAL);

	name = (int *)arg1;
	if ((p = pfind((pid_t)name[0])) == NULL)
		return (ESRCH);
	if ((error = p_cansee(curthread, p))) {
		PROC_UNLOCK(p);
		return (error);
	}
	sv_name = p->p_sysent->sv_name;
	PROC_UNLOCK(p);
	return (sysctl_handle_string(oidp, sv_name, 0, req));
}

#ifdef KINFO_OVMENTRY_SIZE
CTASSERT(sizeof(struct kinfo_ovmentry) == KINFO_OVMENTRY_SIZE);
#endif

#ifdef COMPAT_FREEBSD7
static int
sysctl_kern_proc_ovmmap(SYSCTL_HANDLER_ARGS)
{
	vm_map_entry_t entry, tmp_entry;
	unsigned int last_timestamp;
	char *fullpath, *freepath;
	struct kinfo_ovmentry *kve;
	struct vattr va;
	struct ucred *cred;
	int error, *name;
	struct vnode *vp;
	struct proc *p;
	vm_map_t map;
	struct vmspace *vm;

	name = (int *)arg1;
	if ((p = pfind((pid_t)name[0])) == NULL)
		return (ESRCH);
	if (p->p_flag & P_WEXIT) {
		PROC_UNLOCK(p);
		return (ESRCH);
	}
	if ((error = p_candebug(curthread, p))) {
		PROC_UNLOCK(p);
		return (error);
	}
	_PHOLD(p);
	PROC_UNLOCK(p);
	vm = vmspace_acquire_ref(p);
	if (vm == NULL) {
		PRELE(p);
		return (ESRCH);
	}
	kve = malloc(sizeof(*kve), M_TEMP, M_WAITOK);

	map = &p->p_vmspace->vm_map;	/* XXXRW: More locking required? */
	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
	    entry = entry->next) {
		vm_object_t obj, tobj, lobj;
		vm_offset_t addr;
		int vfslocked;

		if (entry->eflags & MAP_ENTRY_IS_SUB_MAP)
			continue;

		bzero(kve, sizeof(*kve));
		kve->kve_structsize = sizeof(*kve);

		kve->kve_private_resident = 0;
		obj = entry->object.vm_object;
		if (obj != NULL) {
			VM_OBJECT_LOCK(obj);
			if (obj->shadow_count == 1)
				kve->kve_private_resident =
				    obj->resident_page_count;
		}
		kve->kve_resident = 0;
		addr = entry->start;
		while (addr < entry->end) {
			if (pmap_extract(map->pmap, addr))
				kve->kve_resident++;
			addr += PAGE_SIZE;
		}

		for (lobj = tobj = obj; tobj; tobj = tobj->backing_object) {
			if (tobj != obj)
				VM_OBJECT_LOCK(tobj);
			if (lobj != obj)
				VM_OBJECT_UNLOCK(lobj);
			lobj = tobj;
		}

		kve->kve_start = (void*)entry->start;
		kve->kve_end = (void*)entry->end;
		kve->kve_offset = (off_t)entry->offset;

		if (entry->protection & VM_PROT_READ)
			kve->kve_protection |= KVME_PROT_READ;
		if (entry->protection & VM_PROT_WRITE)
			kve->kve_protection |= KVME_PROT_WRITE;
		if (entry->protection & VM_PROT_EXECUTE)
			kve->kve_protection |= KVME_PROT_EXEC;

		if (entry->eflags & MAP_ENTRY_COW)
			kve->kve_flags |= KVME_FLAG_COW;
		if (entry->eflags & MAP_ENTRY_NEEDS_COPY)
			kve->kve_flags |= KVME_FLAG_NEEDS_COPY;
		if (entry->eflags & MAP_ENTRY_NOCOREDUMP)
			kve->kve_flags |= KVME_FLAG_NOCOREDUMP;

		last_timestamp = map->timestamp;
		vm_map_unlock_read(map);

		kve->kve_fileid = 0;
		kve->kve_fsid = 0;
		freepath = NULL;
		fullpath = "";
		if (lobj) {
			vp = NULL;
			switch (lobj->type) {
			case OBJT_DEFAULT:
				kve->kve_type = KVME_TYPE_DEFAULT;
				break;
			case OBJT_VNODE:
				kve->kve_type = KVME_TYPE_VNODE;
				vp = lobj->handle;
				vref(vp);
				break;
			case OBJT_SWAP:
				kve->kve_type = KVME_TYPE_SWAP;
				break;
			case OBJT_DEVICE:
				kve->kve_type = KVME_TYPE_DEVICE;
				break;
			case OBJT_PHYS:
				kve->kve_type = KVME_TYPE_PHYS;
				break;
			case OBJT_DEAD:
				kve->kve_type = KVME_TYPE_DEAD;
				break;
			case OBJT_SG:
				kve->kve_type = KVME_TYPE_SG;
				break;
			default:
				kve->kve_type = KVME_TYPE_UNKNOWN;
				break;
			}
			if (lobj != obj)
				VM_OBJECT_UNLOCK(lobj);

			kve->kve_ref_count = obj->ref_count;
			kve->kve_shadow_count = obj->shadow_count;
			VM_OBJECT_UNLOCK(obj);
			if (vp != NULL) {
				vn_fullpath(curthread, vp, &fullpath,
				    &freepath);
				cred = curthread->td_ucred;
				vfslocked = VFS_LOCK_GIANT(vp->v_mount);
				vn_lock(vp, LK_SHARED | LK_RETRY);
				if (VOP_GETATTR(vp, &va, cred) == 0) {
					kve->kve_fileid = va.va_fileid;
					kve->kve_fsid = va.va_fsid;
				}
				vput(vp);
				VFS_UNLOCK_GIANT(vfslocked);
			}
		} else {
			kve->kve_type = KVME_TYPE_NONE;
			kve->kve_ref_count = 0;
			kve->kve_shadow_count = 0;
		}

		strlcpy(kve->kve_path, fullpath, sizeof(kve->kve_path));
		if (freepath != NULL)
			free(freepath, M_TEMP);

		error = SYSCTL_OUT(req, kve, sizeof(*kve));
		vm_map_lock_read(map);
		if (error)
			break;
		if (last_timestamp != map->timestamp) {
			vm_map_lookup_entry(map, addr - 1, &tmp_entry);
			entry = tmp_entry;
		}
	}
	vm_map_unlock_read(map);
	vmspace_free(vm);
	PRELE(p);
	free(kve, M_TEMP);
	return (error);
}
#endif	/* COMPAT_FREEBSD7 */

#ifdef KINFO_VMENTRY_SIZE
CTASSERT(sizeof(struct kinfo_vmentry) == KINFO_VMENTRY_SIZE);
#endif

static int
sysctl_kern_proc_vmmap(SYSCTL_HANDLER_ARGS)
{
	vm_map_entry_t entry, tmp_entry;
	unsigned int last_timestamp;
	char *fullpath, *freepath;
	struct kinfo_vmentry *kve;
	struct vattr va;
	struct ucred *cred;
	int error, *name;
	struct vnode *vp;
	struct proc *p;
	struct vmspace *vm;
	vm_map_t map;

	name = (int *)arg1;
	if ((p = pfind((pid_t)name[0])) == NULL)
		return (ESRCH);
	if (p->p_flag & P_WEXIT) {
		PROC_UNLOCK(p);
		return (ESRCH);
	}
	if ((error = p_candebug(curthread, p))) {
		PROC_UNLOCK(p);
		return (error);
	}
	_PHOLD(p);
	PROC_UNLOCK(p);
	vm = vmspace_acquire_ref(p);
	if (vm == NULL) {
		PRELE(p);
		return (ESRCH);
	}
	kve = malloc(sizeof(*kve), M_TEMP, M_WAITOK);

	map = &vm->vm_map;	/* XXXRW: More locking required? */
	vm_map_lock_read(map);
	for (entry = map->header.next; entry != &map->header;
	    entry = entry->next) {
		vm_object_t obj, tobj, lobj;
		vm_offset_t addr;
		int vfslocked;

		if (entry->eflags & MAP_ENTRY_IS_SUB_MAP)
			continue;

		bzero(kve, sizeof(*kve));

		kve->kve_private_resident = 0;
		obj = entry->object.vm_object;
		if (obj != NULL) {
			VM_OBJECT_LOCK(obj);
			if (obj->shadow_count == 1)
				kve->kve_private_resident =
				    obj->resident_page_count;
		}
		kve->kve_resident = 0;
		addr = entry->start;
		while (addr < entry->end) {
			if (pmap_extract(map->pmap, addr))
				kve->kve_resident++;
			addr += PAGE_SIZE;
		}

		for (lobj = tobj = obj; tobj; tobj = tobj->backing_object) {
			if (tobj != obj)
				VM_OBJECT_LOCK(tobj);
			if (lobj != obj)
				VM_OBJECT_UNLOCK(lobj);
			lobj = tobj;
		}

		kve->kve_start = entry->start;
		kve->kve_end = entry->end;
		kve->kve_offset = entry->offset;

		if (entry->protection & VM_PROT_READ)
			kve->kve_protection |= KVME_PROT_READ;
		if (entry->protection & VM_PROT_WRITE)
			kve->kve_protection |= KVME_PROT_WRITE;
		if (entry->protection & VM_PROT_EXECUTE)
			kve->kve_protection |= KVME_PROT_EXEC;

		if (entry->eflags & MAP_ENTRY_COW)
			kve->kve_flags |= KVME_FLAG_COW;
		if (entry->eflags & MAP_ENTRY_NEEDS_COPY)
			kve->kve_flags |= KVME_FLAG_NEEDS_COPY;
		if (entry->eflags & MAP_ENTRY_NOCOREDUMP)
			kve->kve_flags |= KVME_FLAG_NOCOREDUMP;

		last_timestamp = map->timestamp;
		vm_map_unlock_read(map);

		freepath = NULL;
		fullpath = "";
		if (lobj) {
			vp = NULL;
			switch (lobj->type) {
			case OBJT_DEFAULT:
				kve->kve_type = KVME_TYPE_DEFAULT;
				break;
			case OBJT_VNODE:
				kve->kve_type = KVME_TYPE_VNODE;
				vp = lobj->handle;
				vref(vp);
				break;
			case OBJT_SWAP:
				kve->kve_type = KVME_TYPE_SWAP;
				break;
			case OBJT_DEVICE:
				kve->kve_type = KVME_TYPE_DEVICE;
				break;
			case OBJT_PHYS:
				kve->kve_type = KVME_TYPE_PHYS;
				break;
			case OBJT_DEAD:
				kve->kve_type = KVME_TYPE_DEAD;
				break;
			case OBJT_SG:
				kve->kve_type = KVME_TYPE_SG;
				break;
			default:
				kve->kve_type = KVME_TYPE_UNKNOWN;
				break;
			}
			if (lobj != obj)
				VM_OBJECT_UNLOCK(lobj);

			kve->kve_ref_count = obj->ref_count;
			kve->kve_shadow_count = obj->shadow_count;
			VM_OBJECT_UNLOCK(obj);
			if (vp != NULL) {
				vn_fullpath(curthread, vp, &fullpath,
				    &freepath);
				kve->kve_vn_type = vntype_to_kinfo(vp->v_type);
				cred = curthread->td_ucred;
				vfslocked = VFS_LOCK_GIANT(vp->v_mount);
				vn_lock(vp, LK_SHARED | LK_RETRY);
				if (VOP_GETATTR(vp, &va, cred) == 0) {
					kve->kve_vn_fileid = va.va_fileid;
					kve->kve_vn_fsid = va.va_fsid;
					kve->kve_vn_mode =
					    MAKEIMODE(va.va_type, va.va_mode);
					kve->kve_vn_size = va.va_size;
					kve->kve_vn_rdev = va.va_rdev;
					kve->kve_status = KF_ATTR_VALID;
				}
				vput(vp);
				VFS_UNLOCK_GIANT(vfslocked);
			}
		} else {
			kve->kve_type = KVME_TYPE_NONE;
			kve->kve_ref_count = 0;
			kve->kve_shadow_count = 0;
		}

		strlcpy(kve->kve_path, fullpath, sizeof(kve->kve_path));
		if (freepath != NULL)
			free(freepath, M_TEMP);

		/* Pack record size down */
		kve->kve_structsize = offsetof(struct kinfo_vmentry, kve_path) +
		    strlen(kve->kve_path) + 1;
		kve->kve_structsize = roundup(kve->kve_structsize,
		    sizeof(uint64_t));
		error = SYSCTL_OUT(req, kve, kve->kve_structsize);
		vm_map_lock_read(map);
		if (error)
			break;
		if (last_timestamp != map->timestamp) {
			vm_map_lookup_entry(map, addr - 1, &tmp_entry);
			entry = tmp_entry;
		}
	}
	vm_map_unlock_read(map);
	vmspace_free(vm);
	PRELE(p);
	free(kve, M_TEMP);
	return (error);
}

#if defined(STACK) || defined(DDB)
static int
sysctl_kern_proc_kstack(SYSCTL_HANDLER_ARGS)
{
	struct kinfo_kstack *kkstp;
	int error, i, *name, numthreads;
	lwpid_t *lwpidarray;
	struct thread *td;
	struct stack *st;
	struct sbuf sb;
	struct proc *p;

	name = (int *)arg1;
	if ((p = pfind((pid_t)name[0])) == NULL)
		return (ESRCH);
	/* XXXRW: Not clear ESRCH is the right error during proc execve(). */
	if (p->p_flag & P_WEXIT || p->p_flag & P_INEXEC) {
		PROC_UNLOCK(p);
		return (ESRCH);
	}
	if ((error = p_candebug(curthread, p))) {
		PROC_UNLOCK(p);
		return (error);
	}
	_PHOLD(p);
	PROC_UNLOCK(p);

	kkstp = malloc(sizeof(*kkstp), M_TEMP, M_WAITOK);
	st = stack_create();

	lwpidarray = NULL;
	numthreads = 0;
	PROC_LOCK(p);
repeat:
	if (numthreads < p->p_numthreads) {
		if (lwpidarray != NULL) {
			free(lwpidarray, M_TEMP);
			lwpidarray = NULL;
		}
		numthreads = p->p_numthreads;
		PROC_UNLOCK(p);
		lwpidarray = malloc(sizeof(*lwpidarray) * numthreads, M_TEMP,
		    M_WAITOK | M_ZERO);
		PROC_LOCK(p);
		goto repeat;
	}
	i = 0;

	/*
	 * XXXRW: During the below loop, execve(2) and countless other sorts
	 * of changes could have taken place.  Should we check to see if the
	 * vmspace has been replaced, or the like, in order to prevent
	 * giving a snapshot that spans, say, execve(2), with some threads
	 * before and some after?  Among other things, the credentials could
	 * have changed, in which case the right to extract debug info might
	 * no longer be assured.
	 */
	FOREACH_THREAD_IN_PROC(p, td) {
		KASSERT(i < numthreads,
		    ("sysctl_kern_proc_kstack: numthreads"));
		lwpidarray[i] = td->td_tid;
		i++;
	}
	numthreads = i;
	for (i = 0; i < numthreads; i++) {
		td = thread_find(p, lwpidarray[i]);
		if (td == NULL) {
			continue;
		}
		bzero(kkstp, sizeof(*kkstp));
		(void)sbuf_new(&sb, kkstp->kkst_trace,
		    sizeof(kkstp->kkst_trace), SBUF_FIXEDLEN);
		thread_lock(td);
		kkstp->kkst_tid = td->td_tid;
		if (TD_IS_SWAPPED(td))
			kkstp->kkst_state = KKST_STATE_SWAPPED;
		else if (TD_IS_RUNNING(td))
			kkstp->kkst_state = KKST_STATE_RUNNING;
		else {
			kkstp->kkst_state = KKST_STATE_STACKOK;
			stack_save_td(st, td);
		}
		thread_unlock(td);
		PROC_UNLOCK(p);
		stack_sbuf_print(&sb, st);
		sbuf_finish(&sb);
		sbuf_delete(&sb);
		error = SYSCTL_OUT(req, kkstp, sizeof(*kkstp));
		PROC_LOCK(p);
		if (error)
			break;
	}
	_PRELE(p);
	PROC_UNLOCK(p);
	if (lwpidarray != NULL)
		free(lwpidarray, M_TEMP);
	stack_destroy(st);
	free(kkstp, M_TEMP);
	return (error);
}
#endif

/*
 * This sysctl allows a process to retrieve the full list of groups from
 * itself or another process.
 */
static int
sysctl_kern_proc_groups(SYSCTL_HANDLER_ARGS)
{
	pid_t *pidp = (pid_t *)arg1;
	unsigned int arglen = arg2;
	struct proc *p;
	struct ucred *cred;
	int error;

	if (arglen != 1)
		return (EINVAL);
	if (*pidp == -1) {	/* -1 means this process */
		p = req->td->td_proc;
	} else {
		p = pfind(*pidp);
		if (p == NULL)
			return (ESRCH);
		if ((error = p_cansee(curthread, p)) != 0) {
			PROC_UNLOCK(p);
			return (error);
		}
	}

	cred = crhold(p->p_ucred);
	if (*pidp != -1)
		PROC_UNLOCK(p);

	error = SYSCTL_OUT(req, cred->cr_groups,
	    cred->cr_ngroups * sizeof(gid_t));
	crfree(cred);
	return (error);
}

SYSCTL_NODE(_kern, KERN_PROC, proc, CTLFLAG_RD,  0, "Process table");

SYSCTL_PROC(_kern_proc, KERN_PROC_ALL, all, CTLFLAG_RD|CTLTYPE_STRUCT|
	CTLFLAG_MPSAFE, 0, 0, sysctl_kern_proc, "S,proc",
	"Return entire process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_GID, gid, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_PGRP, pgrp, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_RGID, rgid, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_SESSION, sid, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_TTY, tty, CTLFLAG_RD | CTLFLAG_MPSAFE, 
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_UID, uid, CTLFLAG_RD | CTLFLAG_MPSAFE, 
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_RUID, ruid, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_PID, pid, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, KERN_PROC_PROC, proc, CTLFLAG_RD | CTLFLAG_MPSAFE,
	sysctl_kern_proc, "Return process table, no threads");

static SYSCTL_NODE(_kern_proc, KERN_PROC_ARGS, args,
	CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_MPSAFE,
	sysctl_kern_proc_args, "Process argument list");

static SYSCTL_NODE(_kern_proc, KERN_PROC_PATHNAME, pathname, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_pathname, "Process executable path");

static SYSCTL_NODE(_kern_proc, KERN_PROC_SV_NAME, sv_name, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_sv_name,
	"Process syscall vector name (ABI type)");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_GID | KERN_PROC_INC_THREAD), gid_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_PGRP | KERN_PROC_INC_THREAD), pgrp_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_RGID | KERN_PROC_INC_THREAD), rgid_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_SESSION | KERN_PROC_INC_THREAD),
	sid_td, CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_TTY | KERN_PROC_INC_THREAD), tty_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_UID | KERN_PROC_INC_THREAD), uid_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_RUID | KERN_PROC_INC_THREAD), ruid_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_PID | KERN_PROC_INC_THREAD), pid_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc, "Process table");

static SYSCTL_NODE(_kern_proc, (KERN_PROC_PROC | KERN_PROC_INC_THREAD), proc_td,
	CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_kern_proc,
	"Return process table, no threads");

#ifdef COMPAT_FREEBSD7
static SYSCTL_NODE(_kern_proc, KERN_PROC_OVMMAP, ovmmap, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_ovmmap, "Old Process vm map entries");
#endif

static SYSCTL_NODE(_kern_proc, KERN_PROC_VMMAP, vmmap, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_vmmap, "Process vm map entries");

#if defined(STACK) || defined(DDB)
static SYSCTL_NODE(_kern_proc, KERN_PROC_KSTACK, kstack, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_kstack, "Process kernel stacks");
#endif

static SYSCTL_NODE(_kern_proc, KERN_PROC_GROUPS, groups, CTLFLAG_RD |
	CTLFLAG_MPSAFE, sysctl_kern_proc_groups, "Process groups");
