/*
 * pool.cpp
 *
 *  Created on: 2013-11-25
 *      Author: hemiao
 */

#include "pool.h"
#include <pthread.h>
#include <iostream>

pool::pool_internal::pool_internal(void * ptr = NULL)
{
	elem = ptr;
	used = false;
}

void pool::pool_internal::set(void * ptr)
{
	elem = ptr;
}

void * pool::pool_internal::occupy()
{
	used = true;
	return elem;
}

void pool::pool_internal::release()
{
	used = false;
}

bool pool::pool_internal::is_occupied()
{
	return used;
}

void * pool::pool_internal::get()
{
	return elem;
}

pool::pool()
{
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
}

pool::pool(int size)
{
	mpool = new pool_internal[size];
	capacity = size;
	this->size = 0;
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
}

pool::~pool()
{
	delete[] mpool;
}

//如果调用的是默认的构造函数，需要调用此函数来开辟空间
void pool::alloc_buffer(int size)
{
	mpool = new pool_internal[size];
	capacity = size;
	this->size = 0;
}

//pool_internal * pool::get_entry()
//{
//	pool_internal * entry = mpool;
//
//	pthread_mutex_lock(mutex);
//	while (size <= 0)
//		pthread_cond_wait(cond, mutex);
//
//	for (int i = 0; i < get_capacity(); i++)
//	{
//		entry += i;
//		if (!entry->is_occupied())
//		{
//			size--;
//			break;
//		}
//	}
//	pthread_mutex_unlock(mutex);
//
//	return NULL;
//}

void * pool::get_entry_internal()
{
	pool_internal * entry = mpool;
	void * ptr = NULL;

	pthread_mutex_lock(&mutex);
	while (size <= 0)
		pthread_cond_wait(&cond, &mutex);

	for (int i = 0; i < get_capacity(); i++)
	{
		entry += i;
		if (!entry->is_occupied())
		{
			ptr = entry->occupy();
			size--;
			break;
		}
	}
	pthread_mutex_unlock(&mutex);

	return ptr;
}

void pool::set_entry_internal(void * ptr)
{
	pool_internal * entry = mpool;

	pthread_mutex_lock(&mutex);
	for (int i = 0; i < get_capacity(); i++)
	{
		entry += i;
		if (!entry->is_occupied())
		{
			entry->set(ptr);
			size++;
			break;
		}
	}

	if (size > 0)
		pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

void pool::release_entry(void * ptr)
{
	pool_internal * entry = mpool;

	pthread_mutex_lock(&mutex);
	for (int i = 0; i < get_capacity(); i++)
	{
		entry += i;
		if (entry->get() == ptr)
		{
			entry->release();
			size++;
			break;
		}
	}
	pthread_mutex_unlock(&mutex);
}

int pool::get_capacity()
{
	return capacity;
}

int pool::availables()
{
	return size;
}
