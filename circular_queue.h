/*
 * circularqueue.h
 *
 *  Created on: 2013-11-24
 *      Author: hemiao
 */

#ifndef CIRCULARQUEUE_H_
#define CIRCULARQUEUE_H_

#include <pthread.h>

class circular_queue
{
public:
	circular_queue(int number);
	virtual ~circular_queue();

	int push(void * elem);
	void * pop();

private:
	void * elem;
	unsigned int size;
	unsigned int in;
	unsigned int out;
	unsigned int max_size;

	pthread_mutex_t * mutex;
	pthread_cond_t  * in_cond;
	pthread_cond_t  * out_cond;
};

typedef class circular_queue circular_queue_t;

#endif /* CIRCULARQUEUE_H_ */
