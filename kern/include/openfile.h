/*
+ * File handles.
+ */

#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <spinlock.h>


/*
+ * Structure for open files.
+ *
+ * This is pretty much just a wrapper around a vnode; the important
+ * additional things we keep here are the open mode and the file's
+ * seek position.
+ *
+ * Open files are reference-counted because they get shared via fork
+ * and dup2 calls. And they need locking because that sharing can be
+ * among multiple concurrent processes.
+ */
struct openfile {
	struct vnode *of_vnode;
	int of_accmode;	/* from open: O_RDONLY, O_WRONLY, or O_RDWR */

	struct lock *of_offsetlock;	/* lock for of_offset */
	off_t of_offset;

	struct spinlock of_reflock;	/* lock for of_refcount */
	int of_refcount;
};

/* open a file (args must be kernel pointers; destroys filename) */
int openfile_open(char *filename, int openflags, mode_t mode,
		  struct openfile **ret);

/* adjust the refcount on an openfile */
void openfile_incref(struct openfile *);
void openfile_decref(struct openfile *);


#endif /* _OPENFILE_H_ */