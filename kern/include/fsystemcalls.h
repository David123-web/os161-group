#ifndef _FSYSTEMCALLS_H_
#define _FSYSTEMCALLS_H_

#include <types.h>
#include<filetable.h>
#include<kern/seek.h>
#include<lib.h>

//usersystem calls


int open(const char *, int, int32_t *);

int write(int, const void *, size_t, int32_t *);

int read(int, void *, size_t , int32_t *);

int lseek(int, off_t, int , int32_t*, int32_t *);

int close(int);

int dup2(int, int, int*);

int chdir(const char*);

int __getcwd(char*, size_t, int32_t*);

#endif
