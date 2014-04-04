/*
 * process.c
 *
 *  Created on: Mar 8, 2014
 *  Author: Babu
 */

//#include <process.h>

#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <addrspace.h>
#include <spl.h>
#include <synch.h>
#include <limits.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <vfs.h>

struct pid_pool
{
	pid_t pid_avail; // Available pid
	struct pid_pool *next; // Point to next available pid
};

pid_t global_pid_count = 1;

/* Head and tail pointer for query and insert operation from pool which
 * follows FIFO approach - Added by Babu
 */
struct pid_pool *head;
struct pid_pool *tail;

/**
 * When a process exits it should invoke this method so that
 * this will add the allocated pid to free pool which can be assigned
 * for other process  - Added by Babu
 */
void
add_pid_to_pool(pid_t pid_free)
{
	if(tail == NULL)
	{
		tail = kmalloc(sizeof(struct pid_pool));
		if(tail == NULL)
			panic("Unable to create pid pool.");
		else
			tail->pid_avail = pid_free;
	}
	else
	{
		tail->next = kmalloc(sizeof(struct pid_pool));
		if(tail->next == NULL)
			panic("Unable to create pid pool element.");
		else
		{
			tail = tail->next;
			tail->pid_avail = pid_free;
		}
	}
	if(head == NULL)
		head = tail;

}

/**
 * Allocate pid based on the pids available in the pool or
 * generate from the counter if the pool is empty  - Added by Babu
 */
pid_t
allocate_pid()
{
	pid_t pid_alloc;
	struct pid_pool *temp;
	if(head == NULL)
	{
		pid_alloc = global_pid_count++;
	}
	else
	{
		pid_alloc = head->pid_avail;
		temp = head;
		if(head == tail)
		{
			head = NULL;
			tail = NULL;
		}
		else
			head = head->next;
		kfree(temp);
	}
	return pid_alloc;
}

/**
 * Added by Babu : 04/02/2014
 * Function to destroy process structure
 */
void
process_destroy(struct process *process)
{
   //int retval = -1;
	/* Adding pid to available pool so that it can be allocated to other processes	 */
	add_pid_to_pool(process->p_pid_self);
	/*TODO : Commenting as of now. Implement free of filetable and trapframe
	kfree(thread->t_parent_process->p_filetable);
	kfree(thread->t_parent_process->p_trapframe); */
	sem_destroy(process->p_exitsem);
	kfree(process);
	return;
}

/**
 * Added by Babu on : 04/01/2014
 * Get pid of the calling process
 */
pid_t
sys_getpid(int32_t *retval)
{
	if(curthread != NULL)
	{
		if(curthread->t_process != NULL)
		{
			retval = &curthread->t_process->p_pid_self;
			return 0;
		}
	}
	return 1;
}

/**
 * Added by Babu on : 04/01/2014
 * Get pid of the calling process
 */
pid_t
sys_getppid(int32_t *retval)
{
	if(curthread != NULL)
	{
		if(curthread->t_process != NULL)
		{
			if(curthread->t_process->p_parent_process != NULL)
			{
				retval = &curthread->t_process->p_parent_process->p_pid_self;
				return 0;
			}
		}
	}
	return 1;
}

/**
 * Added by Babu : 04/02/2014
 * Waitpid will wait for the process to change status/destroyed and collect the return status
 * process which are not collected the return status remain as 'zombies'
 */
int
sys_waitpid(pid_t *pid, int32_t *exitcode)
{
	struct process *childprocess = curthread->t_process->p_child_process;
	int retval = -1;
	if(childprocess->p_exited)
	{
		/*collect the exitstatus and return*/
		exitcode = &childprocess->p_exitcode;
		pid = &childprocess->p_pid_self;
		process_destroy(childprocess);
		retval = 0;
	}
	else
	{
		/*else wait on sem until the thread exits then collect exit status and return*/
		P(curthread->t_process->p_exitsem);
		exitcode = &childprocess->p_exitcode;
		pid = &childprocess->p_pid_self;
		process_destroy(childprocess);
		retval = 0;
	}
	return retval;
}

