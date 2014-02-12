/*
 * packet_queue.c
 *
 *  Created on: 2013-10-20
 *      Author: hemiao
 */


extern "C"
{
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
# include <stdint.h>
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
}


#include "packet_queue.h"



#define MAX_PACKETS 30

int packet_queue_init(packet_queue_t * queue)
{
	if (!queue)
		return -1;

	memset(queue, 0, sizeof(packet_queue_t));
	queue->mutex = SDL_CreateMutex();
	queue->cond = SDL_CreateCond();
	queue->w_cond = SDL_CreateCond();

	return 0;
}

int packet_queue_put(packet_queue_t * queue, AVPacket *pkt)
{
	AVPacketList * pkt_node = NULL;

	if (!queue)
		return -1;

	pkt_node = (AVPacketList *)av_malloc(sizeof(AVPacketList));
	if (!pkt_node)
		return -1;

	memset(pkt_node, 0, sizeof(AVPacketList));
	pkt_node->pkt = *pkt;
	pkt_node->next = NULL;

	SDL_LockMutex(queue->mutex);
	while (queue->nb_packets > MAX_PACKETS)
	{
		SDL_CondWait(queue->w_cond, queue->mutex);
	}

	if (!queue->last_pkt)
	{
		queue->first_pkt = pkt_node;
	}
	else
	{
		queue->last_pkt->next = pkt_node;
	}
	queue->last_pkt = pkt_node;
	queue->nb_packets++;
	queue->size += pkt_node->pkt.size;

	SDL_CondSignal(queue->cond);
	SDL_UnlockMutex(queue->mutex);

	return 0;
}

int packet_queue_get(packet_queue_t * queue, AVPacket * pkt, int block)
{
	AVPacketList * pkt_node = NULL;
	int ret = 0;

	if (!queue)
		return -1;

	SDL_LockMutex(queue->mutex);

	while (1)
	{
		if (queue->first_pkt)
		{
			pkt_node = queue->first_pkt;
			*pkt = pkt_node->pkt;

			queue->first_pkt = pkt_node->next;
			if (!queue->first_pkt)
			{
				queue->last_pkt = NULL;
			}
			queue->nb_packets--;
			queue->size -= pkt->size;
			av_free(pkt_node);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(queue->cond, queue->mutex);
		}
	}

	if (queue->nb_packets <= MAX_PACKETS)
		SDL_CondSignal(queue->w_cond);

	SDL_UnlockMutex(queue->mutex);

	return ret;
}

void packet_queue_flush(packet_queue_t * queue)
{
	AVPacketList * it, *tt;

	SDL_LockMutex(queue->mutex);
	for (it = queue->first_pkt; it != NULL; it = tt)
	{
		tt = it->next;
		av_free_packet(&it->pkt);
		av_freep(&it);
	}
	queue->first_pkt = NULL;
	queue->last_pkt = NULL;
	queue->size = 0;
	queue->nb_packets = 0;
	SDL_UnlockMutex(queue->mutex);
}
