/*
 * main.cpp
 *
 *  Created on: 2013-11-24
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
	#include <libswresample/swresample.h>
	#include <libswscale/swscale.h>
	#include <libavutil/avutil.h>
	#include <libavutil/time.h>
	#include <SDL/SDL.h>
	#include <SDL/SDL_thread.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <unistd.h>
	#include <X11/Xlib.h>
}

#include "packet_queue.h"
#include "types.h"
int64_t global_video_pkt_pts;
AVPacket flush_pkt;

#define SDL_AUDIO_BUFFER_SIZE 1024
#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0


int our_get_buffer(struct AVCodecContext *c, AVFrame *pic)
{
	int ret = avcodec_default_get_buffer(c, pic);
	uint64_t *pts = (uint64_t *)av_malloc(sizeof(uint64_t));
	*pts = global_video_pkt_pts;
	pic->opaque = pts;
	return ret;
}

void our_release_buffer(struct AVCodecContext *c, AVFrame *pic)
{
	if (pic)
		av_freep(&pic->opaque);
	avcodec_default_release_buffer(c, pic);
}


void stream_seek(media_status_t *is, int64_t pos, int rel) {
  if(!is->seek_req) {
    is->seek_pos = pos;
    is->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    is->seek_req = 1;
  }
}

double get_audio_clock(media_status_t * ms)
{
	double pts;
	int remain_bytes, bytes_per_sec;

	pts = ms->audio.clock;
	remain_bytes = ms->audio.buf_size - ms->audio.buf_index;
	bytes_per_sec = ms->audio.codec_ctx_ptr->channels * ms->audio.codec_ctx_ptr->sample_rate * 2;
	pts -= remain_bytes / bytes_per_sec;

	return pts;
}

int audio_decode_frame(media_status_t * ms)
{
	int ret, len;
	int decoded_bytes = 0;
	static int packet_data_size = 0;
	int got_frame = 0;
	AVPacket pkt, *packet = &pkt;
	AVFrame * frame = NULL;
	static uint8_t audio_buf[4096 * 4] __attribute__((aligned(16)));
	int out_count = 0;
	uint8_t * out[] = {audio_buf};
	int nb_channels = 0;

	if (!frame)
		frame = av_frame_alloc();

	avcodec_get_frame_defaults(frame);
	while (1)
	{
		if (packet_data_size)
		{
			ret = avcodec_decode_audio4(ms->audio.codec_ctx_ptr, frame, &got_frame, packet);
			if (ret < 0)
				continue;

			if (!got_frame) {
				packet_data_size = 0;
				continue;
			}

			packet_data_size -= ret;
			nb_channels = frame->channels > 0 ? frame->channels
					: av_get_channel_layout_nb_channels(frame->channel_layout);
			decoded_bytes = av_samples_get_buffer_size(NULL, nb_channels,
						frame->nb_samples, (AVSampleFormat)frame->format, 1);

			if (decoded_bytes <= 0)
							continue;

			if (frame->channels != ms->audio.out_channels ||
				frame->sample_rate != ms->audio.out_sample_rate ||
				frame->format != ms->audio.out_sample_format ||
				frame->channel_layout != ms->audio.out_channel_layout)
			{
				if (!ms->audio.swr_ctx)
				{
					ms->audio.swr_ctx = swr_alloc_set_opts(NULL, ms->audio.out_channel_layout,
											ms->audio.out_sample_format, ms->audio.out_sample_rate,
											frame->channel_layout, (enum AVSampleFormat)frame->format,
											frame->sample_rate, 0, NULL);
				}
				if (ms->audio.swr_ctx)
					swr_init(ms->audio.swr_ctx);

				out_count = sizeof(audio_buf) / ms->audio.out_channels /
						av_get_bytes_per_sample(ms->audio.out_sample_format);
				len = swr_convert(ms->audio.swr_ctx, out, out_count,
						(const uint8_t **)frame->extended_data, frame->nb_samples);
				if (len < 0)
				{
					fprintf(stderr, "error:  %s\n", av_err2str(errno));
				}
				decoded_bytes = len * ms->audio.out_channels *
									av_get_bytes_per_sample(ms->audio.out_sample_format);
				ms->audio.buffer = audio_buf;
				if (len == out_count)
				{
					log_err("audio buffer too small.\n");
				}
			} else {
				ms->audio.buffer = frame->data[0];
			}

			int n = av_get_bytes_per_sample((enum AVSampleFormat)frame->format) * nb_channels;
			ms->audio.clock += decoded_bytes / (n * ms->audio.codec_ctx_ptr->sample_rate);

			return decoded_bytes;
		}

		if (ms->quit)
			return 0;
		if (packet_queue_get(ms->audio.pkt_queue, packet, 1) < 0)
			continue;

		if (packet->data == flush_pkt.data)
		{
			avcodec_flush_buffers(ms->audio.codec_ctx_ptr);
			continue;
		}

		if (packet)
			packet_data_size = packet->size;

		if (packet->pts != AV_NOPTS_VALUE)
		{
//			ms->audio.clock = av_q2d(ms->audio.codec_ctx_ptr->time_base) * packet->pts;
			ms->audio.clock = av_q2d(ms->fmt_ctx_ptr->streams[ms->audio.stream_id]->time_base) * packet->pts;

		}
	}

	return ret;
}

void audio_callback(void * userdata, uint8_t * stream, int len)
{
	media_status_t * ms = (media_status_t *) userdata;
	int size, remain, ret;

	while (len > 0)
	{
		if (ms->quit)
			break;

		if (ms->audio.buf_index >= ms->audio.buf_size)
		{
			ret = audio_decode_frame(ms);
			if (ret < 0)
				continue;

			if (0 == ret)
				ms->audio.buf_size = 1024;
			else
				ms->audio.buf_size = ret;

			ms->audio.buf_index = 0;
		}

		remain = ms->audio.buf_size - ms->audio.buf_index;
		size = len;
		if (len > remain)
			size = remain;

		memcpy(stream, ms->audio.buffer + ms->audio.buf_index, size);
		len -= size;
		ms->audio.buf_index += size;
	}
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
	SDL_Event event;
	event.type = REFRESH_TIMER;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);
	return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(media_status_t *ms, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, ms);
}

void * parse_thread(void * userdata)
{
	media_status_t * ms = (media_status_t *)userdata;
	AVPacket pkt, * packet = &pkt;

	int lock = 0;

	while (1)
	{
		if (ms->quit)
			break;

		if (ms->seek_req)
		{
			int stream_index = -1;
			int64_t seek_target = ms->seek_pos;

			if (ms->video.stream_id >= 0)
			{
				stream_index = ms->video.stream_id;
			}
			else if (ms->video.stream_id >= 0)
			{
				stream_index = ms->audio.stream_id;
			}

			if (stream_index >= 0)
			{
				seek_target = av_rescale_q(ms->seek_pos, AV_TIME_BASE_Q,
						ms->fmt_ctx_ptr->streams[stream_index]->time_base);
			}

			if (av_seek_frame(ms->fmt_ctx_ptr, stream_index, seek_target, ms->seek_flags) < 0)
			{
				fprintf(stderr, "%s: error while seeking", ms->filename);
			}
			else
			{
				if (ms->audio.codec_ctx_ptr)
				{
					packet_queue_flush(ms->audio.pkt_queue);
					packet_queue_put(ms->audio.pkt_queue, &flush_pkt);
				}
				if (ms->audio.codec_ctx_ptr)
				{
					packet_queue_flush(ms->video.pkt_queue);
					packet_queue_put(ms->video.pkt_queue, &flush_pkt);
				}
			}

			printf("%f\n", packet->pts * av_q2d(ms->fmt_ctx_ptr->streams[stream_index]->time_base));
			ms->seek_req = 0;
			lock = 1;
			continue;
		}

		int ttemp;
		if ((ttemp = av_read_frame(ms->fmt_ctx_ptr, packet)) < 0) {
			fprintf(stderr, "error: %s.\n", av_err2str(ttemp));
			break;
		}

		if (lock) {
		printf("after seek: %f\n", packet->pts * av_q2d(ms->fmt_ctx_ptr->streams[ms->video.stream_id]->time_base));
		lock = 0;
		}

		if (packet->stream_index == ms->audio.stream_id)
		{
			if (ms->audio.pkt_queue->nb_packets > 20)
				usleep(80000);
			packet_queue_put(ms->audio.pkt_queue, packet);
			continue;
		}

		if (packet->stream_index == ms->video.stream_id)
		{
			if (ms->video.pkt_queue->nb_packets > 20)
						usleep(80000);
			packet_queue_put(ms->video.pkt_queue, packet);
			continue;
		}

	}
	return NULL;
}

double synchronize_video(media_status_t *is, AVFrame *src_frame, double pts)
{

  double frame_delay;

  if(pts != 0) {
    /* if we have pts, set video clock to it */
    is->video.clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = is->video.clock;
  }
  /* update the video clock */
