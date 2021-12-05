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
#include <kern/errno.h>
#include <types.h>
#include <spl.h>
#include <lib.h>
#include <current.h>
#include <spinlock.h>
#include <proc.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

#define DUMBVM_STACKPAGES    18


//////////////////////////////////////////
//newly added page table functions

/*
 * Initialize a new pagetable
 */
struct pt_entry *pt_create(size_t size) {
    struct pt_entry *pt = (struct pt_entry *) kmalloc(size*sizeof(struct pt_entry));
    KASSERT(pt != NULL);
    return pt;
}

/*
 * Find the page entry using address vaddr.
 */
struct pt_entry *get_pt(struct addrspace *as, vaddr_t vaddr) {
    int  i=0;
    int size = sizeof(as->as_pages)/sizeof(as->as_pages[0]);
	while(i<size){
		if(as->as_pages[i].pt_vaddr == vaddr) {              // Search for the pt with vaddr
            return &as->as_pages[i];
        }
        i++;
	}
    return NULL;
}

/*
 * Free the pagetable.
 */
void pt_free(struct pt_entry *pt) {
    int i=0;
    int size = sizeof(pt)/sizeof(pt[0]);
	while(i < size){
           kfree(&pt[i]);                     //free each entry
		   i++;
	}
}

/*
 * Resize the pt by twice using an old pt
 */
struct pt_entry* pt_resize(struct pt_entry *pt, size_t prevSize) {

    struct pt_entry *newPt = pt_create(prevSize*2);

    for(size_t i=0; i < prevSize; i++){
		newPt[i].pt_vaddr = pt[i].pt_vaddr;
        newPt[i].pt_paddr = pt[i].pt_paddr;
        newPt[i].pt_flag = pt[i].pt_flag;
    }

    for(size_t j=prevSize; j < prevSize*2; j++){
        newPt[j].pt_vaddr = 0;
        newPt[j].pt_paddr = 0;
		newPt[j].pt_flag = 0;
    }

    return newPt;
}

/////////////////////////////////
//as functions
/*
 * Allocate space for holding information of address space and return allocated addrspace struct 
 */
struct addrspace *as_create(void){
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	//A6
	as->stack_start = (vaddr_t) 0;
	as->stack_end = (vaddr_t) 0;
	as->heap_start = (vaddr_t) 0;
	as->heap_end = (vaddr_t) 0;
	as->rlist = NULL;
	as->as_pages = NULL;

	return as;
}

/*
 * Destroy the address space
 */
void as_destroy(struct addrspace *as){
    kfree(as->rlist);   // Free the region list
    pt_free(as->as_pages);      // Free the page table
    kfree(as);              //free the as
}

/*
 * TLB shootdown on the current address space
 */
void as_activate(void){
	int spl = splhigh();         // disable interrupts ?
	splx(spl);
}

void as_deactivate(void){}

/*
 * Set up a new segment in VM from VADDR 
 */
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable) {
    size_t npages;
    struct region *regionlist;
	int rSize;
    int permit;
    
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

    rSize = sizeof(as->rlist)/sizeof(as->rlist[0]);     // Get size of regionlist
	permit = 7 & (readable | writeable | executable);  // Get permit
    
    /*resize region by 2 if needed */
    if(as->rlist[rSize-1].as_vbase > 0) {
        
        regionlist = (struct region*) kmalloc(sizeof(struct region)*(rSize + 2));      // Copy data 

        if(regionlist == NULL) { return ENOMEM; }
        
		int i = 0;
        while(i < rSize){
			regionlist[i].as_pbase = as->rlist[i].as_pbase;
			regionlist[i].as_vbase = as->rlist[i].as_vbase;
			regionlist[i].region_flag = as->rlist[i].region_flag;
            regionlist[i].as_npages = as->rlist[i].as_npages;
			i++;
		}
        
        for(int j=0; j<2; j++) {                    // Initialize last two additional regions 
		    regionlist[rSize+j].as_pbase = 0;
            regionlist[rSize+j].as_vbase = 0;
            regionlist[rSize+j].as_npages = 0;
            regionlist[rSize+j].region_flag = 0;
        }
        rSize += 2;

        kfree(as->rlist);            // free old regionlist
        as->rlist = regionlist;
    }        

	as->rlist[rSize-2].as_pbase = 0;            //set up new region
    as->rlist[rSize-2].as_vbase = vaddr;
    as->rlist[rSize-2].as_npages = npages;
    as->rlist[rSize-2].region_flag = permit;

	return 0;
}

/*
 * Returns the USERSTACK inside stackptr
 */
int as_define_stack(struct addrspace *as, vaddr_t *stackptr){
    (void)as;
	*stackptr = USERSTACK;
	return 0;
}

/*
 * Get physical pages for each as region. Return 0 if successful else errno.
 */
