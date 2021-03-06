
#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */

#include <vm.h>
#include "opt-dumbvm.h"

struct vnode;

/* Initalize pagetable */
#define INITIAL_SIZE 64
struct pt_entry {                      
    int pt_flag;                                    
    paddr_t pt_paddr;      // physical address 
    vaddr_t pt_vaddr;      // virtual address 
};

struct region {
    paddr_t as_pbase;
    vaddr_t as_vbase;
    int region_flag;
    size_t as_npages;
};

/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */
struct addrspace {
#if OPT_DUMBVM
    vaddr_t as_vbase1;
    paddr_t as_pbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    paddr_t as_pbase2;
    size_t as_npages2;
    paddr_t as_stackpbase;
#else
    struct region *rlist;    //more regions instead of two

    vaddr_t stack_start, stack_end;   //growing stack

    vaddr_t heap_start, heap_end;   //growing heap

    struct pt_entry *as_pages;

#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);


/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

//A6 added here
struct pt_entry *pt_create(size_t size);   //initialize pagetable
struct pt_entry *get_pt(struct addrspace *as, vaddr_t vaddr);   //get pagetable pointer
void pt_free(struct pt_entry *pt);      //free pagetable
struct pt_entry *pt_resize(struct pt_entry *pt, size_t prevSize); //resize pagetable

#endif /* _ADDRSPACE_H_ */