//  frame_delay = av_q2d(is->video_st->codec->time_base);
  frame_delay = av_q2d(is->fmt_ctx_ptr->streams[is->video.stream_id]->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video.clock += frame_delay;
  return pts;
}

void * video_thread(void * userdata)
{
	AVFrame * frame = NULL;
	AVPacket pkt, *packet = &pkt;
	media_status_t * ms = (media_status_t *)userdata;
	static int packet_data_size = 0;
	int ret;
	int got_picture = 0;
	SDL_Overlay * bmp = NULL;
	video_picture_t * vp = NULL;

	frame = av_frame_alloc();
	while (1)
	{
		if (ms->quit)
			break;

		if (packet_data_size)
		{
			ret = avcodec_decode_video2(ms->video.codec_ctx_ptr, frame, &got_picture, packet);
			if (ret <= 0)
				break;

			if (!got_picture)
			{
				packet_data_size = 0;
				continue;
			}


			packet_data_size -= ret;
			vp = &ms->video.picture_queue[ms->video.pictq_windex];
			if (!vp->bmp)
			{
				SDL_Event event;

				event.type = ALLOC_PICTURE;
				SDL_PushEvent(&event);

				SDL_LockMutex(ms->video.pictq_mutex);
				while (!vp->allocated && !ms->quit)
					SDL_CondWait(ms->video.pictq_cond, ms->video.pictq_mutex);
				SDL_UnlockMutex(ms->video.pictq_mutex);

				if (ms->quit)
					break;
			}

			SDL_LockMutex(ms->video.pictq_mutex);
			while (ms->video.pictq_size > 0)
				SDL_CondWait(ms->video.pictq_cond, ms->video.pictq_mutex);
			SDL_UnlockMutex(ms->video.pictq_mutex);

			bmp = vp->bmp;
			if (packet->pts == AV_NOPTS_VALUE && frame->opaque &&
					*(int64_t *)frame->opaque != AV_NOPTS_VALUE)
			{
				vp->pts = * (uint64_t *) frame->opaque;
			} else if (packet->pts != AV_NOPTS_VALUE) {
				vp->pts = packet->pts;
			} else {
				vp->pts = 0;
			}

			vp->pts *= av_q2d(ms->fmt_ctx_ptr->streams[ms->video.stream_id]->time_base);
			vp->pts = synchronize_video(ms, frame, vp->pts);

			SDL_LockYUVOverlay(bmp);
			if (1)
			{
				ms->video.sws_ctx = sws_getCachedContext(ms->video.sws_ctx,frame->width, frame->height,
									(enum AVPixelFormat) frame->format, ms->video.dst_width,
									ms->video.dst_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
				AVPicture pict;

				pict.data[0] = bmp->pixels[0];
				pict.data[1] = bmp->pixels[2];
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];
				pict.linesize[1] = bmp->pitches[2];
				pict.linesize[2] = bmp->pitches[1];

				sws_scale(ms->video.sws_ctx, frame->data, frame->linesize, 0, frame->height,
						pict.data, pict.linesize);
			} else {
				AVPicture pict;

				pict.data[0] = bmp->pixels[0];
				pict.data[1] = bmp->pixels[2];
				pict.data[2] = bmp->pixels[1];

				pict.linesize[0] = bmp->pitches[0];
				pict.linesize[1] = bmp->pitches[2];
				pict.linesize[2] = bmp->pitches[1];

				bmp->pitches[0] = frame->linesize[0];
				bmp->pitches[2] = frame->linesize[1];
				bmp->pitches[1] = frame->linesize[2];
				avpicture_fill(&pict, frame->data[0], AV_PIX_FMT_YUV420P, frame->width, frame->height);
			}
			SDL_UnlockYUVOverlay(bmp);

			SDL_LockMutex(ms->video.pictq_mutex);
			if (++ms->video.pictq_windex >= PICTURE_QUEUE_SIZE)
				ms->video.pictq_windex = 0;
			ms->video.pictq_size++;
			SDL_CondSignal(ms->video.pictq_cond);
			SDL_UnlockMutex(ms->video.pictq_mutex);
		}

		if (packet_queue_get(ms->video.pkt_queue, packet, 1) < 0)
			continue;

		if (packet->data == flush_pkt.data)
		{
			avcodec_flush_buffers(ms->video.codec_ctx_ptr);
			continue;
		}

		packet_data_size = packet->size;
		global_video_pkt_pts = packet->pts;
	}
	av_frame_free(&frame);

	return NULL;
}



