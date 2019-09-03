#include "threads.h"

/**
* mctx_create(), mctx_create_trampoline(), and mctx_create_boot() work in tandem to create thread contexts using
* sigsetjmp(), siglongjmp(), and sigaltstack(), following the protocol specified in Engelschall's 2000 paper.
* The macros mctx_save(), mctx_restore(), and mctx_switch() are used to move from one context to another.
* http://www.cs.bu.edu/fac/richwest/cs410_fall_2018/assignments/a2/portable_threads.pdf
**/


static mctx_t		mctx_caller;
static sig_atomic_t	mctx_called;

static mctx_t		*mctx_creat;
static void		(*mctx_creat_func)(void *);
static void		*mctx_creat_arg;
static sigset_t		mctx_creat_sigs;


void mctx_create(mctx_t *mctx, void (*sf_addr)(void *), void *sf_arg, void *sk_addr, size_t sk_size) {

	//structs for new stack
	struct sigaction sa;
	struct sigaltstack ss;
	sigset_t sigs;


	//structs to preserve original stack
	struct sigaction osa;
	struct sigaltstack oss;
	sigset_t osigs;


	/* STEP 1 */
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigs, &osigs);

	
	/* STEP 2 */
	memset((void *)&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = mctx_create_trampoline;
	sa.sa_flags = SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, &osa);

	
	/* STEP 3 */
	ss.ss_sp = sk_addr;
	ss.ss_size = sk_size;
	ss.ss_flags = NULL;
	sigaltstack(&ss, &oss);


	/* STEP 4 */
	mctx_creat = mctx;
	mctx_creat_func = sf_addr;
	mctx_creat_arg = sf_arg;
	mctx_creat_sigs = osigs;
	mctx_called = FALSE;
	kill(getpid(), SIGUSR1);
	sigfillset(&sigs);
	sigdelset(&sigs, SIGUSR1);
	while(!mctx_called)
		sigsuspend(&sigs);	


	/* STEP 6 */
	sigaltstack(NULL, &ss);
	ss.ss_flags = SS_DISABLE;
	sigaltstack(&ss, NULL);
	if (!(oss.ss_flags & SS_DISABLE))
		sigaltstack(&oss, NULL);
	sigaction(SIGUSR1, &osa, NULL);
	sigprocmask(SIG_SETMASK, &osigs, NULL);


	/* STEP 7, STEP 8 */
	mctx_switch(&mctx_caller, mctx, 0);

	
	/* STEP 14 */
	return;
}


void mctx_create_trampoline(int sig) {

	/* STEP 5 */
	if (mctx_save(mctx_creat, 0) == 0) {

		mctx_called = TRUE;
		return;
	}


	/* STEP 9 */
	mctx_create_boot();
}


void mctx_create_boot(void) {

	void (*mctx_start_func)(void *);
	void *mctx_start_arg;


	/* STEP 10 */
	sigprocmask(SIG_SETMASK, &mctx_creat_sigs, NULL);


	/* STEP 11 */
	mctx_start_func = mctx_creat_func;
	mctx_start_arg = mctx_creat_arg;

	
	/* STEP 12, STEP 13 */
	mctx_switch(mctx_creat, &mctx_caller, 1); //save sigset here because we will siglongjmp from a SIGALRM and want to reset signals


	/* Start thread. */
	mctx_start_func(mctx_start_arg);


	/* We should never reach this. */
	abort();
}
