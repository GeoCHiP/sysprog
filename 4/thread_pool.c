#include "thread_pool.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum task_state {
	IS_NEW = 0,
	IS_PUSHED,
	IS_RUNNING,
	IS_FINISHED,
	IS_JOINED,
};

struct thread_task {
	thread_task_f function;
	void *arg;

	/* PUT HERE OTHER MEMBERS */
	_Atomic enum task_state state;
	void *result;
	pthread_cond_t cond;
	pthread_mutex_t mutex;

	struct thread_pool *pool;

	struct thread_task *prev;
	struct thread_task *next;
};

struct thread_pool {
	pthread_t *threads;

	/* PUT HERE OTHER MEMBERS */
	int max_thread_count;
	int threads_capacity;
	bool queue_task_available;
	bool is_deleting;

	struct thread_task *queue_head;
	struct thread_task *queue_tail;
	int queue_size;
	struct thread_task *running_queue_head;
	struct thread_task *running_queue_tail;
	int running_queue_size;
	pthread_cond_t queue_cond;
	pthread_mutex_t queue_mutex;
	pthread_cond_t running_queue_cond;
	pthread_mutex_t running_queue_mutex;
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
	// TODO: use calloc
	memset(*pool, 0, sizeof(**pool));
	(*pool)->max_thread_count = max_thread_count;
	pthread_mutex_init(&(*pool)->queue_mutex, NULL);
	pthread_cond_init(&(*pool)->queue_cond, NULL);
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
	if (pool->running_queue_size != 0) {
		return TPOOL_ERR_HAS_TASKS;
	}
	pthread_mutex_t *queue_mutex = &pool->queue_mutex;
	pthread_cond_t *queue_cond = &pool->queue_cond;

	pthread_mutex_lock(queue_mutex);
	pool->is_deleting = true;
	pthread_cond_broadcast(queue_cond);
	pthread_mutex_unlock(queue_mutex);

	for (int i = 0; i < pool->threads_capacity; ++i) {
		pthread_join(pool->threads[i], NULL);
	}

	pthread_cond_destroy(&pool->queue_cond);
	pthread_mutex_destroy(&pool->queue_mutex);
	free(pool);
	return 0;
}