void display_video(media_status_t * ms, SDL_Overlay * bmp)
{
	//SDL_Overlay * bmp = bmp;
	float aspect_ratio;
	SDL_Rect rect;
	int w, h;


	//bmp = (SDL_Overlay *)ms->video.pict_pool->get_entry_internal();
	if (bmp)
	{
		if (ms->video.codec_ctx_ptr->sample_aspect_ratio.num == 0)
		{
			aspect_ratio = 0;
			w = ms->screen->w;
			h = ms->screen->h;
		} else {
			aspect_ratio = av_q2d(ms->video.codec_ctx_ptr->sample_aspect_ratio) *
						ms->video.codec_ctx_ptr->width / ms->video.codec_ctx_ptr->height;
//		}
			if (aspect_ratio < 0.0)
			{
				aspect_ratio = (float) ms->video.codec_ctx_ptr->width
						/ (float) ms->video.codec_ctx_ptr->height;
			}
			h = ms->screen->h;
			w = ((int) rint(h * aspect_ratio)) & -3;
			if (w > ms->screen->w)
			{
				w = ms->screen->w;
				h = ((int) rint(w * aspect_ratio)) & -3;
			}
		}
		int x = (ms->screen->w - w) / 2;
		int y = (ms->screen->h - h) / 2;

		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;

		SDL_DisplayYUVOverlay(bmp, &rect);
		//ms->video.pict_pool->release_entry(bmp);


	}
}