int as_prepare_load(struct addrspace *as){
    
    vaddr_t  stack_vaddr, vaddr;
    paddr_t  stack_paddr, paddr;

	struct region *regionlist = as->rlist;
    size_t r_size = sizeof(regionlist)/sizeof(regionlist[0]);

    size_t i=0, k=0;
    size_t numP = 0;
    
    int permit = 7 & ( 100 | 010 | 000 );  //Assign permit to read/write

    for(i = 0; i < r_size; i++) {
        numP += as->rlist[i].as_npages;      //get total number of pages
    }
    
	size_t tmp = 0;
    if((numP&(numP-1))!=0) {                  // Round up numP to next power of two
	    size_t p = 2;
        while(numP >>= 1){p <<= 1;} 
        tmp = p;
        KASSERT(tmp > numP);
        numP = tmp;
    }

    /*
     * Allocate physical pages for each region at powers of two
     */
    size_t ptSize = 0;
    if(as->as_pages == NULL) {                       // Create a new page table if null
        as->as_pages = pt_create(INITIAL_SIZE);
        if(as->as_pages == NULL) {
            return ENOMEM;
        }
        ptSize = 64;
    }

    else {
        ptSize = sizeof(as->as_pages)/sizeof(as->as_pages[0]);
        while(as->as_pages[i].pt_vaddr != 0) {             //get where the last pt entry lies           i?
            i++;
        }
    }
    
    if(numP > ptSize || numP > ptSize - i) {               //if no enough space, resize it
        as->as_pages = pt_resize(as->as_pages, ptSize);
    }
    
    k = i;   
    for(i=0; i < r_size; i++) {                           //fill the page table from the last index
    
        vaddr = regionlist[i].as_vbase;
        as->as_pages[k].pt_vaddr = vaddr;
        paddr = getppages(1);

        if(paddr == 0) { return ENOMEM; }
        
		as->as_pages[k].pt_flag = permit;
        as->as_pages[k].pt_paddr = paddr;
        
        vaddr += PAGE_SIZE;
		k++;
         
        for(size_t j=0; j < regionlist[i].as_npages; j++) {      // Fill subsequent pt entries.
        
            as->as_pages[k].pt_vaddr = vaddr;
            paddr = alloc_kpages(1);
            if(paddr == 0) { return ENOMEM;}
            as->as_pages[k].pt_paddr = paddr;
            as->as_pages[k].pt_flag = permit;
            
			k++;
            vaddr += PAGE_SIZE;
        }
    }
    
    as_define_stack(as, &stack_vaddr);    //allocate pages for stack and heap
    
    as->as_pages[k].pt_vaddr = stack_vaddr;   //set up start of stack
    stack_paddr = getppages(1);
    if(stack_paddr == 0) { return ENOMEM; }
    
    as->stack_start = as->stack_end = stack_vaddr;
	k++;
    
    as->as_pages[k].pt_vaddr = vaddr;         //set up start of heap
    paddr = getppages(1);
    if(paddr == 0) { return ENOMEM; }
    k++;
    as->heap_start = as->heap_end = vaddr; 

	return 0;
}

/*
 * Restore permit value
 */
int as_complete_load(struct addrspace *as){

    size_t i=0, j=0, k=0;
    size_t npages;
	struct region *reglist = as->rlist;
    size_t size = sizeof(reglist)/sizeof(reglist[0]);

    while(i < size){
		k=i+j;
		npages = reglist[i].as_npages;
        for(j=0; j<npages; j++) {
            as->as_pages[k].pt_flag = reglist[i].region_flag;
        }

		i++;
	}
    
	return 0;
}

/*
 * Make a copy of old addrspace. Return 0 if succeed
 */
int as_copy(struct addrspace *old, struct addrspace **ret){
	struct addrspace *new;
    new = as_create();
	if (new == NULL) { return ENOMEM; }

    int r_size = sizeof(old->rlist)/sizeof(old->rlist[0]);          // Copy region list
    new->rlist = (struct region*) kmalloc(sizeof(struct region)*r_size);
    if(new->rlist == NULL) { return ENOMEM; }
    
    for(int i = 0; i < r_size; i++) {
		new->rlist[i].as_pbase = old->rlist[i].as_pbase;
        new->rlist[i].as_vbase = old->rlist[i].as_vbase;
        new->rlist[i].as_npages = old->rlist[i].as_npages;
        new->rlist[i].region_flag = old->rlist[i].region_flag;
    }
    
	if (as_prepare_load(new)) {                   	// Allocate physical pages 
		as_destroy(new);
		return ENOMEM;
	}

    int pt_size = sizeof(old->as_pages)/sizeof(old->as_pages[0]);         // Copy pt mappings 

    for(int j = 0; j < pt_size; j++) {
        memmove((void *)PADDR_TO_KVADDR(new->as_pages[j].pt_paddr),           
                (const void *)PADDR_TO_KVADDR(old->as_pages[j].pt_paddr),
                PAGE_SIZE);

        new->as_pages[j].pt_vaddr = old->as_pages[j].pt_vaddr;
        new->as_pages[j].pt_paddr = old->as_pages[j].pt_paddr;
		new->as_pages[j].pt_flag = old->as_pages[j].pt_flag;
    }

	*ret = new;
	return 0;
}





