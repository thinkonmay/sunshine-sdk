/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

// local includes
#include "config.h"
#include "main.h"
#include "dll.h"
#include "platform/common.h"
#include "video.h"

struct _VideoPipeline
{
	std::chrono::steady_clock::time_point start;
	video::config_t monitor;
	safe::mail_t mail;
};

extern VideoPipeline *__cdecl StartQueue(int video_width,
										 int video_height,
										 int video_bitrate,
										 int video_framerate,
										 int video_codec)
{
	static bool init = false;
	if (!init)
	{
		// If any of the following fail, we log an error and continue event though sunshine will not function correctly.
		// This allows access to the UI to fix configuration problems or view the logs.
		if (platf::init())
		{
			// BOOST_LOG(error) << "Platform failed to initialize"sv;
			return NULL;
		}
		else if (video::probe_encoders())
		{
			// BOOST_LOG(error) << "Video failed to find working encoder"sv;
			return NULL;
		}
		init = true;
	}

	static VideoPipeline pipeline = {};
	auto mail = std::make_shared<safe::mail_raw_t>();
	pipeline.mail = mail;
	pipeline.monitor = {1920, 1080, 60, 6000, 1, 0, 1, 0, 0};
	pipeline.start = std::chrono::steady_clock::now();

	switch (video_codec)
	{
	case 2: // h265
		pipeline.monitor.videoFormat = 1;
		config::video.hevc_mode = 1;
		config::video.av1_mode = 0;
		break;
	case 3: // av1
		pipeline.monitor.videoFormat = 2;
		config::video.hevc_mode = 0;
		config::video.av1_mode = 1;
		break;
	default:
		pipeline.monitor.videoFormat = 0;
		config::video.hevc_mode = 0;
		config::video.av1_mode = 0;
		break;
	}

	auto thread = std::thread{[&](){ video::capture(pipeline.mail, pipeline.monitor, NULL); }};
	thread.detach();

	return &pipeline;
}

int __cdecl 
PopFromQueue(VideoPipeline *pipeline,
			void *data,
			int *duration)
{
	auto packets = pipeline->mail->queue<video::packet_t>(mail::video_packets);
	auto packet = packets->pop();

	if (packet->frame_timestamp)
	{
		auto duration_to_latency = [](const std::chrono::steady_clock::duration &duration)
		{
			const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
			return std::clamp<decltype(duration_ns)>((duration_ns + 50) / 100, 0, std::numeric_limits<int>::max());
		};

		*duration = duration_to_latency(*packet->frame_timestamp - pipeline->start);
		pipeline->start = *packet->frame_timestamp;
	}

	memcpy(data, packet->data(), packet->data_size());
	return packet->data_size();
}

void __cdecl 
RaiseEvent(VideoPipeline *pipeline,
			EventType event,
			int value)
{
	switch (event)
	{
	case IDR_FRAME: // IDR FRAME
		pipeline->mail->event<bool>(mail::idr)->raise(true);
		break;
	case STOP: // IDR FRAME
		pipeline->mail->event<bool>(mail::shutdown)->raise(true);
		break;
	default:
		break;
	}
}

void __cdecl 
WaitEvent(VideoPipeline* pipeline,
          EventType event,
          int* value)
{
	switch (event)
	{
	case STOP: // IDR FRAME
		pipeline->mail->event<bool>(mail::shutdown)->pop();
		break;
	default:
		break;
	}
}



int main(int argc, char *argv[])
{

	VideoPipeline *pipeline = StartQueue(1920, 1080, 6000, 60, 1);
	auto video = std::thread{[&]() {
		// Video traffic is sent on this thread
		platf::adjust_thread_priority(platf::thread_priority_e::high);
		int duration = 0;
		void *data = malloc(100 * 1000 * 1000);

		int count = 0;
		while (true) {
			int size = PopFromQueue(pipeline, data, &duration);
			if (pipeline->mail->event<bool>(mail::shutdown)->peek() || count == 10) {
				break;
			}

			printf("received packet with size %d\n", size);
			count++;
		}

		RaiseEvent(pipeline,STOP,0);
	}};

	int val;
	WaitEvent(pipeline,STOP,&val);
	return 0;
}