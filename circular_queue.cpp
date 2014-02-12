/*
 * circularqueue.cpp
 *
 *  Created on: 2013-11-24
 *      Author: hemiao
 */

#include "circular_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

circular_queue::circular_queue(int number)
{
	// TODO Auto-generated constructor stub
	elem = malloc(sizeof(void * ) * number);
	memset(elem, 0, sizeof(void *) * number);
	max_size = number;

	pthread_mutex_init(mutex, NULL);
	pthread_cond_init(in_cond, NULL);
	pthread_cond_init(out_cond, NULL);
}

circular_queue::~circular_queue()
{
	// TODO Auto-generated destructor stub
	free(elem);
	elem = NULL;

	pthread_mutex_destroy(mutex);
	pthread_cond_destroy(in_cond);
	pthread_cond_destroy(out_cond);
}

int circular_queue::push(void * elem)
{
	pthread_mutex_lock(mutex);
	if (size >= max_size)
		pthread_cond_wait(in_cond, mutex);

#ifdef __i386__
		((int **) this->elem)[in++] = (int *)elem;
#endif

	size++;
	if (in >= max_size)
		in = 0;

	if (size > 0)
		pthread_cond_signal(out_cond);

	return 0;
}

void * circular_queue::pop()
{
	void * ptr = NULL;

	pthread_mutex_lock(mutex);
	if (size <= 0)
		pthread_cond_wait(out_cond, mutex);

#ifdef __i386__
	ptr = (void *)((int *) this->elem)[out++];
#endif

	size--;
	if (out >= max_size)
		out = 0;

	if (size <= max_size)
		pthread_cond_signal(in_cond);

	return ptr;
}