static void *
thread_pool_runner(void *arg)
{
	// TODO: get task from the queue and run it
	struct thread_pool *pool = arg;
	pthread_mutex_t *queue_mutex = &pool->queue_mutex;
	pthread_cond_t *queue_cond = &pool->queue_cond;
	pthread_mutex_t *running_queue_mutex = &pool->running_queue_mutex;
	/*pthread_cond_t *running_queue_cond = &pool->running_queue_cond;*/

	while (true) {
		pthread_mutex_lock(queue_mutex);
		while (pool->queue_task_available == false) {
			if (pool->is_deleting) {
				pthread_mutex_unlock(queue_mutex);
				return NULL;
			}
			pthread_cond_wait(queue_cond, queue_mutex);
		}
		struct thread_task *task = pool->queue_head;
		pool->queue_head = task->next;
		if (!pool->queue_head) {
			pool->queue_tail = NULL;
		}
		pool->queue_size--;

		pthread_mutex_t *task_mutex = &task->mutex;
		pthread_cond_t *task_cond = &task->cond;

		pthread_mutex_lock(task_mutex);
		/*while (task->state != IS_PUSHED) {*/
			/*task = task->next;*/
		/*}*/
		task->next = NULL;
		task->prev = NULL;
		task->state = IS_RUNNING;

		// Check if no more tasks are available for execution
		struct thread_task *iter = pool->queue_head;
		while (iter && iter->state != IS_PUSHED) {
			iter = iter->next;
		}
		if (!iter) {
			pool->queue_task_available = false;
		}

		pthread_mutex_unlock(task_mutex);
		pthread_mutex_unlock(queue_mutex);

		// TODO: extract into a function
		pthread_mutex_lock(running_queue_mutex);
		if (pool->running_queue_size == 0) {
			pool->running_queue_head = task;
			pool->running_queue_tail = task;
		} else {
			task->prev = pool->running_queue_tail;
			pool->running_queue_tail->next = task;
			pool->running_queue_tail = task;
		}
		pool->running_queue_size++;
		pthread_mutex_unlock(running_queue_mutex);

		void *result = task->function(task->arg);

		pthread_mutex_lock(task_mutex);
		task->result = result;
		task->state = IS_FINISHED;
		pthread_cond_signal(task_cond);
		pthread_mutex_unlock(task_mutex);
	}
	// TODO: should be unreachable? wait for join?
	return NULL;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->queue_size == TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	pthread_mutex_t *queue_mutex = &pool->queue_mutex;
	pthread_cond_t *queue_cond = &pool->queue_cond;

	pthread_mutex_lock(queue_mutex);
	// TODO: maybe remove queue_size and just check queue_head == NULL?
	if (pool->queue_size == 0) {
		pool->queue_head = task;
		pool->queue_tail = task;
	} else {
		task->prev = pool->queue_tail;
		pool->queue_tail->next = task;
		pool->queue_tail = task;
	}
	pool->queue_size++;
	task->pool = pool;
	task->state = IS_PUSHED;
	pool->queue_task_available = true;

	if (pool->queue_size > pool->threads_capacity
			&& pool->threads_capacity < pool->max_thread_count) {
		pool->threads = realloc(pool->threads, ++pool->threads_capacity * sizeof(*pool->threads));
		int err = pthread_create(&pool->threads[pool->threads_capacity - 1], NULL, thread_pool_runner, pool);
		if (err != 0) {
			handle_error_en(err, "pthread_create");
		}
	}
	pthread_cond_signal(queue_cond);
	pthread_mutex_unlock(queue_mutex);

	return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	*task = malloc(sizeof(**task));
	(*task)->function = function;
	(*task)->arg = arg;
	(*task)->result = NULL;
	(*task)->state = IS_NEW;
	(*task)->pool = NULL;
	(*task)->next = NULL;
	(*task)->prev = NULL;
	pthread_cond_init(&(*task)->cond, NULL);
	pthread_mutex_init(&(*task)->mutex, NULL);
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
	pthread_mutex_t *task_mutex = &task->mutex;
	pthread_cond_t *task_cond = &task->cond;
	pthread_mutex_t *running_queue_mutex = &task->pool->running_queue_mutex;

	pthread_mutex_lock(task_mutex);
	while (task->state != IS_FINISHED) {
		pthread_cond_wait(task_cond, task_mutex);
	}
	*result = task->result;

	/*pthread_mutex_t *queue_mutex = &task->pool->queue_mutex;*/
	/*pthread_mutex_lock(queue_mutex);*/
	/*if (task->prev) {*/
		/*task->prev->next = task->next;*/
	/*} else {*/
		/*task->pool->queue_head = task->next;*/
	/*}*/
	/*if (task->next) {*/
		/*task->next->prev = task->prev;*/
	/*} else {*/
		/*task->pool->queue_tail = task->prev;*/
	/*}*/

	pthread_mutex_lock(running_queue_mutex);
	if (task->prev) {
		task->prev->next = task->next;
	} else {
		task->pool->running_queue_head = task->next;
	}
	if (task->next) {
		task->next->prev = task->prev;
	} else {
		task->pool->running_queue_tail = task->prev;
	}
	task->pool->running_queue_size--;
	pthread_mutex_unlock(running_queue_mutex);

	task->next = NULL;
	task->prev = NULL;
	task->state = IS_JOINED;

	/*task->pool->queue_size--;*/
	/*pthread_mutex_unlock(queue_mutex);*/
	pthread_mutex_unlock(task_mutex);

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
	// TODO: check for other states, not only IS_NEW
	if (task->state != IS_NEW && task->state != IS_JOINED) {
		return TPOOL_ERR_TASK_IN_POOL;
	}
	pthread_mutex_destroy(&task->mutex);
	pthread_cond_destroy(&task->cond);
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