static int decode_interrupt_cb(void *ctx)
{
    media_status_t *is = (media_status_t *)ctx;
    return is->quit;
}


int stream_component_open(media_status_t * ms)
{
	if (!ms)
	{
		log_err("null pointer.\n");
		return -1;
	}

	ms->fmt_ctx_ptr = avformat_alloc_context();
	if (!ms->fmt_ctx_ptr)
		return -1;

//	ms->fmt_ctx_ptr->interrupt_callback.callback = decode_interrupt_cb;
//	ms->fmt_ctx_ptr->interrupt_callback.opaque = ms;

	if (avformat_open_input(&ms->fmt_ctx_ptr, ms->filename, NULL, NULL))
	{
		avformat_free_context(ms->fmt_ctx_ptr);
		return -1;
	}

	avformat_find_stream_info(ms->fmt_ctx_ptr, NULL);
//	av_dump_format(ms->fmt_ctx_ptr, 0, ms->filename, 0);
	ms->audio.valid = 0;
	ms->video.valid = 0;
	if ((ms->audio.stream_id = av_find_best_stream(ms->fmt_ctx_ptr, AVMEDIA_TYPE_AUDIO, -1, -1,
									&ms->audio.codec_ptr, 0)) >= 0)
	{
		SDL_AudioSpec wanted_spec, spec;
		int64_t wanted_layout;
		int channels;

		ms->audio.codec_ctx_ptr = ms->fmt_ctx_ptr->streams[ms->audio.stream_id]->codec;
		wanted_layout = ms->audio.codec_ctx_ptr->channel_layout;
		channels = av_get_channel_layout_nb_channels(wanted_layout);

		if (channels != ms->audio.codec_ctx_ptr->channels)
		{
			wanted_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
			channels = av_get_channel_layout_nb_channels(wanted_layout);
		}

		ms->audio.valid = 1;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = ms;
		wanted_spec.silence = 0;
		wanted_spec.format = AUDIO_S16;
		wanted_spec.channels = channels;
		wanted_spec.freq = ms->audio.codec_ctx_ptr->sample_rate;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;

		if (SDL_OpenAudio(&wanted_spec, &spec))
			goto failed;

		ms->audio.out_channels = spec.channels;
		ms->audio.out_channel_layout = av_get_default_channel_layout(spec.channels);
		ms->audio.out_sample_format = AV_SAMPLE_FMT_S16;
		ms->audio.out_sample_rate = spec.freq;
		ms->audio.buf_index = 0;
		ms->audio.buf_size = 0;
		ms->audio.pkt_index = 0;
		ms->audio.pkt_size = 0;

		if (avcodec_open2(ms->audio.codec_ctx_ptr, ms->audio.codec_ptr, NULL))
		{
			av_log(NULL, AV_LOG_PANIC, "open audio stream failed.\n");
		}
		//ms->audio.pkt_queue = new circular_queue(MAX_PACKET_NUM);
		ms->audio.pkt_queue = (packet_queue_t *)malloc(sizeof(packet_queue_t));
		packet_queue_init(ms->audio.pkt_queue);

		SDL_PauseAudio(0);
	}

	if ((ms->video.stream_id = av_find_best_stream(ms->fmt_ctx_ptr, AVMEDIA_TYPE_VIDEO, -1, -1,
						&ms->video.codec_ptr, 0)) >= 0)
	{
		ms->video.valid = 1;
		ms->video.codec_ctx_ptr = ms->fmt_ctx_ptr->streams[ms->video.stream_id]->codec;
		ms->video.codec_ctx_ptr->get_buffer = our_get_buffer;
		ms->video.codec_ctx_ptr->release_buffer = our_release_buffer;
		ms->screen = SDL_SetVideoMode(ms->video.codec_ctx_ptr->width,
								ms->video.codec_ctx_ptr->height, 0, 0);
		ms->video.dst_width = ms->video.codec_ctx_ptr->width;
		ms->video.dst_height = ms->video.codec_ctx_ptr->height;
		ms->video.dst_fmt = SDL_YV12_OVERLAY;
//		ms->video.overlay_pool = new pool(MAX_FRAME_NUM);
//		ms->video.pict_pool = new pool(MAX_FRAME_NUM);
		ms->video.pkt_queue = (packet_queue_t *)malloc(sizeof(packet_queue_t));
		ms->frame_last_delay = 40e-3;
		ms->frame_timer = (double) av_gettime() / 1000000.0;
		ms->video.pictq_mutex = SDL_CreateMutex();
		ms->video.pictq_cond = SDL_CreateCond();
		ms->video.pictq_size = 0;
		ms->video.pictq_rindex = 0;
		ms->video.pictq_windex = 0;
		packet_queue_init(ms->video.pkt_queue);
		schedule_refresh(ms, 100);
		pthread_create(&ms->video_tid, NULL, video_thread, ms);

		if (avcodec_open2(ms->video.codec_ctx_ptr, ms->video.codec_ptr, NULL))
		{
			av_log(NULL, AV_LOG_PANIC, "open video stream failed.\n");
		}
	}
	return 0;

failed:
	avformat_close_input(&ms->fmt_ctx_ptr);
	avformat_free_context(ms->fmt_ctx_ptr);

	return 0;
}

