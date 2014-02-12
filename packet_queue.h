/*
 * packet_queue.h
 *
 *  Created on: 2013-10-20
 *      Author: hemiao
 */


#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

extern "C"
{
#include <libavformat/avformat.h>
#include <SDL/SDL.h>
}

typedef struct packet_queue
{
	  AVPacketList *first_pkt, *last_pkt;
	  int nb_packets;
	  int size;
	  SDL_mutex *mutex;
	  SDL_cond *cond;
	  SDL_cond * w_cond;
}packet_queue_t;

int packet_queue_init(packet_queue_t * queue);
int packet_queue_get(packet_queue_t * queue, AVPacket * packet, int block);
int packet_queue_put(packet_queue_t * queue, AVPacket * packet);
void packet_queue_flush(packet_queue_t * queue);

#endif
