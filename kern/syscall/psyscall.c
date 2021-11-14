#include <types.h>
#include <addrspace.h>
#include <kern/wait.h>
#include <copyinout.h>
#include <machine/trapframe.h>
#include <lib.h>
#include <thread.h>
#include <proc.h>
#include <clock.h>
#include <kern/errno.h>
#include <current.h>
#include <syscall.h>
#include <pid.h>
#include <vnode.h>
#include <filetable.h>


/*
 * Get process id
 */
int getpid(pid_t *ret) {
	*ret = curproc->p_pid;
	return 0;
}

/*
 * create a new process
 */
static void fork_newthread(void *inputtf, unsigned long junk) {
	struct trapframe tf;
	struct trapframe *newTf = inputtf;

	(void)junk;

	tf = *newTf;            //copy the new tf and use it to enter userspace
	kfree(newTf);

	enter_forked_process(&tf);
}

/*
 * Duplicate the process
 */
int fork(struct trapframe *itf, pid_t *ret)
{
	struct trapframe *newTf;
	struct proc *nproc;

	newTf = kmalloc(sizeof(struct trapframe));
	if(newTf==NULL){                       
	   return ENOMEM;
	}
	*newTf = *itf;                    //set current ts to the new valid one

	int output = p_fork(&nproc);
	if(output){
	   kfree(newTf);                  //child proc frees the copy
	   return output;
	}
	*ret = nproc->p_pid;

	output = thread_fork(curthread->t_name, nproc, fork_newthread, newTf, 0);
	if(output){
	   p_unfork(nproc);
	   kfree(newTf);
	   return output;
	}

	return 0;
}

/*
 * Wait for a process to exit
 */
int waitpid(pid_t pid, userptr_t ret_st, int flags, pid_t *ret_val) {
	int output;
	int st;

	output = pid_wait(pid, &st, flags, ret_val);
	if (output) {
		return output;
	}

	if (ret_st != NULL) {
		output = copyout(&st, ret_st, sizeof(int));
	}

	return output;
}

/*
 * Exit the process
 */
__DEAD
void exit(int status) {
	p_exit(_MKWAIT_EXIT(status));
	thread_exit();
}


//Below are helper functions
/*
* Copy current process 
*/
int p_fork(struct proc **retval){

	struct addrspace *as;
	struct filetable *ft;
	struct proc *nproc;

	int res;

	nproc=proc_create(curproc->p_name);
	if(nproc==NULL){
		return ENOMEM;
	}

	res=pid_allocate(&nproc->p_pid);      //allocate space for pid id

	if(res){                     //destroy the process if process id cannot be allocated
		proc_destroy(nproc);
		return res;
	}

	as = proc_getas();  //Fetch the address space of the current process.
	if(as != NULL){
        res = as_copy(as, &nproc->p_addrspace); //copy addrspace to current process addrspace
	   	if(res){
          pid_deallocate(nproc->p_pid);
		  nproc->p_pid = INVALID_PID;
		  proc_destroy(nproc);
		  return res;
	   	}
	}

	ft = curproc->proc_ft;        
	if(ft!=NULL){
		res = filetable_copy(ft, &nproc->proc_ft);  //copy the filetable to the newprocess
		if(res){
			as_destroy(nproc->p_addrspace);
			nproc->p_addrspace=NULL;
			pid_deallocate(nproc->p_pid);
			nproc->p_pid = INVALID_PID;
			proc_destroy(nproc);
			return res;
		}
	}

	//copy current directory to the new process	
	spinlock_acquire(&curproc->p_lock);
	if(curproc->p_cwd != NULL){
              VOP_INCREF(curproc->p_cwd);   
			  nproc->p_cwd = curproc->p_cwd;
		}
    spinlock_release(&curproc->p_lock);

	*retval = nproc;
	
	return 0;
		
	
}

/*
*Undo proc_fork
*/
void p_unfork(struct proc *nproc){
	pid_deallocate(nproc->p_pid);
	nproc->p_pid = INVALID_PID;
	proc_destroy(nproc);
}

/*
*Exit the process
*/
void p_exit(int st){
	struct proc *p = curproc;
	KASSERT(p != kproc);
	pid_setExit(st);                    //wake up other waiting threads

	KASSERT(curthread->t_proc = p);
    proc_remthread(curthread);
	proc_addthread(kproc, curthread);

	KASSERT(threadarray_num(&p->p_threads) == 0);           //no threads left
    proc_destroy(p);
	thread_exit();
}
