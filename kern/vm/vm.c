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
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
#include <clock.h>

/*
 * Working VM which is carved out of VM assignment :)
 * Author : Babu
 */

/* under dumbvm, always have 48k of user stack */
//#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

struct lock *lock_coremap = NULL;

void
vm_bootstrap(void)
{
	paddr_t paddr_first = 0;
	paddr_t paddr_last = 0;
	paddr_t paddr_free = 0;

	/* Initialize coremap */
	ram_getsize(&paddr_first, &paddr_last);


	page_num = ROUNDUP(paddr_last, PAGE_SIZE) / PAGE_SIZE;// :| Why rounddown?
	//page_num = (paddr_last - paddr_free) / PAGE_SIZE;


	/* pages should be a kernel virtual address !!  */
	pages = (struct page *) PADDR_TO_KVADDR(paddr_first);
	paddr_free = paddr_first + (page_num * PTE_SIZE); // core map size

	// Mapping coremap elements(page table entry - PTE) to their respective virtual memory.
	vaddr_t tmp_addr = PADDR_TO_KVADDR(paddr_free);
	for (uint32_t i = 0; i < page_num; i++)
	{
		(pages + (i * PTE_SIZE))-> virtual_addr = tmp_addr;
		(pages + (i * PTE_SIZE))-> num_pages = 1;
		(pages + (i * PTE_SIZE))-> page_state = FREE;
		(pages + (i * PTE_SIZE))-> timestamp = 0;
		tmp_addr += PAGE_SIZE;
	}

	/*initialize page lock*/
	lock_coremap = lock_create("coremap_lock");
	KASSERT(lock_coremap != NULL);
}

/**
 * Get pages by stealing memory from ram.
 * Invoked during boot sequence as the vm might have not been initialized.
 * Should never be called once vm is initialized
 * Modified by : Babu
 */
static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);

	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{

	paddr_t alloc_mem = 0;
	if(lock_coremap != NULL)
		alloc_mem = page_nalloc(npages);
	else
		alloc_mem = getppages(npages);
	return PADDR_TO_KVADDR(alloc_mem);
}

void 
free_kpages(vaddr_t addr)
{
	lock_acquire(lock_coremap);

	struct page *find_page = pages;

	while(find_page->virtual_addr != addr)
	{
		find_page += PTE_SIZE;
	}

	//Check whether this is the page we are looking for to de-allocate.
	KASSERT(find_page->virtual_addr == addr);

	int no_dealloc = find_page->num_pages;
	for (int i = 0; i < no_dealloc; ++i)
	{
		find_page->page_state =  FREE;
		find_page->timestamp = 0;
		find_page->num_pages = 1;
		as_destroy(find_page->addrspce);

		find_page += (PTE_SIZE);
	}

	lock_release(lock_coremap);

}


void
page_free(vaddr_t addr)
{
	lock_acquire(lock_coremap);

	struct page *find_page = pages;

	while(find_page->virtual_addr != addr)
	{
		find_page += PTE_SIZE;
	}

	//Check whether this is the page we are looking for to de-allocate.
	KASSERT(find_page->virtual_addr == addr);

	int32_t no_dealloc = find_page->num_pages;
	for (int32_t i = 0; i < no_dealloc; ++i)
	{
		find_page->page_state =  FREE;
		find_page->timestamp = 0;
		find_page->num_pages = 1;
		//as_destroy(find_page->addrspce); user space ---??

		find_page += (PTE_SIZE);
	}

	lock_release(lock_coremap);

}


/**
 * page_alloc - Page allocation for user program
 */
vaddr_t
page_alloc()
{
	lock_acquire(lock_coremap);
	/**
	 * Find some free memory and try to allocate to the caller
	 */
	struct page *free_page = pages;
	uint32_t i = 0;
	//int32_t oldest_page_timestamp = 0xffffffff;
	//int32_t oldest_page_num = -1;
	for(i=0;i < page_num; i++)
	{
		// TODO : allocate the pages if its not fixed untill swapping is implemented
		/*Logic for page swapping - FIFO*/
		/*if(free_page -> timestamp < oldest_page_timestamp)
		{
			oldest_page_timestamp = free_page -> timestamp;
			oldest_page_num = i;
		}*/
		if(free_page ->page_state == FREE) //if(free_page ->page_state != FIXED)
			break;
		free_page += PTE_SIZE;
	}

	//free_page = pages + (oldest_page_num * PTE_SIZE);

	// The oldest page has to be made available and used
	make_page_avail(free_page);

	free_page->addrspce = as_create();
	free_page->num_pages = 1;

	time_t current_time = 0;
	//gettime(&current_time, NULL);
	if(current_time != -1)
		free_page->timestamp = current_time;
	else
		return EFAULT;

	bzero((void *)free_page->virtual_addr, PAGE_SIZE);

	make_page_avail(free_page);

	lock_release(lock_coremap);

	return free_page->virtual_addr;
}

vaddr_t
page_nalloc(unsigned long npages)
{
	lock_acquire(lock_coremap);
	/**
	 * Find some free memory and try to allocate to the caller
	 */
	struct page *free_page = pages;

	uint32_t start_page = 0;
	uint32_t free_page_cnt = 0;
	for(start_page=0;start_page < page_num;start_page++)
	{
		if(free_page ->page_state == FREE)
		{
			for (free_page_cnt=0; free_page_cnt<npages; free_page_cnt++)
			{
				free_page += PTE_SIZE;
				if(free_page->page_state != FREE)
					break;
			}

			if(free_page_cnt == npages)
				break;
			else
				start_page += free_page_cnt - 1;

			free_page += PTE_SIZE;
		}
	}

	// what if there is no free  page?
	// Follow page replacement algorithm
	// As of now we take a random page and flush it
	if(free_page_cnt  == 0)
	{
		free_page = pages;
		free_page_cnt = npages;
	}

	// Check at the end of the iteration whether we got the
	// number of pages we want
	KASSERT(free_page_cnt == npages);

	free_page = pages + (start_page * PTE_SIZE);

	// Make the entire page available

	for (free_page_cnt=0; free_page_cnt<npages; free_page_cnt++)
	{
		make_page_avail(free_page);

		free_page->addrspce = as_create();
		free_page->num_pages = npages;

		time_t current_time = 0;
		//gettime(&current_time, NULL);
		if(current_time != -1)
			free_page->timestamp = current_time;
		else
			return EFAULT;

		// Zeroing the page before returning
		bzero((void *)free_page->virtual_addr, PAGE_SIZE);

		free_page += PTE_SIZE;

	}
	lock_release(lock_coremap);

	// Start of n chunks of free pages
	free_page = pages + (start_page * PTE_SIZE);
	return free_page->virtual_addr;

}


/**
 * This function will make the page available for either by evicting the page to disk
 * a.k.a swapping out or by flushing it
 * Author : Babu
 */
int32_t
make_page_avail(struct page *free_page)
{

	// as of now just making the page state as free by flushing it
	// TODO : implement swapping in this function

	// Allocating it as dirty for the first time as the disk will not have a copy
	free_page->page_state = DIRTY;
	bzero((void *)free_page->virtual_addr, PAGE_SIZE);

	return 0;
}


void
vm_tlbshootdown_all(void)
{
	lock_acquire(lock_coremap);

	/* TODO */

	lock_release(lock_coremap);
}



void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	lock_acquire(lock_coremap);

	/* TODO */

	lock_release(lock_coremap);
	(void)ts;

}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	lock_acquire(lock_coremap);

	/* TODO */

	lock_release(lock_coremap);
	(void) faulttype;
	(void) faultaddress;
	return EUNIMP;
}
