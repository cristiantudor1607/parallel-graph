// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4
#define STARTING_NODE	0

static _Atomic int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
/* TODO: Define graph synchronization mechanisms. */

pthread_mutex_t visited_mutex;

/* TODO: Define graph task argument. */
static void destory_uint(void *heap_uint)
{
	free(heap_uint);
}

// This function will become process_node() if it works
static void task_function(void *heap_uint)
{
	unsigned int idx = *(unsigned int *)heap_uint;
	//log_debug("Processing %d by thread %lu\n", idx, pthread_self());

	atomic_fetch_add(&sum, graph->nodes[idx]->info);
	//log_debug("Sum at %d is %d by thread %lu\n", idx, sum, pthread_self());

	pthread_mutex_lock(&visited_mutex);
	graph->visited[idx] = DONE;
	pthread_mutex_unlock(&visited_mutex);
}

static void *get_uint(unsigned int integer)
{
	unsigned int *heap_uint = malloc(sizeof(unsigned int));
	DIE(!heap_uint, "malloc failed\n");

	*heap_uint = integer;

	return (void *)heap_uint;
}


static void graph_loop_function(unsigned int idx)
{
	pthread_mutex_lock(&visited_mutex);
	graph->visited[idx] = PROCESSING;
	pthread_mutex_unlock(&visited_mutex);

	for (unsigned int i = 0; i < graph->nodes[idx]->num_neighbours; i++) {
		unsigned int arg = graph->nodes[idx]->neighbours[i];

		pthread_mutex_lock(&visited_mutex);
		if (graph->visited[arg] != NOT_VISITED) {
			pthread_mutex_unlock(&visited_mutex);
			continue;
		}
		pthread_mutex_unlock(&visited_mutex);

		graph_loop_function(arg);
	}

	void *heap_idx = get_uint(idx);

	os_task_t *new_task = create_task(&task_function, heap_idx, &destory_uint, idx);

	enqueue_task(tp, new_task);

	atomic_fetch_add(&tp->enqueued_tasks, 1);
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);
	//print_graph(graph);

	/* TODO: Initialize graph synchronization mechanisms. */
	pthread_mutex_init(&visited_mutex, NULL);

	atomic_store(&sum, 0);

	tp = create_threadpool(NUM_THREADS, graph->num_nodes);
	//unsigned int start = STARTING_NODE;
	//setup_start_node(&start);
	//process_node(&start);

	graph_loop_function(STARTING_NODE);
	atomic_store(&tp->enqueue_is_done, 1);
	//log_trace("Enqueued tasks: %d\n", atomic_load(&tp->enqueued_tasks));
	wait_for_completion(tp);
	destroy_threadpool(tp);

	pthread_mutex_destroy(&visited_mutex);

	printf("%d", sum);

	return 0;
}