//void alloc_picture(void * userdata)
//{
//	media_status_t * ms = (media_status_t *)userdata;
//
//	SDL_Overlay * bmp = SDL_CreateYUVOverlay(ms->video.dst_width, ms->video.dst_height,
//										ms->video.dst_fmt, ms->screen);
//	if (!bmp)
//		return;
//	ms->video.overlay_pool->set_entry_internal(bmp);
//}

void alloc_picture(void * userdata)
{
	media_status_t * ms = (media_status_t *)userdata;
	video_picture_t * vp = NULL;

	vp = &ms->video.picture_queue[ms->video.pictq_windex];
	vp->bmp = SDL_CreateYUVOverlay(ms->video.dst_width, ms->video.dst_height, ms->video.dst_fmt,
			ms->screen);

	SDL_LockMutex(ms->video.pictq_mutex);
	vp->allocated = 1;
	SDL_CondSignal(ms->video.pictq_cond);
	SDL_UnlockMutex(ms->video.pictq_mutex);
}



void video_refresh_timer(void * userdata)
{
	media_status_t * ms = (media_status_t *)userdata;
	video_picture_t * vp = NULL;
//	double pts;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	//schedule_refresh(ms, 30);

	//if (ms->video.pict_pool->availables())
	if (ms->video.codec_ctx_ptr)
	{
		if (ms->video.pictq_size == 0)
		{
			schedule_refresh(ms, 1);
		}
		else
		{
			//vp = (video_picture_t *) ms->video.pict_pool->get_entry_internal();
			vp = &ms->video.picture_queue[ms->video.pictq_rindex];

			delay = vp->pts - ms->frame_last_pts;
			if (delay <= 0 || delay > 0.1)
			{
				delay = ms->frame_last_delay;
			}
			ms->frame_last_delay = delay;
			ms->frame_last_pts = vp->pts;

			ref_clock = get_audio_clock(ms);
			diff = vp->pts - ref_clock;
			sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;

			if (fabs(diff) < 10)
			{
				if (diff <= -sync_threshold)
					delay = 0;
				if (diff >= sync_threshold)
					delay *= 2;
			}

			ms->frame_timer += delay;
			actual_delay = ms->frame_timer - (av_gettime() / 1000000.0);
			if (actual_delay < 0.010)
				actual_delay = 0.010;

//			printf("%d\n", (int)(actual_delay * 1000 + 0.5));
			schedule_refresh(ms, (int) (actual_delay * 1000 + 0.5));  /*  timer的单位是毫秒  */

			display_video(ms, vp->bmp);
			SDL_LockMutex(ms->video.pictq_mutex);
			if (++ms->video.pictq_rindex == PICTURE_QUEUE_SIZE)
			{
				ms->video.pictq_rindex = 0;
			}
			ms->video.pictq_size--;
			SDL_CondSignal(ms->video.pictq_cond);
			SDL_UnlockMutex(ms->video.pictq_mutex);
		}
	} else {
		schedule_refresh(ms, 100);
	}
}

