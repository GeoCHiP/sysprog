#include "thread_pool.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
	void *result;
	enum task_state state;
	bool is_detached;

	pthread_cond_t cond;
	pthread_mutex_t mutex;

	struct thread_pool *pool;

	struct thread_task *prev;
	struct thread_task *next;
};

struct task_queue {
	struct thread_task *head;
	struct thread_task *tail;
	int size;
};

struct thread_pool {
	pthread_t *threads;

	/* PUT HERE OTHER MEMBERS */
	int max_thread_count;
	int threads_capacity;
	int threads_working;

	bool queue_task_available;
	bool is_deleting;

	struct task_queue waiting_queue;

	pthread_cond_t waiting_queue_cond;
	pthread_mutex_t waiting_queue_mutex;
};

void
queue_push(struct task_queue *q, struct thread_task *task) {
	if (q->size == 0) {
		q->head = task;
		q->tail = task;
	} else {
		task->next = NULL;
		task->prev = q->tail;
		q->tail->next = task;
		q->tail = task;
	}
	q->size++;
}

struct thread_task *
queue_pop(struct task_queue *q) {
	struct thread_task *task = q->head;
	pthread_mutex_lock(&task->mutex);
	q->head = task->next;
	if (!q->head) {
		q->tail = NULL;
	}
	q->size--;
	task->prev = NULL;
	task->next = NULL;
	pthread_mutex_unlock(&task->mutex);
	return task;
}

void
queue_remove(struct task_queue *q, struct thread_task *task) {
	if (task->prev) {
		task->prev->next = task->next;
	} else {
		q->head = task->next;
	}
	if (task->next) {
		task->next->prev = task->prev;
	} else {
		q->tail = task->prev;
	}
	q->size--;
}

#define handle_error_en(en, msg) \
	   do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count < 1 || max_thread_count > TPOOL_MAX_THREADS) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	*pool = calloc(sizeof(**pool), 1);
	(*pool)->max_thread_count = max_thread_count;
	pthread_mutex_init(&(*pool)->waiting_queue_mutex, NULL);
	pthread_cond_init(&(*pool)->waiting_queue_cond, NULL);
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
	pthread_mutex_t *queue_mutex = &pool->waiting_queue_mutex;
	pthread_cond_t *queue_cond = &pool->waiting_queue_cond;
	pthread_mutex_lock(queue_mutex);

	if (pool->waiting_queue.size != 0 || pool->threads_working != 0) {
		pthread_mutex_unlock(queue_mutex);
		return TPOOL_ERR_HAS_TASKS;
	}

	pool->is_deleting = true;
	pthread_cond_broadcast(queue_cond);
	pthread_mutex_unlock(queue_mutex);

	for (int i = 0; i < pool->threads_capacity; ++i) {
		pthread_join(pool->threads[i], NULL);
	}

	pthread_cond_destroy(&pool->waiting_queue_cond);
	pthread_mutex_destroy(&pool->waiting_queue_mutex);
	free(pool->threads);
	free(pool);
	return 0;
}

static void *
thread_pool_runner(void *arg)
{
	struct thread_pool *pool = arg;
	pthread_mutex_t *queue_mutex = &pool->waiting_queue_mutex;
	pthread_cond_t *queue_cond = &pool->waiting_queue_cond;

	while (true) {
		pthread_mutex_lock(queue_mutex);
		while (pool->queue_task_available == false) {
			if (pool->is_deleting) {
				pthread_mutex_unlock(queue_mutex);
				return NULL;
			}
			pthread_cond_wait(queue_cond, queue_mutex);
		}

		struct thread_task *task = queue_pop(&pool->waiting_queue);
		pool->threads_working++;

		// Check if no more tasks are available for execution
		struct thread_task *iter = pool->waiting_queue.head;
		while (iter && iter->state != IS_PUSHED) {
			iter = iter->next;
		}
		if (!iter) {
			pool->queue_task_available = false;
		}
		pthread_mutex_t *task_mutex = &task->mutex;
		pthread_mutex_lock(task_mutex);
		pthread_mutex_unlock(queue_mutex);

		task->state = IS_RUNNING;
		pthread_mutex_unlock(task_mutex);

		void *result = task->function(task->arg);
		pthread_mutex_lock(queue_mutex);
		pool->threads_working--;
		pthread_mutex_unlock(queue_mutex);

		pthread_mutex_lock(task_mutex);
		task->result = result;
		task->state = task->is_detached ? IS_JOINED : IS_FINISHED;
		if (task->is_detached) {
			pthread_mutex_unlock(task_mutex);
			thread_task_delete(task);
		} else {
			pthread_cond_signal(&task->cond);
			pthread_mutex_unlock(task_mutex);
		}
	}
	return NULL;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->waiting_queue.size == TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	pthread_mutex_t *queue_mutex = &pool->waiting_queue_mutex;
	pthread_cond_t *queue_cond = &pool->waiting_queue_cond;

	pthread_mutex_lock(queue_mutex);
	queue_push(&pool->waiting_queue, task);
	task->pool = pool;
	task->state = IS_PUSHED;
	pool->queue_task_available = true;

	if (pool->waiting_queue.size > pool->threads_capacity
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
	*task = calloc(sizeof(**task), 1);
	(*task)->function = function;
	(*task)->arg = arg;
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

	pthread_mutex_lock(task_mutex);
	while (task->state != IS_FINISHED) {
		pthread_cond_wait(task_cond, task_mutex);
	}
	*result = task->result;

	task->next = NULL;
	task->prev = NULL;
	task->state = IS_JOINED;

	pthread_mutex_unlock(task_mutex);

	return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	if (task->state == IS_NEW) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	if (timeout < 0) {
		return TPOOL_ERR_TIMEOUT;
	}

	pthread_mutex_t *task_mutex = &task->mutex;
	pthread_cond_t *task_cond = &task->cond;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	ts.tv_sec += (time_t)timeout;
	timeout -= (time_t)timeout;

	uint64_t nsec = ts.tv_nsec + timeout * 1e9;
	if (nsec >= 1e9) {
		ts.tv_sec += 1;
		nsec -= 1e9;
	}
	ts.tv_nsec = nsec;

	int err = pthread_mutex_timedlock(task_mutex, &ts);
	if (err == ETIMEDOUT) {
		return TPOOL_ERR_TIMEOUT;
	}
	while (task->state != IS_FINISHED) {
		int err = pthread_cond_timedwait(task_cond, task_mutex, &ts);
		if (err == ETIMEDOUT) {
			pthread_mutex_unlock(task_mutex);
			return TPOOL_ERR_TIMEOUT;
		}
	}
	*result = task->result;

	task->next = NULL;
	task->prev = NULL;
	task->state = IS_JOINED;

	pthread_mutex_unlock(task_mutex);

	return 0;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
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
	if (task->state == IS_NEW) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	pthread_mutex_t *task_mutex = &task->mutex;
	pthread_mutex_lock(task_mutex);

	if (task->state == IS_FINISHED) {
		task->state = IS_JOINED;
		pthread_mutex_unlock(task_mutex);
		thread_task_delete(task);
		return 0;
	}

	task->is_detached = true;
	pthread_mutex_unlock(task_mutex);

	return 0;
}

#endif
