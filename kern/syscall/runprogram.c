/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <synch.h>
#include <limits.h>
#include <filetable.h>
#include <copyinout.h>
#include <vm.h>

/*
create a buffer structure to store args for execv
*/

struct buf {
	char *args;
	int nargs;
	size_t length;
	size_t max_len;
	bool holdlock;
};
//lock used to limit number of processes in exec
static struct semaphore *lk_exec;

/*initialize the args buffer*/
static void buf_init(struct buf* buf){
	buf->args=NULL;
	buf->nargs=0;
	buf->length=0;
	buf->max_len=0;
	buf->holdlock=false;
}

/*free args buffer*/
static void buf_destroy(struct buf* buf){
	buf->nargs=0;
	buf->length=0;
	buf->max_len=0;
	if(buf->args!=NULL){
		kfree(buf->args);
		buf->args=NULL;
	}
	if(buf->holdlock==true){
		V(lk_exec);  //free the lock and let other process in
		buf->holdlock=false;
	}

}
//allocate space from buffer
static int buf_allocate(struct buf *buf, size_t size){
	
	buf->max_len=size;
	
	buf->args=kmalloc(size); //allocate space for args

	if(buf->args==NULL){   //no space return no memory message
		return ENOMEM;
	}

	return 0;
}

/*
exec_start initilize lock for the argbuf, bootstrap of execv
*/

void exec_start(void){
	lk_exec=sem_create("",1); //initialize the semaphore to be free at first
	if(lk_exec==NULL){
		panic("failed to create buf lock for args!!");
	}
}


/*
copy the arguments of the process into the kernel space
*/

static int args_copyin(userptr_t args, struct buf* buf){
	userptr_t argsptr;

	size_t argslength;

    int result;

	
	//iterate through the argv
	for(buf->nargs=0; argsptr!=NULL; buf->nargs++){
		//fetch each ptr in the argv
		result=copyin(args, &argsptr, sizeof(userptr_t));
		
		if(result){
			return result;
		}
		//reach the end of argv, Null indicates the end of the argv
		if(argsptr==NULL){
			break;
		}
		
		char* bufstart=buf->args+buf->length; //find buffer entrypoint
		size_t bufend=buf->max_len-buf->length; //find buffer endpoint

		//fetch the arguments str
		result=copyinstr(argsptr, bufstart, bufend, &argslength);
		
		//check if the argv string is too long
		if(result==ENAMETOOLONG){ 
			return E2BIG;
		}
		if(result){
			return result;
		}
		
		
		//update the buffer length
		buf->length= buf->length+argslength; 

		//increse the size of argv
		args=args+sizeof(userptr_t);    
	}
	return 0;

}

/* copy the argv from kernel to user space
*/

static int args_copyout(struct buf* buf, userptr_t *args, vaddr_t *sp, int *nargs){
	userptr_t strbase, argbase, arg;

	vaddr_t stk; size_t offset=0;

	userptr_t argsptr; size_t argslength=0; int result;

	stk=*sp;

	//find out the starting point of the buffer, stack grows downward
	stk=stk-buf->length;

	//align stack to strbase
	stk=stk-(stk&(sizeof(void*)-1));
		
	strbase=(userptr_t)stk;

	//leave space for argv, and find the argbase
	int argc=buf->nargs+1; //add one to add the null argument
	//nargs*sizeof arg
	stk=stk-(argc)*sizeof(userptr_t);

	//arrive at the base of stack
	argbase=(userptr_t)stk;

	*args=argbase;
	*sp=stk;

	arg=argbase;
	//copyout the actual arg data
	for(offset=0; offset<buf->length; offset+=argslength){
		//calculate user addr of the string 
		argsptr=strbase+offset;

		//copy to the argv array
		result=copyout(&argsptr, arg, sizeof(argsptr));

		if(result){
			return result;

		}

		char* start=buf->args+offset; //find the starting point of one argument
		size_t end=buf->length-offset; //find the ending point of the above argument
		//calculate the argslength
		result= copyoutstr(start, argsptr, end, &argslength); //calculate one argument's length
		if(result){
			return result;
		}
		arg=arg+sizeof(argsptr);
	
	}
	//add NUll to terminate the argv

	argsptr=NULL;

	result=copyout(&argsptr, arg, sizeof(userptr_t));

	if(result){
		return result;

	}

	*nargs=buf->nargs;
	
	return 0;
}



/*
load the executable
*/

