/*
 * pool.h
 *
 *  Created on: 2013-11-25
 *      Author: hemiao
 */

#ifndef POOL_H_
#define POOL_H_
extern "C"
{
#include <pthread.h>
}

class pool
{
public:
	pool();
	pool(int size);
	virtual ~pool();
	void alloc_buffer(int size);
	void set_entry_internal(void * ptr);
	void release_entry(void * ptr);
	//pool_internal * get_entry();
	void * get_entry_internal();
	int get_capacity();   		//获得总容量
	int availables();  		//获得当前可用的条目数量
private:
	class pool_internal
	{
	private:
		void * elem;
		bool used;
	public:
		pool_internal(void * ptr);
		void set(void * ptr);
		void * get();
		void * occupy();
		void release();
		bool is_occupied();
	};

	pool_internal * mpool;
	int capacity;
	int size;

	pthread_mutex_t  mutex;
	pthread_cond_t  cond;
};

#endif /* POOL_H_ */