void media_control_init(media_status_t * ms)
{
	media_control_t * ctl = NULL;

	ctl = &ms->control;
	ctl->mutex = SDL_CreateMutex();
	ctl->cond = SDL_CreateCond();
	ctl->flags = 0;
}

void media_control_play(media_status_t * ms)
{
	media_control_t * ctrl = &ms->control;

	SDL_LockMutex(ctrl->mutex);
	if (ctrl->flags != MEDIA_CONTROL_PLAY) {
		ctrl->flags = MEDIA_CONTROL_PLAY;
		SDL_CondSignal(ctrl->cond);
	}
	SDL_UnlockMutex(ctrl->mutex);
}

void media_control_pause(media_status_t * ms)
{
	media_control_t * ctrl = &ms->control;

	SDL_LockMutex(ctrl->mutex);
	if (ctrl->flags == MEDIA_CONTROL_PLAY) {
		ctrl->flags = MEDIA_CONTROL_PAUSE;
	}
	SDL_UnlockMutex(ctrl->mutex);
}

void media_control_stop(media_status_t * ms)
{
	media_control_t * ctrl = &ms->control;

	SDL_LockMutex(ctrl->mutex);
	if (ctrl->flags != MEDIA_CONTROL_STOP) {
		ctrl->flags = MEDIA_CONTROL_STOP;
	}
	SDL_UnlockMutex(ctrl->mutex);
}

