/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __OS_THREADPOOL_H__
#define __OS_THREADPOOL_H__	1

#define _XOPEN_SOURCE 600
#include <pthread.h>
#include <stdatomic.h>
#include "os_list.h"

#define OS_TASK_FIRST_MEMBER argument

typedef struct {
	void *argument;
	void (*action)(void *arg);
	void (*destroy_arg)(void *arg);
	unsigned int id;
	os_list_node_t list;
} os_task_t;

typedef struct os_threadpool {
	unsigned int num_threads;
	pthread_t *threads;

	/* Synchronization data */
	_Atomic unsigned int num_tasks;
	_Atomic unsigned int exited_threads;
	_Atomic unsigned int enqueued_tasks;
	_Atomic unsigned int dequeued_tasks;
	_Atomic unsigned int leave;
	pthread_mutex_t list_mutex;

	_Atomic unsigned int waiting_threads;
	pthread_mutex_t waiting_mutex;

	pthread_cond_t list_signal;
	pthread_mutex_t list_signal_mutex;

	pthread_cond_t enqueue_signal;
	pthread_mutex_t enqueue_mutex;

	/*
	 * Head of queue used to store tasks.
	 * First item is head.next, if head.next != head (i.e. if queue
	 * is not empty).
	 * Last item is head.prev, if head.prev != head (i.e. if queue
	 * is not empty).
	 */
	os_list_node_t head;
} os_threadpool_t;

os_task_t *create_task(void (*f)(void *), void *arg, void (*destroy_arg)(void *), unsigned int id);
void destroy_task(os_task_t *t);

os_threadpool_t *create_threadpool(unsigned int num_threads);
void destroy_threadpool(os_threadpool_t *tp);

void enqueue_task(os_threadpool_t *q, os_task_t *t);
os_task_t *dequeue_task(os_threadpool_t *tp);
void wait_for_completion(os_threadpool_t *tp);

#endif
