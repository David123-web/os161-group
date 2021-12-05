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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/vm.h>
#include <mips/vm.h>


/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES    18


// create coremap to keep track of all allocated/non-allocated physical pages and their location in physical memory
struct coremap* coremap;

//number of coremap pages
static unsigned long npage;

//avalible physical addr

paddr_t av_pa;

//determine whether vm is bootstraped or not

bool vm_init=false;

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

//initialize the vm
void
vm_bootstrap(void)
{
	paddr_t faddr, laddr;

	//set up ram and settle down its range
	faddr=ram_getfirstfree();
	laddr=ram_getsize();
	
	//allocate coremap
	coremap=(struct coremap*) PADDR_TO_KVADDR(faddr);
	
	//find out how many physical pages are in ram
	int nppage;

	nppage=(laddr-faddr)/PAGE_SIZE;
	npage=nppage;

	
	//allocate avabile phsyical addr to back up the virtual region
	av_pa=ROUNDUP((faddr+nppage*(sizeof(struct coremap))), PAGE_SIZE);
	
	
	int i=0;
	while(i<nppage){
		if(i<(int)(faddr/PAGE_SIZE)){
			//used adddr are dirty
			coremap[i].state=DIRTY;
		}else if(i<((int)(av_pa-faddr)/PAGE_SIZE) && i>((int)faddr/PAGE_SIZE)){
			//coremap pages are fixed
			coremap[i].state=FIXED;
		}else{
			//remaining pages are free
			coremap[i].state=FREED;
		}

		
		coremap[i].vas=PADDR_TO_KVADDR((paddr_t)i*PAGE_SIZE+av_pa);
		i++;
	}

	vm_init=true;


}

//get the available phsyical pages
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
	unsigned long pstart=0;
	unsigned long nump=0;

	if(vm_init){
		spinlock_acquire(&stealmem_lock);
		unsigned int i=0;
		while(i<npage && nump!=npages){
				if(coremap[i].state==FREED){
					nump++;
				}else{
					
					//reset the start of the block
					pstart=i+1;
					
					//reset the number of pages to zero if the block is not contiguous
					nump=0;
				}
			i++;
		}
		//return 0 if there isn't enough pages
		if(i==npage && nump!=npages){
			spinlock_release(&stealmem_lock);
			return 0;
		}
		//block is in use
		coremap[pstart].state=DIRTY;
		//the block is contiguous
		coremap[pstart].isContiguous=true;
		//block size
		coremap[pstart].bsize=npages;

		for(i=0; i<npages-1; i++){
			//block is in use
			coremap[i+1+pstart].state=DIRTY;
			//the block is not contiguous
			coremap[i+1+pstart].isContiguous=false;
			//block size
			coremap[i+1+pstart].bsize=npages;
		}

		addr=(paddr_t)pstart*PAGE_SIZE+av_pa;

	}else{
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
	}

	spinlock_release(&stealmem_lock);
	return addr;		
	
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	unsigned int i=0;
	unsigned int k=0;
	spinlock_acquire(&stealmem_lock);
	//prob through the coremap to find the matching vaddr
	while(i<npage){
		if(coremap[i].vas==addr){
			break;
		}
		i++;
	}
	//free the corresponding page
	while(k<coremap[i].bsize){
		coremap[i+k].bsize=0;
		coremap[i+k].isContiguous=false;
		coremap[i+k].state=FREED;
		
	}

	spinlock_release(&stealmem_lock);
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

//handle eviction and TLB updates
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase, vtop;
	paddr_t paddr;
	unsigned int i=0;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	struct pt_entry* pt;
	struct region region;
	size_t nreg;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->stack_start != 0);
	KASSERT(as->stack_end != 0);
	KASSERT(as->heap_start != 0);
	KASSERT(as->heap_end != 0);
	KASSERT(as->rlist != NULL);
	KASSERT(as->as_pages!= NULL);
	KASSERT((as->stack_start & PAGE_FRAME) == as->stack_start);
	KASSERT((as->stack_end & PAGE_FRAME) == as->stack_end);
	KASSERT((as->heap_start & PAGE_FRAME) == as->heap_start);
	KASSERT((as->heap_end & PAGE_FRAME) == as->heap_end);
	
	//check how many regions are in the addrspace
	nreg=sizeof(as->rlist)/sizeof(as->rlist[0]);

	//????????check whether the regions are all valid or not
	while(i<nreg){
		vbase=as->rlist[i].as_vbase;
		vtop=vbase+as->rlist[i].as_npages;
		//get the valid region
		region=as->rlist[i];
		if(faultaddress<vtop && faultaddress>=vbase){
			break;
		}
		i++;
	}

	//if the vaddr is invalid return an error
	if(i==nreg){
		if(faultaddress<as->stack_start|| faultaddress>=as->stack_end
		|| faultaddress<as->heap_start|| faultaddress>=as->heap_end){
			return EFAULT;

		}
	}

	//retireve page entry
	pt=get_pt(as, faultaddress);

	if(!pt){
		//page fault
		//get the size of the pagetable
		size_t pagetable_size=sizeof(as->as_pages)/sizeof(as->as_pages[0]);

		//allocate one physical page 
		paddr=getppages(1);

		//if no available page return an error
		if(paddr==0){
			return ENOMEM;
		}

		//resize page table

		if( as->as_pages[pagetable_size-1].pt_vaddr != 0){
			as->as_pages=pt_resize(as->as_pages, pagetable_size);
			as->as_pages[pagetable_size].pt_vaddr=faultaddress;
			as->as_pages[pagetable_size].pt_paddr=paddr;
			as->as_pages[pagetable_size].pt_flag=region.region_flag;
		}else{
			//if not able to resize evict the last page entry
			as->as_pages[pagetable_size-1].pt_vaddr=faultaddress;
			as->as_pages[pagetable_size-1].pt_paddr=paddr;
			as->as_pages[pagetable_size-1].pt_flag=region.region_flag;
		}


	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}

