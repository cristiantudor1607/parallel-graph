// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	pthread_mutex_lock(&tp->list_mutex);

	list_add_tail(tp->head.next, &t->list);
	atomic_store(&tp->enqueued_tasks, 1);

	pthread_cond_signal(&tp->list_signal);
	pthread_mutex_unlock(&tp->list_mutex);

	pthread_mutex_lock(&tp->enqueue_mutex);
	pthread_cond_broadcast(&tp->enqueue_signal);
	pthread_mutex_unlock(&tp->enqueue_mutex);
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t = NULL;

	// Wait for the first enqueue to happen
	// When it happens all the threads waiting will escape, and they'll block
	// at the next mutex
	pthread_mutex_lock(&tp->enqueue_mutex);
	while (!atomic_load(&tp->enqueued_tasks)) {
		// Increase the number of waiting threads
		pthread_mutex_lock(&tp->waiting_mutex);
		atomic_fetch_add(&tp->waiting_threads, 1);
		pthread_mutex_unlock(&tp->waiting_mutex);

		pthread_cond_wait(&tp->enqueue_signal, &tp->enqueue_mutex);
		atomic_fetch_sub(&tp->waiting_threads, 1);

		break;
	}
	pthread_mutex_unlock(&tp->enqueue_mutex);

	// Wait until something is enqueued
	pthread_mutex_lock(&tp->list_mutex);
	while (queue_is_empty(tp)) {
		atomic_fetch_add(&tp->waiting_threads, 1);

		// If all threads are waiting here, then it is done
		if (atomic_load(&tp->waiting_threads) == tp->num_threads) {
			pthread_cond_broadcast(&tp->list_signal);
			pthread_mutex_unlock(&tp->list_mutex);
			return NULL;
		}

		pthread_cond_wait(&tp->list_signal, &tp->list_mutex);

		// If the thread recieved a signal, then it could be a signal
		// to leave the program, sent from if block from above
		if (atomic_load(&tp->waiting_threads) == tp->num_threads) {
			pthread_mutex_unlock(&tp->list_mutex);
			return NULL;
		}

		atomic_fetch_sub(&tp->waiting_threads, 1);
	}
	t = list_entry(tp->head.next, os_task_t, list);
	list_del(tp->head.next);
	pthread_mutex_unlock(&tp->list_mutex);

	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* Synchronization data initialization */
	atomic_store(&tp->waiting_threads, 0);
	atomic_store(&tp->enqueued_tasks, 0);

	pthread_mutex_init(&tp->list_mutex, NULL);
	pthread_mutex_init(&tp->list_signal_mutex, NULL);
	pthread_cond_init(&tp->list_signal, NULL);

	pthread_mutex_init(&tp->enqueue_mutex, NULL);
	pthread_cond_init(&tp->enqueue_signal, NULL);

	pthread_mutex_init(&tp->waiting_mutex, NULL);

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	pthread_mutex_destroy(&tp->list_mutex);
	pthread_mutex_destroy(&tp->list_signal_mutex);
	pthread_cond_destroy(&tp->list_signal);

	pthread_mutex_destroy(&tp->enqueue_mutex);
	pthread_cond_destroy(&tp->enqueue_signal);

	pthread_mutex_destroy(&tp->waiting_mutex);

	os_list_node_t *n, *p;

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
