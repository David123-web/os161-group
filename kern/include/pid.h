#ifndef _PID_H_
#define _PID_H_

#define INVALID_PID	0	/* nothing has this pid */
#define KERNEL_PID	1	/* kernel proc has this pid */

/*
 * Initialize pid
 */
void pid_init(void);

/*
 * Get a pid for a new thread.
 */
int pid_allocate(pid_t *retval);

/*
 * Deallocate pid
 */
void pid_deallocate(pid_t pid);



/*
 * Set the exit status of the current thread. Wake up any threads waiting
 */
void pid_setExit(int st);

/*
 * Wait for the thread with pid inputPid to exit
 */
int pid_wait(pid_t inputPid, int *st, int flags, pid_t *retv) ;

#endif 