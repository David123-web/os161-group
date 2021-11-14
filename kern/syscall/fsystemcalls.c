#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>
#include <fsystemcalls.h>

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
int
open(const_userptr_t upath, int flags, mode_t mode, int *retval)
{
	const int allflags =
		O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;

	char *kpath;
	struct openfile *file;
	int result;

	if ((flags & allflags) != flags) {
		/* unknown flags were set */
		return EINVAL;
	}

	kpath = kmalloc(PATH_MAX);
	if (kpath == NULL) {
		return ENOMEM;
	}

	/* Get the pathname. */
	result = copyinstr(upath, kpath, PATH_MAX, NULL);
	if (result) {
		kfree(kpath);
		return result;
	}

	/*
	 * Open the file. Code lower down (in vfs_open) checks that
	 * flags & O_ACCMODE is a valid value.
	 */
	result = openfile_open(kpath, flags, mode, &file);
	if (result) {
		kfree(kpath);
		return result;
	}
	kfree(kpath);

	/*
	 * Place the file in our process's file table, which gives us
	 * the result file descriptor.
	 */
	result = filetable_place(curproc->proc_ft, file, retval);
	if (result) {
		openfile_decref(file);
		return result;
	}

	return 0;
}

/*
 * Common logic for read and write.
 *
 * Look up the fd, then use VOP_READ or VOP_WRITE.
 */
static
int
readwrite(int fd, userptr_t buf, size_t size, enum uio_rw rw,
	      int badaccmode, ssize_t *retval)
{
	struct openfile *file;
	bool locked;
	off_t pos;
	struct iovec iov;
	struct uio useruio;
	int result;

	/* better be a valid file descriptor */
	result = filetable_get(curproc->proc_ft, fd, &file);
	if (result) {
		return result;
	}

	/* Only lock the seek position if we're really using it. */
	locked = VOP_ISSEEKABLE(file->of_vnode);
	if (locked) {
		lock_acquire(file->of_offsetlock);
		pos = file->of_offset;
	}
	else {
		pos = 0;
	}

	if (file->of_accmode == badaccmode) {
		result = EBADF;
		goto fail;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	uio_uinit(&iov, &useruio, buf, size, pos, rw);

	/* do the read or write */
	result = (rw == UIO_READ) ?
		VOP_READ(file->of_vnode, &useruio) :
		VOP_WRITE(file->of_vnode, &useruio);
	if (result) {
		goto fail;
	}

	if (locked) {
		/* set the offset to the updated offset in the uio */
		file->of_offset = useruio.uio_offset;
		lock_release(file->of_offsetlock);
	}

	filetable_put(curproc->proc_ft, fd, file);

	/*
	 * The amount read (or written) is the original buffer size,
	 * minus how much is left in it.
	 */
	*retval = size - useruio.uio_resid;

	return 0;

fail:
	if (locked) {
		lock_release(file->of_offsetlock);
	}
	filetable_put( curproc->proc_ft, fd, file);
	return result;
}

/*
 * read() - use readwrite
 */
int
read(int fd, userptr_t buf, size_t size, int *retval)
{
	return readwrite(fd, buf, size, UIO_READ, O_WRONLY, retval);
}

/*
 * write() - use readwrite
 */
int
write(int fd, userptr_t buf, size_t size, int *retval)
{
	return readwrite(fd, buf, size, UIO_WRITE, O_RDONLY, retval);
}

/*
 * close() - remove from the file table.
 */
int
close(int fd)
{
	struct filetable *ft;
	struct openfile *file;

	ft =  curproc->proc_ft;

	/* check if the file's in range before calling placeat */
	if (!filetable_okfd(ft, fd)) {
		return EBADF;
	}

	/* place null in the filetable and get the file previously there */
	filetable_placeat(ft, NULL, fd, &file);

	if (file == NULL) {
		/* oops, it wasn't open, that's an error */
		return EBADF;
	}

	/* drop the reference */
	openfile_decref(file);
	return 0;
}

/*
 * lseek() - manipulate the seek position.
 */
int
lseek(int fd, off_t offset, int whence, off_t *retval)
{
	struct stat info;
	struct openfile *file;
	int result;

	/* Get the open file. */
	result = filetable_get( curproc->proc_ft, fd, &file);
	if (result) {
		return result;
	}

	/* If it's not a seekable object, forget about it. */
	if (!VOP_ISSEEKABLE(file->of_vnode)) {
		filetable_put( curproc->proc_ft, fd, file);
		return ESPIPE;
	}

	/* Lock the seek position. */
	lock_acquire(file->of_offsetlock);

	/* Compute the new position. */
	switch (whence) {
	    case SEEK_SET:
		*retval = offset;
		break;
	    case SEEK_CUR:
		*retval = file->of_offset + offset;    
		break;
	    case SEEK_END:
		result = VOP_STAT(file->of_vnode, &info);
		if (result) {
			lock_release(file->of_offsetlock);
			filetable_put( curproc->proc_ft, fd, file);
			return result;
		}
		*retval = info.st_size + offset;      
		break;
	    default:
		lock_release(file->of_offsetlock);
		filetable_put( curproc->proc_ft, fd, file);
		return EINVAL;
	}

	/* If the resulting position is negative (which is invalid) fail. */
	if (*retval < 0) {
		lock_release(file->of_offsetlock);
		filetable_put( curproc->proc_ft, fd, file);
		return EINVAL;
	}

	/* Success -- update the file structure with the new position. */
	file->of_offset = *retval;

	lock_release(file->of_offsetlock);
	filetable_put( curproc->proc_ft, fd, file);

	return 0;
}

/*
 * dup2() - clone a file descriptor.
 */
int
dup2(int oldfd, int newfd, int *retval)
{
	struct filetable *ft;
	struct openfile *oldfdfile, *newfdfile;
	int result;

	ft =  curproc->proc_ft;

	if (!filetable_okfd(ft, newfd)) {
		return EBADF;
	}

	/* dup2'ing an fd to itself automatically succeeds (BSD semantics) */
	if (oldfd == newfd) {
		*retval = newfd;
		return 0;
	}

	/* get the file */
	result = filetable_get(ft, oldfd, &oldfdfile);
	if (result) {
		return result;
	}

	/* make another reference and return the fd */
	openfile_incref(oldfdfile);
	filetable_put(ft, oldfd, oldfdfile);

	/* place it */
	filetable_placeat(ft, oldfdfile, newfd, &newfdfile);

	/* if there was a file already there, drop that reference */
	if (newfdfile != NULL) {
		openfile_decref(newfdfile);
	}

	/* return newfd */
	*retval = newfd;
	return 0;
}

/*
 * chdir() - change directory. Send the path off to the vfs layer.
 */
int
chdir(const_userptr_t path)
{
	char *pathbuf;
	int result;

	pathbuf = kmalloc(PATH_MAX);
	if (pathbuf == NULL) {
		return ENOMEM;
	}
	result = copyinstr(path, pathbuf, PATH_MAX, NULL);
	if (result) {
		kfree(pathbuf);
		return result;
	}

	result = vfs_chdir(pathbuf);
	kfree(pathbuf);
	return result;
}

/*
 * __getcwd() - get current directory. Make a uio and get the data
 * from the VFS code.
 */
int
__getcwd(userptr_t buf, size_t buflen, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	int result;

	uio_uinit(&iov, &useruio, buf, buflen, 0, UIO_READ);

	result = vfs_getcwd(&useruio);
	if (result) {
		return result;
	}

	*retval = buflen - useruio.uio_resid;
	return 0;
}