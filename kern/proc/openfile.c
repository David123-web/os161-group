/*
  * File handles.
  */

 #include <types.h>
 #include <kern/errno.h>
 #include <kern/fcntl.h>
 #include <lib.h>
 #include <synch.h>
 #include <vfs.h>
 #include <openfile.h>
 
 /*
  * Constructor for struct openfile.
  */
 static
 struct openfile *
 openfile_create(struct vnode *vn, int accmode)
 {
 	struct openfile *file;
 
 	/* this should already have been checked (e.g. by vfs_open) */
 	KASSERT(accmode == O_RDONLY ||
 		accmode == O_WRONLY ||
 		accmode == O_RDWR);
 
 	file = kmalloc(sizeof(struct openfile));
 	if (file == NULL) {
 		return NULL;
 	}
 
 	file->of_offsetlock = lock_create("openfile");
 	if (file->of_offsetlock == NULL) {
 		kfree(file);
 		return NULL;
 	}
 
 	spinlock_init(&file->of_reflock);
 
 	file->of_vnode = vn;
 	file->of_accmode = accmode;
 	file->of_offset = 0;
 	file->of_refcount = 1;
 
 	return file;
 }
 
 /*
  * Destructor for struct openfile. Private; should only be used via
  * openfile_decref().
  */
 static
 void
 openfile_destroy(struct openfile *file)
 {
 	/* balance vfs_open with vfs_close (not VOP_DECREF) */
 	vfs_close(file->of_vnode);
 
 	spinlock_cleanup(&file->of_reflock);
 	lock_destroy(file->of_offsetlock);
 	kfree(file);
 }
 
 /*
  * Open a file (with vfs_open) and wrap it in an openfile object.
  */
 int
 openfile_open(char *filename, int openflags, mode_t mode,
 	      struct openfile **ret)
 {
 	struct vnode *vn;
 	struct openfile *file;
 	int result;
 
 	result = vfs_open(filename, openflags, mode, &vn);
 	if (result) {
 		return result;
 	}
 
 	file = openfile_create(vn, openflags & O_ACCMODE);
 	if (file == NULL) {
 		vfs_close(vn);
 		return ENOMEM;
 	}
 
 	*ret = file;
 	return 0;
 }
 
 /*
  * Increment the reference count on an openfile.
  */
 void
 openfile_incref(struct openfile *file)
 {
 	spinlock_acquire(&file->of_reflock);
 	file->of_refcount++  ;   //////////////////////
 	spinlock_release(&file->of_reflock);
 }
 
 /*
  * Decrement the reference count on an openfile. Destroys it when the
  * reference count reaches zero.
  */
 void
 openfile_decref(struct openfile *file)
 {
 	spinlock_acquire(&file->of_reflock);
 
 	/* if this is the last close of this file, free it up */
 	if (file->of_refcount == 1) {
 		spinlock_release(&file->of_reflock);
 		openfile_destroy(file);
 	}
 	else {
 		KASSERT(file->of_refcount > 1);
 		file->of_refcount--;
 		spinlock_release(&file->of_reflock);
 	}
 }
