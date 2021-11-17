#include <types.h>
#include <lib.h>
#include <limits.h>
#include <thread.h>
#include <array.h>
#include <clock.h>
#include <kern/wait.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <pid.h>
#include <synch.h>

/*
 * Process id info data structures
 */
struct p_info {
	pid_t id;			  // id of current thread
	pid_t p_id;			  // id of parent thread
	int exitStatus;		
	volatile bool exited;
	struct cv *p_cv;		
};

/*
 * Pid metadata.
 */
static struct lock *p_lk;		// lock for exit data
static int numOfProcs;			// number of processes allocated
static pid_t nextp;			    // next pid
static struct p_info *p_info[PROCS_MAX]; 


/*
 * Create a new p_info for the a given pid.
 */
static struct p_info * p_info_create(pid_t pid, pid_t ppid) {
	
	KASSERT(pid != INVALID_PID);
    struct p_info * pinfo;
	pinfo = kmalloc(sizeof(struct p_info));
	if (pinfo != NULL) {
	 pinfo->p_cv = cv_create("");
	  if (pinfo->p_cv == NULL) {               //double check if the cv is created
		kfree(pinfo);
		return NULL;
	  }

	 pinfo->id = pid;                         //assign properties
	 pinfo->p_id = ppid;
	 pinfo->exitStatus = 0xbeef;             
	 pinfo->exited = false;
	 return pinfo;
	}

    return NULL;
}

/*
 * Destroy a p_info element
 */
static void p_info_destroy(struct p_info *pinfo) {
	KASSERT(pinfo->exited == true);
	KASSERT(pinfo->p_id == INVALID_PID);
	cv_destroy(pinfo->p_cv);
	kfree(pinfo);
}


/*
 * initialize a pid
 */
void pid_init(void) {

	p_lk = lock_create(""); 
	if(p_lk == NULL) {                                         //lock creation failure
		panic("cannot create pid lock due to limited memory\n");
	}

	//initialize pidinfo
	int i = 0;
	while(i < PROCS_MAX) {
		 p_info[i] = NULL;
		  i++;
	} 

	p_info[KERNEL_PID] = p_info_create(KERNEL_PID, INVALID_PID);
	if (p_info[KERNEL_PID]==NULL) {                                // p_info creation failure
		panic("cannot create kernel pid data due to limited memory\n");
	}
    numOfProcs = 1;
	nextp = PID_MIN;
}

/*
 * Find a p_info using the given id
 */
static struct p_info * pi_get(pid_t pid) {
	struct p_info * pinfo;
    
	KASSERT(pid != INVALID_PID);               //badcall handler
	KASSERT(pid >= 0);
	KASSERT(lock_do_i_hold(p_lk));

	pinfo = p_info[pid % PROCS_MAX];

	if (pinfo==NULL) {                         //p_info access failure
		return NULL; }

	else if (pinfo->id != pid) {
		return NULL; }

	return pinfo;
}

/*
 * Insert a new p_info into the process table 
 */ 
static void pi_add(pid_t pid, struct p_info *pinfo) {
	KASSERT(lock_do_i_hold(p_lk));               //badcall handler
	KASSERT(pid != INVALID_PID);
	KASSERT(p_info[pid % PROCS_MAX] == NULL);
	p_info[pid % PROCS_MAX] = pinfo;
	numOfProcs++;
}

/*
 * Clear and free a p_info element from the process table
 */
static void pi_drop(pid_t pid) {
	struct p_info *pinfo;
	KASSERT(lock_do_i_hold(p_lk));
	pinfo = p_info[pid % PROCS_MAX];
	KASSERT(pinfo != NULL);
	KASSERT(pinfo->id == pid);
	p_info_destroy(pinfo);
	p_info[pid % PROCS_MAX] = NULL;
	numOfProcs--;
}


/*
 * Allocate a process id.
 */
