#include <types.h>
#include <vnode.h> //
#include <vfs.h> //
#include <limits.h> //
#include <filetable.h> //
#include <kern/errno.h> //
#include <uio.h> //
 //
#include <fsystemcalls.h> //
#include <copyinout.h> //
#include <proc.h> //
#include <kern/fcntl.h> //
#include <vm.h> //
#include <current.h> //
#include <kern/stat.h> //
#include <synch.h> //

//open files with given flags
struct file* file_create(struct vnode*);

int
open(const char *filename, int flags, int32_t *retval){
    struct vnode *vn=NULL;
    char* name=kmalloc(PATH_MAX);
    size_t *length=kmalloc(sizeof(size_t));
    struct ft *filetable=curproc->proc_ft;
    
    //copy the pathname from userspace ot kernel space
    int callback=copyinstr((const_userptr_t)filename, name, PATH_MAX, length);
    if(callback){  //check if the string copied is valid, if not return the error message
        kfree(length);
        kfree(name);
        return callback;
    }

    int open=vfs_open(name, flags, 0 ,&vn); //mode dont care
        kfree(length);
        kfree(name);
    if(!open){

        struct file *file=file_create(vn);
        file->flag=flags;
        
        if(file==NULL){
            return ENOMEM;
        } 

        if(flags & O_APPEND){
            struct stat *stat=kmalloc(sizeof(struct stat));
            VOP_STAT(file->vn, stat);
            file->offs=stat->st_size;
            kfree(stat);
        }

        lock_acquire(filetable->lk_ft);
        open=new_file(filetable, file,retval);
        lock_release(filetable->lk_ft);
       
        int fail=0;
        if(open){
            return open;
        }else{
            return fail;
        }
    }else{
        return open;
    }





}

//write data into the file 

int
write(int fd, const void *buf, size_t nbytes, int32_t *retval1){
    struct iovec track;   //keeping track of blocks of data for I/O.
    struct uio  control;          //manage blocks of data moved around by the kernel
    struct ft* filetable=curproc->proc_ft;


    struct file *file;
    lock_acquire(filetable->lk_ft);
    if((fd<0||fd>=OPEN_MAX) && (filetable->filetable[fd]!=NULL)){
        lock_release(filetable->lk_ft);
        return EBADF;
    }else{
       file=filetable->filetable[fd]; 
    }
    
    lock_acquire(file->lk_file);
    lock_release(filetable->lk_ft);
    if(!(file->flag & O_WRONLY || file->flag &O_RDWR)){
        lock_release(file->lk_file);
        return EBADF;
    }
    
    
    track.iov_len=nbytes;
    track.iov_ubase=(userptr_t)buf;
    control.uio_space=curproc->p_addrspace;
    control.uio_iovcnt=1;
    control.uio_iov=&track;
    control.uio_resid=nbytes;
    control.uio_segflg=UIO_USERSPACE;
    control.uio_rw=UIO_WRITE;
    control.uio_offset=file->offs;
    

    int fail=0;
    int isWrite=VOP_WRITE(file->vn, &control);
    if(!isWrite){
        ssize_t length=nbytes-control.uio_resid;
        file->offs=file->offs+(off_t)length;
        lock_release(file->lk_file);
        *retval1=length;
        return fail;
    }else{
        lock_release(file->lk_file);
        return isWrite;
    }

}

//read reads up to buflen bytes from the file specified by fd, at the location in the file specified by the current seek position of the file, and stores them in the space pointed to by buf. The file must be open for reading. 

int
read(int fd, void *buf, size_t buflen, int32_t *retval1){
    struct iovec track;   //keeping track of blocks of data for I/O.
    struct uio  control;          //manage blocks of data moved around by the kernel
    struct ft* filetable=curproc->proc_ft; //exetract the current process's filetable 


    struct file *file;
    lock_acquire(filetable->lk_ft);
    if((fd<0||fd>=OPEN_MAX)&& (filetable->filetable[fd]!=NULL)){
        lock_release(filetable->lk_ft);
        return EBADF;
    }else{
       file=filetable->filetable[fd]; 
    }
    
    lock_acquire(file->lk_file);
    lock_release(filetable->lk_ft);
    if(!(file->flag & O_RDONLY || file->flag &O_RDWR)){
        lock_release(file->lk_file);
        return EBADF;
    }
    
   
    track.iov_len=buflen;
    track.iov_ubase=(userptr_t)buf;
    control.uio_space=curproc->p_addrspace;
    control.uio_iovcnt=1;
    control.uio_iov=&track;
    control.uio_resid=buflen;
    control.uio_segflg=UIO_USERSPACE;
    control.uio_rw=UIO_READ;
    control.uio_offset=file->offs;
    

    int fail=0;
    int isRead=VOP_READ(file->vn, &control);
    if(!isRead){
        file->offs +=(off_t) buflen-control.uio_resid;
        lock_release(file->lk_file);
        *retval1= buflen-control.uio_resid;
        return fail;
    }else{
        lock_release(file->lk_file);
        return isRead;
    }
}


