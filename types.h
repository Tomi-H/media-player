/*
 * types.h
 *
 *  Created on: 2013-11-25
 *      Author: hemiao
 */
#ifndef TYPES_H_
#define TYPES_H_

extern "C"
{

#include <SDL/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
//#include "circular_queue.h"
#include "packet_queue.h"
#include "pool.h"

#define log_err(str) do {fprintf(stderr, "%s:%d %s", __FILE__, __LINE__, str);} while(0)

#define MAX_PACKET_NUM 10
#define MAX_FRAME_NUM 10

#define ALLOC_PICTURE SDL_USEREVENT + 1
#define REFRESH_TIMER SDL_USEREVENT + 2
#define PICTURE_QUEUE_SIZE 1



typedef struct video_picture
{
	SDL_Overlay * bmp;
	double pts;
	int allocated;
}video_picture_t;

enum {
	MEDIA_CONTROL_PLAY = 1,
	MEDIA_CONTROL_PAUSE,
	MEDIA_CONTROL_STOP,
};

typedef struct media_control {
	SDL_mutex * mutex;
	SDL_cond * cond;
	int flags;
} media_control_t;

class video_internal_t
{
public:
	AVCodecContext * codec_ctx_ptr;
	AVCodec * codec_ptr;
	int stream_id;
	int valid;

	struct SwsContext * sws_ctx;
	enum AVPixelFormat src_fmt;
	int dst_fmt;
	int src_width, src_height;
	int dst_width, dst_height;

	packet_queue_t * pkt_queue;
//	pool * overlay_pool;
//	pool * pict_pool;

	video_picture_t picture_queue[PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex, pictq_windex;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;

	double clock;
};

class audio_internal_t
{
public:
	AVCodecContext * codec_ctx_ptr;
	AVCodec * codec_ptr;
	int stream_id;
	int valid;

	packet_queue_t * pkt_queue;

	struct SwrContext * swr_ctx;
	uint8_t * buffer;
//	unsigned int buffer_size;
//	unsigned int buffer_index;

	//以下四个变量是用于构造swr_ctx的目标参数
	enum AVSampleFormat out_sample_format;
	int out_channels;
	uint64_t out_channel_layout;
	int out_sample_rate;

	int buf_size;
	int buf_index;
	int pkt_size;
	int pkt_index;
	double clock;
};

class media_status_t
{
public:
	char * filename;

	AVFormatContext * fmt_ctx_ptr;
	video_internal_t  video;
	audio_internal_t  audio;

	pthread_t  parse_tid;
	pthread_t  video_tid;

	SDL_Surface * screen;
	int quit;


	double frame_timer;
	double frame_last_delay;
	uint64_t frame_last_pts;
	double audio_clock;

	int 	seek_req;
	int 	seek_flags;
	int64_t seek_pos;


	media_control_t control;
};

#endif  /* TYPES_H_ */
