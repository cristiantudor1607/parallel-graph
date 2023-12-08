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

static void *get_uint(unsigned int integer)
{
	unsigned int *heap_uint = malloc(sizeof(unsigned int));
	DIE(!heap_uint, "malloc failed\n");

	*heap_uint = integer;

	return (void *)heap_uint;
}

static void free_uint(void *heap_uint)
{
	free(heap_uint);
}

static void process_node(void *arg)
{
	unsigned int idx = *(unsigned int *)arg;
	//log_debug("Processing %d by thread %lu\n", idx, pthread_self());

	atomic_fetch_add(&sum, graph->nodes[idx]->info);
	//log_debug("Sum at %d is %d by thread %lu\n", idx, sum, pthread_self());

	for (unsigned int i = 0; i < graph->nodes[idx]->num_neighbours; i++) {
		unsigned int new_arg = graph->nodes[idx]->neighbours[i];
		
		pthread_mutex_lock(&visited_mutex);
		if (graph->visited[new_arg] != NOT_VISITED) {
			pthread_mutex_unlock(&visited_mutex);
			continue;
		}

		graph->visited[new_arg] = PROCESSING;
		pthread_mutex_unlock(&visited_mutex);

		void *heap_uint = get_uint(new_arg);

		os_task_t *new_task = create_task(&process_node, heap_uint, &free_uint, new_arg);

		enqueue_task(tp, new_task);
	}


	pthread_mutex_lock(&visited_mutex);
	graph->visited[idx] = DONE;
	pthread_mutex_unlock(&visited_mutex);
}

static void setup_start_node(void *arg)
{
	unsigned int start_node = *(unsigned int *)arg;

	pthread_mutex_lock(&visited_mutex);
	graph->visited[start_node] = PROCESSING;
	pthread_mutex_unlock(&visited_mutex);

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

	tp = create_threadpool(NUM_THREADS, graph->num_nodes);
	unsigned int start = STARTING_NODE;

	setup_start_node(&start);
	process_node(&start);
	wait_for_completion(tp);
	destroy_threadpool(tp);

	pthread_mutex_destroy(&visited_mutex);

	printf("%d", sum);

	return 0;
}
