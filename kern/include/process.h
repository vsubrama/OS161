/*
 * process.h
 *
 *  	Created on: Mar 8, 2014
 *      Author: Babu
 */

#ifndef PROCESS_H_
#define PROCESS_H_

/*
 * Process table which holds all information about the process
 * such as pid, parent process, threads belongs to that process
 *
 */
struct process {

	pid_t p_pid_self;  /* process id of the process */

	struct thread *p_thread;

	pid_t p_pid_parent;

	// Variables for process exit
	bool p_exited;

	int p_exitcode;

	struct semaphore *p_exitsem;
};


/**
 * Added by Babu:
 * Pid pool to maintain the free available pids
 */
struct pid_pool
{
	pid_t pid_avail; // Available pid
	struct pid_pool *next; // Point to next available pid
};

/** Process Table **/
struct process processtable[MAX_RUNNING_PROCS];

// Golbal PID counter
pid_t global_pid_count = 1;

/* Head and tail pointer for query and insert operation from pool which
 * follows FIFO approach - Added by Babu
 */
struct pid_pool *head = NULL;
struct pid_pool *tail = NULL;


/**
 * When a process exits it should invoke this method so that
 * this will add the allocated pid to free pool which can be assigned
 * for other process  - Added by Babu
 */
void add_pid_to_pool(pid_t);


/**
 * Allocate pid based on the pids available in the pool or
 * generate from the counter if the pool is empty  - Added by Babu
 */
pid_t allocate_pid(void);

/**
 * function to destroy process and it related book keeping stuffs
 */
void process_destroy(struct process *process);


/**
 * getpid system call which fetches the pid of the calling process
 */
pid_t sys_getpid(int32_t *retval);

/**
 * getppid system call which fetches the pid of the calling process parent
 */
pid_t sys_getppid(int32_t *retval);

/**
 * exit system call which allows the calling process to exit
 */
void sys__exit(int exitcode);

/**
 * wait system call allows the calling process' parent to collect the status of child process
 */
int sys_waitpid(pid_t *pid, int32_t *exitcode);

/**
 * fork system call which creates clone of the calling process
 */
int sys_fork(int32_t *retval, struct trapframe *tf);

/**
 * execv system call allows create process from the file and loads into address space and executes
 */
int sys_execv(char *prgname, char *argv[]);

/**
 * Entry point function for the child process/thread created by fork()
 */
void child_entrypoint(void *data1, unsigned long data2);


#endif /* PROCESS_H_ */
