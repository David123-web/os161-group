#ifndef _FSYSTEMCALLS_H_
#define _FSYSTEMCALLS_H_

#include <types.h>
#include<filetable.h>
#include<kern/seek.h>
#include<lib.h>

//usersystem calls
int open(const_userptr_t filename, int flags, mode_t mode, int *retval);
int dup2(int oldfd, int newfd, int *retval);
int close(int fd);
int read(int fd, userptr_t buf, size_t size, int *retval);
int write(int fd, userptr_t buf, size_t size, int *retval);
int lseek(int fd, off_t offset, int code, off_t *retval);

int chdir(const_userptr_t path);
int __getcwd(userptr_t buf, size_t buflen, int *retval);

#endif
