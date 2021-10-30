#include<types.h>
#include<lib.h> //
#include<filetable.h> //
#include <kern/errno.h> //
#include <fsystemcalls.h> //
#include <vfs.h> //
#include <kern/fcntl.h> //

struct file* file_create(struct vnode*);
//static int init_std_io(struct ft *, int , int);
//initialize the filetable

int ft_init(struct ft*filetable){
    KASSERT(filetable!=NULL);

    struct vnode *vn;
    const char *cons1="con:";
    const char *cons2="con:";
    const char *cons3="con:";
    
    int success;

    KASSERT(filetable!=NULL);

    //STDIN
    success=vfs_open(kstrdup(cons1), O_RDONLY, 0,&vn);
    if(success){
       return success;
    }
    

    filetable->filetable[0]=file_create(vn);
    if(filetable->filetable[0]==NULL){
        return ENOMEM;
    }

    filetable->filetable[0]->flag=O_RDONLY;
    filetable->filetable[0]->refcount++;
    
    //STDOUT
    struct vnode *vn1;
    success=vfs_open(kstrdup(cons2), O_WRONLY, 0,&vn1);
    if(success){
        return success;
    }

    filetable->filetable[1]=file_create(vn1);
    if(filetable->filetable[1]==NULL){
        return ENOMEM;
    }

    filetable->filetable[1]->flag=O_WRONLY;
    filetable->filetable[1]->refcount++;

    //STDERR
    struct vnode *vn2;
    cons3=kstrdup("con:");
    success=vfs_open(kstrdup(cons3), O_WRONLY, 0,&vn2);
    if(success){
        return success;
    }

    filetable->filetable[2]=file_create(vn2);
    if(filetable->filetable[2]==NULL){
        return ENOMEM;
    }

    filetable->filetable[2]->flag=O_WRONLY;
    filetable->filetable[2]->refcount++;

    return 0;


}

//create a new file
struct file* file_create(struct vnode*vn){
    struct file *file;
    file=kmalloc(sizeof(struct file));
    if(file==NULL){
        return NULL;
    }
    file->lk_file=lock_create("");
    file->vn=vn;
    file->refcount=0;
    file->offs=0;

    return file;

}

//destroy the filetable
void file_destroy(struct ft* filetable){
    KASSERT(filetable!=NULL);
    lock_destroy(filetable->lk_ft);
    kfree(filetable);
}

//create a new filetable
struct ft* filetable_create(void){
    struct ft *filetable;
    filetable=kmalloc(sizeof(struct ft));

    if(filetable!=NULL){
        filetable->lk_ft=lock_create("");
        for(int i=0; i<OPEN_MAX; i++){
            filetable->filetable[i]=NULL;
        }
    }else{
        return filetable;
    }

    return filetable;

}
//add a new file into the filetable
int new_file(struct ft* filetable, struct file* file, int32_t *fd){
    for(int i=3; i<OPEN_MAX; i++){
        if(filetable->filetable[i]==NULL){
            KASSERT(i>=0&&i<OPEN_MAX);
            addNewFd(filetable, file, i);
            *fd=i;
            return 0;
        }
    }
    return EMFILE;
}


/*
*Assign a new file descriptor 
*/
void addNewFd(struct ft * ft, struct file * item, int fd){
    KASSERT(ft!=NULL);
    KASSERT(item!= NULL);
    KASSERT(fd >= 0 && fd < OPEN_MAX );
    KASSERT(ft->filetable[fd]==NULL);
    ft->filetable[fd] = item;
    lock_acquire(item->lk_file);
    KASSERT(item != NULL );
    item->refcount++;
    lock_release(item->lk_file);
}

/*
*Free a file descriptor 
*/
void freeFd(struct ft * ftable, int fd){
     KASSERT(fd >= 0 && fd < OPEN_MAX );
     KASSERT(ftable != NULL);
     KASSERT(ftable->filetable[fd] != NULL);

     lock_acquire(ftable->filetable[fd]->lk_file);
     decre_file(ftable->filetable[fd], true);
     ftable->filetable[fd] = NULL;
}

/*
*Decrement entry in the filetable
*/
void decre_file(struct file * f, bool b){
     KASSERT(f!=NULL);
     KASSERT(b);

     f->refcount--;
     if(f->refcount==0){
         vfs_close(f->vn);
         lock_destroy(f->lk_file);
         kfree(f);
         return;
     }
     lock_release(f->lk_file);
}