//lseek alters the current seek position of the file handle filehandle, seeking to a new position based on pos and whence.
int
lseek(int fd, off_t pos, int whence, int32_t*retval1, int32_t *retval2){
    struct stat *info;
    struct file *file;
    struct ft *filetable=curproc->proc_ft;
    
    lock_acquire(filetable->lk_ft);

    if((fd<0||fd>=OPEN_MAX) && (filetable->filetable[fd]!=NULL)){
        lock_release(filetable->lk_ft);
        return EBADF;
    }else{
       file=filetable->filetable[fd]; 
    }
    lock_acquire(file->lk_file);
    lock_release(filetable->lk_ft);
    off_t seek_pos;

    if(VOP_ISSEEKABLE(file->vn)){
        if(whence==SEEK_SET){
            seek_pos=pos;
        }else if(whence==SEEK_CUR){
            seek_pos=file->offs+pos;
        }else if(whence==SEEK_END){
            info=kmalloc(sizeof(struct stat));
            int lseek=VOP_STAT(file->vn, info);
            if(!lseek){
                seek_pos=info->st_size+pos;
            }else{
                lock_release(file->lk_file);
                return lseek;
            }
        }else{
            lock_release(file->lk_file);
            return EINVAL;
        }

        if(seek_pos<0){
            lock_release(file->lk_file);
            return EINVAL;
        }

        file->offs=seek_pos;

        lock_release(file->lk_file);
        *retval1 = seek_pos >> 32;
        *retval2 = seek_pos & 0xFFFFFFFF;

        return 0;


    }else{
        lock_release(file->lk_file);
        return ESPIPE;
    }
}

/*
Close the file at position fd and handle invalid file by returning EBADF
@param position of the file descriptor
@return 0 if successfully close the file else EBADF
*/
int close(int fd){
    struct ft *ftable = curproc->proc_ft;
    lock_acquire(ftable->lk_ft);

    if(ftable == NULL || fd < 0 || fd >= OPEN_MAX || ftable->filetable[fd] == NULL) {          // check file descriptor range
        lock_release(ftable->lk_ft);
        return EBADF;
    }
    freeFd(ftable, fd);
    lock_release(ftable->lk_ft);
    return 0;
}

/*
Make a duplicate copy of the old file into the new file
@param oldFile  file to be copied
@param newFile  file to be copied to
@return 0 if successfully duplicate the file else EBADF
*/
int dup2(int oldFile, int newFile, int *out){   
    struct ft *ft = curproc->proc_ft;
    struct file *fileItem;
    if (newFile >= 0 && oldFile >= 0 && newFile < OPEN_MAX && oldFile < OPEN_MAX) {         //check if file index is valid
        
        lock_acquire(ft->lk_ft);

    if (ft!=NULL && oldFile >= 0 && oldFile < OPEN_MAX && ft->filetable[oldFile] != NULL) {   //check if old file is valid
        
        fileItem = ft->filetable[oldFile];

      if (ft!=NULL && newFile >= 0 && newFile < OPEN_MAX && ft->filetable[newFile] != NULL){   //check if new file is valid
        freeFd(ft, newFile);                                     
      }
        addNewFd(ft, fileItem, newFile);                          
        lock_release(ft->lk_ft);
        *out = newFile;  
        return 0;
    }
        lock_release(ft->lk_ft);
        return EBADF;
    }
        return EBADF;
}

/*
Changes the current working directory to a specified one
@param name - the directory to be changed to
*/
int chdir(const char *name)
{   
    int error;
    char *route;
    size_t *len;
    int output = vfs_chdir((char*)name);
    
    len = kmalloc(sizeof(int));
    route = kmalloc(PATH_MAX);
    error = copyinstr((const_userptr_t) name, route, PATH_MAX, len);   //copy string to kernel space address

    if (!error){
      kfree(route);                                       //free the old space
      kfree(len);

    if (output) { return output; }

    return 0;
    }
      kfree(route);
      kfree(len);
      return error;
}


/*
*Stores the current working directory at buf.
*@param buffer - directory holder
*@param size - length of the holder
*@param out - if succussfully store the content
*/
int __getcwd(char *buffer, size_t size, int32_t *out){   
    int temp;
    struct iovec track;
    struct uio control;
    
    track.iov_len=size;                                //Stores the current working directory at buf
    track.iov_ubase=(userptr_t)buffer;
    control.uio_space=curproc->p_addrspace;
    control.uio_iovcnt=1;
    control.uio_iov=&track;
    control.uio_resid=size;
    control.uio_segflg=UIO_USERSPACE;
    control.uio_rw=UIO_READ;
    control.uio_offset=0;                                               

    temp = vfs_getcwd(&control);
    if (!temp) {                                             //storation success
    *out = size - control.uio_resid;
    return 0;
    }
    return temp;
}