int pid_allocate(pid_t *retval) {
	
	struct p_info *pinfo;
	pid_t pid;
	
	KASSERT(curproc->p_pid != INVALID_PID);
	lock_acquire(p_lk);                  //lock the pid table
	if (numOfProcs == PROCS_MAX) {
		lock_release(p_lk);
		return EAGAIN;
	}                                         //The above code ensures the following loop will terminate.

	int counter = 0;
	while (p_info[nextp % PROCS_MAX] != NULL) {
		KASSERT(counter < PROCS_MAX*2+5);     //avoid various boundary cases by allowing extra loops?
		counter++;
		KASSERT(lock_do_i_hold(p_lk));
	    nextp++;
	    if (nextp > PID_MAX) { nextp = PID_MIN;  }  //if pid reached the max then start over
	}

    pid = nextp;
	
	pinfo = p_info_create(pid, curproc->p_pid);      //create p_info element
	if (pinfo==NULL) {
		lock_release(p_lk);
		return ENOMEM;
	}

	pi_add(pid, pinfo);

	KASSERT(lock_do_i_hold(p_lk));                   //move to next pid
    nextp++;
	if (nextp > PID_MAX) { nextp = PID_MIN;  }       //if pid reached the max then start over

	lock_release(p_lk);

	*retval = pid;
	return 0;
}

/*
 *Deallocate a process id that hasn't run yet.
 */
void pid_deallocate(pid_t pid) {
	
	KASSERT(pid >= PID_MIN && pid <= PID_MAX);
    struct p_info *pinfo;
	lock_acquire(p_lk);

	pinfo = pi_get(pid);
	KASSERT(pinfo != NULL);
	KASSERT(pinfo->exited == false);        //the pid cannot exit
	KASSERT(pinfo->p_id == curproc->p_pid);

	pinfo->exited = true;
	pinfo->p_id = INVALID_PID;
	pinfo->exitStatus = 0xdead;
	
	pi_drop(pid);
	lock_release(p_lk);
}

/*
 * Wait on a pid and return the exit status when it's available.
 */
int pid_wait(pid_t inputPid, int *st, int flags, pid_t *retv) {
	struct p_info *inputPidInfo;
	KASSERT(curproc->p_pid != INVALID_PID);

    if (flags != 0 && flags != WNOHANG) {          //flags valid
		return EINVAL;
	}

	if (inputPid == INVALID_PID || inputPid<0) {   //no negative or 0 pid
		return ENOSYS;
	}

	if (inputPid == curproc->p_pid) {             //avoid deadlock
		return ECHILD;
	}

	lock_acquire(p_lk);

	inputPidInfo = pi_get(inputPid);
	if (inputPidInfo == NULL) {
		lock_release(p_lk);
		return ESRCH;
	}

	KASSERT(inputPidInfo->id == inputPid);

	if (inputPidInfo->p_id != curproc->p_pid) {        //Allow waiting for our own children pid. 
		lock_release(p_lk);
		return ECHILD;
	}

	if (inputPidInfo->exited == false) {
		if (flags == WNOHANG) {
			lock_release(p_lk);
			KASSERT(retv != NULL);
			*retv = 0;
			return 0;
		}
		cv_wait(inputPidInfo->p_cv, p_lk);
		KASSERT(inputPidInfo->exited == true);
	}
    if (retv != NULL) {	*retv = inputPid;  }     //return the pid we want
	if (st != NULL) { *st = inputPidInfo->exitStatus;  }   //status may be null

	inputPidInfo->p_id = 0;
	pi_drop(inputPidInfo->id);                  //clear the pidinfo

	lock_release(p_lk);
	return 0;
}

/*
 * Sets the exit status of this process. Wakes up any
 * waiters and clear the pid data if it is not used by any process.
 */
void pid_setExit(int st) {
	struct p_info *ourPidInfo;

	lock_acquire(p_lk);
	KASSERT(curproc->p_pid != INVALID_PID);
	int i=0;
	while(i < PROCS_MAX){             // Disown all children pid
		
		if (p_info[i]!=NULL) {	
			if (p_info[i]->p_id == curproc->p_pid) {
				p_info[i]->p_id = INVALID_PID;          //If the process is done, then release the pid for subsequent reuse
				if (p_info[i]->exited) {
					pi_drop(p_info[i]->id);
				}
			}
		}
		i++;
	}

	ourPidInfo = pi_get(curproc->p_pid);                //wake up parents pid
	KASSERT(ourPidInfo != NULL);
    
	ourPidInfo->exitStatus = st;
    ourPidInfo->exited = true;

	if (ourPidInfo->p_id == INVALID_PID) {              //no parent pid
		pi_drop(curproc->p_pid);
	}
	else {
		cv_broadcast(ourPidInfo->p_cv, p_lk);
	}

	curproc->p_pid = INVALID_PID;            //If the process is done, then release the pid for subsequent reuse 
	lock_release(p_lk);
}