int main(int argc, char ** argv)
{
	media_status_t * ms = NULL;
	SDL_Event event;

	if (argc < 2)
	{
		av_log(NULL, AV_LOG_ERROR, "miss arguments.\n");
		av_log(NULL, AV_LOG_ERROR, "Usage: %s filename.\n", argv[0]);
		exit(-1);
	}

	XInitThreads();

	ms = (media_status_t *)av_mallocz(sizeof(media_status_t));
	if (!ms)
	{
		av_log(NULL, AV_LOG_ERROR, "%s.\n", av_err2str(errno));
		exit(-1);
	}

	ms->filename = av_strdup(argv[1]);
	ms->quit = 0;
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	av_register_all();
	avformat_network_init();

	if (stream_component_open(ms))
		goto error;

	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t *)"FLUSH";

	pthread_create(&ms->parse_tid, NULL, parse_thread, ms);
	media_control_init(ms);


	while (SDL_WaitEvent(&event))
	{
		double pos, incr;

		switch (event.type)
		{
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym)
				{
					case SDLK_LEFT:
						incr = -0.00002;
						goto do_seek;
					case SDLK_RIGHT:
						incr = 0.00002;
						goto do_seek;
					case SDLK_UP:
						incr = 60.0;
						goto do_seek;
					case SDLK_DOWN:
//						incr = -60.0;
						goto do_seek;
					case SDLK_BACKSPACE:
					{
						media_control_pause(ms);
					}
						break;

					do_seek:
						pos = get_audio_clock(ms);
						printf("before seek: %f\n", pos);
						pos += incr;
						media_control_play(ms);
						stream_seek(ms, (int64_t) (pos * AV_TIME_BASE), incr);
						break;
					default:
						break;
				}
				break;

			case SDL_QUIT:
				ms->quit = 1;
				SDL_Quit();
				break;
			case ALLOC_PICTURE:
				alloc_picture(ms);
				break;
			case REFRESH_TIMER:
				video_refresh_timer(ms);
				break;
			default:
				break;
		}
	}

//	pthread_join(ms->parse_tid, NULL);
//	pthread_join(ms->video_tid, NULL);

//	free(ms->audio.pkt_queue);
//	free(ms->video.pkt_queue);
//	avformat_close_input(&ms->fmt_ctx_ptr);
//	avformat_free_context(ms->fmt_ctx_ptr);
error:
	av_freep(ms->filename);
	av_freep(ms);
	return(0);
}


