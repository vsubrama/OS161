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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <synch.h>
#include <unistd.h>
/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	struct vnode *o, *i, *e;
	char *con0 = kstrdup("con:");
	char *con1 = kstrdup("con:");
	char *con2 = kstrdup("con:");
	int inpTemp = vfs_open(con0,O_RDONLY,0664,&i);
	int outTemp = vfs_open(con1,O_WRONLY,0664,&o);
	int errTemp = vfs_open(con2,O_WRONLY,0664,&e);
	KASSERT(inpTemp!=1);
	KASSERT(outTemp!=1);
	KASSERT(errTemp!=1);

	struct fTable *input, *output, *error;
	input= kmalloc(sizeof(struct fTable));
	output = kmalloc(sizeof(struct fTable));
	error = kmalloc(sizeof(struct fTable));
	KASSERT(input!=NULL);
	KASSERT(output!=NULL);
	KASSERT(error!=NULL);

	input->name=kstrdup("Standard_Input");
	input->offset=0;
	input->ref_count =0;
	input->status=O_RDONLY;
	input->vn=i;
	input->lock=lock_create("Standard Input");

	output->name=kstrdup("Standard_Output");
	output->offset=0;
	output->ref_count =0;
	output->status=O_WRONLY;
	output->vn=o;
	output->lock=lock_create("Standard Output");

	error->name=kstrdup("Standard_Error");
	error->offset=0;
	error->ref_count =0;
	error->status=O_WRONLY;
	error->vn=e;
	error->lock=lock_create("Standard Error");
	KASSERT(input->lock!=NULL);
	KASSERT(output->lock!=NULL);
	KASSERT(error->lock!=NULL);

	curthread->ft[STDIN_FILENO]=input;
	curthread->ft[STDOUT_FILENO]=output;
	curthread->ft[STDERR_FILENO]=error;
	kfree(con0);
	kfree(con1);
	kfree(con2);
	//kprintf("IO fd's initialized\n");
	/* Open the file. */
	kprintf("opening file : %s",progname);
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

