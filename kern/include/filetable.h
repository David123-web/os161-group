#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>
#include <lib.h>


//file descriptor
struct file{
    int refcount;               //reference count
    off_t offs;                 //offset
    struct lock *lk_file;        
    struct vnode *vn;     
    const char *filename;       //name of the file
    int flag;                   //read and write flags
}; 

//filetable
struct ft {
    struct lock *lk_ft;
    struct file *filetable[OPEN_MAX];
};


//filetable functions
struct ft *filetable_create(void);
int ft_init(struct ft*);
void file_destroy(struct ft*);
int new_file(struct ft*, struct file*, int *);

void decre_file(struct file * f, bool b);
void addNewFd(struct ft * ft, struct file * item, int fd);
void freeFd(struct ft * ftable, int fd);

#endif