/**
 * Added by Babu : 04/02/2014
 * sysexit will stop the current thread execution and destroy it immediately
 */
int
sys__exit(int exitstatus)
{
	/*TODO: close file handle? */
	int retval = -1;
	if(!curthread->t_process->p_exited && !curthread->t_process->p_parent_process->p_exited)
	{
		curthread->t_process->p_exitcode = exitstatus;
		/* Free thread structure and destroy all the thread related book keeping stuffs*/
		thread_exit();
		//thread_destroy();
		V(curthread->t_process->p_exitsem);
		retval = 0;
	}
	return retval;
}

/**
 * Added by Babu on 03/09/2014:
 *
 * fork() creates a new process by duplicating the calling process. The new process,
 * referred to as the child, is an exact duplicate of the calling process, referred
 * to as the parent.
 *
 * return value(retval) 0 for child process and child pid for parent process
 *
 */
int
sys_fork(int32_t *retval, struct trapframe *tf)
{
	struct addrspace *child_addrspce = NULL;
	struct process *child = NULL;
	int spl = 0;
	struct process *parent = NULL;
	int retvalfork = 1;
	/**
	 * Create
	 */

	parent = curthread->t_process;
	if(parent == NULL)
		panic("parent process retrieve operation failed");

	parent->p_thread = curthread;
	parent->p_trapframe = tf;

	/*
	 * Child process creation
	 */
	child  = kmalloc(sizeof(struct process));
	if(child == NULL)
		panic("Child process creation failed");

	child->p_parent_process->p_pid_self = parent->p_pid_self;
	child->p_pid_self = allocate_pid();
	child->p_trapframe = kmalloc(sizeof(struct trapframe));

	/* create a copy of trapframe using memcpy */
	memcpy(child->p_trapframe, tf, sizeof(tf));

	/* Addres space and file table cloning from parent */
	as_copy(parent->p_thread->t_addrspace, &child_addrspce);

	/* TODO :file table?*/
	child->p_filetable =  parent->p_filetable;

	/* disable interrupts*/
	spl  = splhigh();

	/**
	 * copy parent trapframe to the child process
	 * and invoke creating child thread
	 */
	tf->tf_a0 = (uint32_t) child_addrspce;
	retvalfork = thread_fork("child process", child_entrypoint, tf,(unsigned long) child_addrspce, &child->p_thread);
	kprintf("return value of thread_fork : %d\n",retvalfork);

	/*Assigning the process structure to the thread just got created*/
	child->p_thread->t_process = child;

	/* Return values as child process pid */
	retval = &child->p_pid_self;

	/* enable interrupts again*/
	splx(spl);

	/* Return success or error code  */
	return retvalfork;

}

/**
 * Child entry point function which will be invoked
 * immediately after the child process creation
 */
void
child_entrypoint(void *data1, unsigned long data2)
{
	struct trapframe tf_copy;
	struct trapframe *child_tf = (struct trapframe *) data1;
	struct addrspace *child_addrspce = (struct addrspace *) data2;

	if(child_addrspce == NULL)
		child_addrspce = (struct addrspace *) child_tf->tf_a0;


	if(child_addrspce == NULL || child_tf == NULL) /* To indicate failure of child fork */
	{
		child_tf->tf_v0 = EFAULT;
		child_tf->tf_a3 = 1;
		panic("child entry point failed");
	}
	else /* To indicate success of child fork */
	{
		child_tf->tf_v0 = 0;
		child_tf->tf_a3 = 0;
	}

	/*
	* Now, advance the program counter, to avoid restarting
	* the syscall over and over again.
	*/
	child_tf->tf_epc += 4;

	/** Loading child's address space into current thread address space */
	curthread->t_addrspace = child_addrspce;
	as_activate(curthread->t_addrspace);

	/* Copy modified trap frame*/
	memcpy(&tf_copy, child_tf, sizeof(struct trapframe));

	/* And enter user mode*/
	mips_usermode(&tf_copy);

}

