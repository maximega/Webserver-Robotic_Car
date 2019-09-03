#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#define FALSE 0
#define TRUE 1

#define mctx_save(mctx, save_sigs) \
	sigsetjmp((mctx)->jb, save_sigs)

#define mctx_restore(mctx) \
	siglongjmp((mctx)->jb, 1)

#define mctx_switch(mctx_old, mctx_new, save_sigs) \
	if (sigsetjmp((mctx_old)->jb, save_sigs) == 0) \
		siglongjmp((mctx_new)->jb, 1)

typedef struct mctx_st {
	unsigned long long int tid;	//will use for thread id's later
	sigjmp_buf jb;
	void *sk;
} mctx_t;

void mctx_create(mctx_t*, void (*func)(void*), void*, void*, size_t);
void mctx_create_trampoline(int);
void mctx_create_boot(void);