static int exec_load(char *name, vaddr_t *entryp, vaddr_t *sp){

	struct vnode *v;
	char* newthread;
	int result;
	struct addrspace  *newspace, *oldspace;
	
	//read the target file
	result=vfs_open(name, O_RDONLY, 0, &v);
	if(result){    
		return result;
	}

	//kmalloc new thread's name
	newthread=kstrdup(name);
	if(newthread==NULL){
		return ENOMEM;
	}

	

	//make new addressapce

	newspace=as_create();
	if(newspace==NULL){
		kfree(newthread);
		vfs_close(v); //close the file if failed to create addrspace
		return ENOMEM;
	}

	//Change the address space of (the current) process. Return the old one
	oldspace=proc_setas(newspace);
	//make the current addrspace avaible for processor
	as_activate();

	//load the executable

	result=load_elf(v, entryp);
	//if failed, then activate to the oldaddrspace
	if(result){
		kfree(newthread);
		vfs_close(v);
		//curthread->t_addrspace=oldspace;
		proc_setas(oldspace); //restore the old addrspace if failed
		as_activate();
		as_destroy(newspace);
		return result;

	}

	//set up the stack region in the address space.
	result=as_define_stack(newspace, sp);

	if(result){
		kfree(newthread);
		vfs_close(v);
		proc_setas(oldspace);
		as_activate();
		as_destroy(newspace);
		return result;
	}

	//close the file
	vfs_close(v);

	//clean the old addressapce
	if(oldspace!=NULL){
		as_destroy(oldspace);
	}

	//change the name of the curthread to the new thread's name

	kfree(curthread->t_name);
	curthread->t_name=newthread;

	return 0;


}




/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{	
	struct buf buf;
	int argcount;
	userptr_t uargs;
	vaddr_t entrypoint, stackptr;
	int result;

	//check if the process id is valid thtat can run in user process
    KASSERT(curproc->p_pid>=PID_MIN && curproc->p_pid <=PID_MAX);

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	if(curproc->proc_ft==NULL){
		//create a new filetable
		curproc->proc_ft=filetable_create(); 
		if(curproc->proc_ft==NULL){
			return ENOMEM;
		}
		//set filetable to have stdin/out/err files 
		result=ft_init(curproc->proc_ft);
		if(result){
			return result;
		}

	}
	//create the arguments buf
	buf_init(&buf);

	result=buf_allocate(&buf,strlen(progname)+1);
	if(result){
		buf_destroy(&buf);
		return result;
	}
	strcpy(buf.args, progname);
	buf.length=strlen(progname)+1;
	buf.nargs=1;

	//load the executable
	result=exec_load(progname, &entrypoint, &stackptr);
	if(result){
		buf_destroy(&buf);
		return result;
	}

	//copyout the arguments
	result=args_copyout(&buf, &uargs, &stackptr, &argcount);
	if(result){
		panic("args_copyout failed");
	}

	buf_destroy(&buf);


	/* Warp to user mode. */
	enter_new_process(argcount/*argc*/, uargs /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}


/*
execv replaces the currently executing program with a newly loaded program image.
This occurs within one process; the process id is unchanged.
*/

int
execv(userptr_t program, userptr_t args){

	struct buf buf;
	int argcount;
	char *name;
	vaddr_t entrypoint, stackptr;
	int result;

	//allocate space for path
	name=kmalloc(PATH_MAX);
	if(name==NULL){
		return ENOMEM;
	}

	buf_init(&buf);

	//retrieve the filename

	result=copyinstr(program, name, PATH_MAX, NULL);
	if(result){
		kfree(name);
		buf_destroy(&buf);
		return result;
	}

	//retrieve the args str
	result=buf_allocate(&buf, PAGE_SIZE);
	if(result){
		kfree(name);
		buf_destroy(&buf);
		return result;
	}

	
	result=args_copyin(args, &buf);

	//if arguments str is too long, try a bigger buffer
	if(result==E2BIG){

		buf_destroy(&buf);
		buf_init(&buf);

		P(lk_exec);
		buf.holdlock=true;

		result=buf_allocate(&buf, ARG_MAX);
		if(result){
			kfree(name);
			buf_destroy(&buf);
			return result;
		}

		result=args_copyin(args, &buf);
	 }
	
	if(result){
		kfree(name);
		buf_destroy(&buf);
		return result;
	}

	//load the executable

	result=exec_load(name, &entrypoint, &stackptr);

	if(result){
		kfree(name);
		buf_destroy(&buf);
		return result;
	}
	
	//copyout the args strings to the user process
	result=args_copyout(&buf, &args, &stackptr, &argcount);

	if(result){
		panic("failed to copyout args strings");
	}

	kfree(name);

	//free the buffer after execution
	buf_destroy(&buf);


	/* Warp to user mode. */
	enter_new_process(argcount/*argc*/, args /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}