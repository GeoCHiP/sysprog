#include "thread_pool.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum task_state {
	IS_NEW = 0,
	IS_PUSHED,
	IS_RUNNING,
	IS_FINISHED
};

struct thread_task {
	thread_task_f function;
	void *arg;

	/* PUT HERE OTHER MEMBERS */
	enum task_state state;
	void *result;

	struct thread_task *next;
};

struct thread_pool {
	pthread_t *threads;

	/* PUT HERE OTHER MEMBERS */
	int threads_capacity;
	struct thread_task *tasks_queue_head;
	struct thread_task *tasks_queue_tail;
	int queue_size;
};

#define handle_error_en(en, msg) \
	   do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count < 1 || max_thread_count > TPOOL_MAX_THREADS) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	*pool = malloc(sizeof(**pool));
	memset(*pool, 0, sizeof(**pool));
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->threads_capacity;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	// TODO: check for active tasks
	free(pool);
	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->queue_size == TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	task->state = IS_RUNNING;
	int err = pthread_create(&pool->threads[0], NULL, task->function, task->arg);
	if (err != 0) {
		handle_error_en(err, "pthread_create");
	}

	/*if (pool->queue_size == 0) {*/
		/*pool->tasks_queue_head = task;*/
		/*pool->tasks_queue_tail = task;*/
	/*} else {*/
		/*pool->tasks_queue_tail->next = task;*/
	/*}*/
	/*task->state = IS_PUSHED;*/

	return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	*task = malloc(sizeof(**task));
	(*task)->function = function;
	(*task)->arg = arg;
	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->state == IS_FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->state == IS_RUNNING;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	if (task->state == IS_NEW) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	// TODO: maybe use condition variable for this;
	while (task->state != IS_FINISHED) {
		// TODO: pthread_cond_wait
	}

	*result = task->result;

	return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	if (task->state != IS_NEW) {
		return TPOOL_ERR_TASK_IN_POOL;
	}
	free(task);
	return 0;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
