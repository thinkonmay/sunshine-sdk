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
#include "dll.h"
#include "main.h"
#include "platform/common.h"
#include "video.h"
#include "audio.h"

using namespace std::literals;

struct _VideoPipeline {
    std::chrono::steady_clock::time_point start;
    video::config_t monitor;
    audio::config_t soundcard;
    safe::mail_t mail;
};

extern VideoPipeline *__cdecl StartQueue(int video_codec) {
    static bool init = false;
    if (!init) {
        auto deinit_guard = platf::init();
        if (!deinit_guard) {
            BOOST_LOG(error) << "Platform failed to initialize"sv;
        } else if (video::probe_encoders()) {
            BOOST_LOG(error) << "Video failed to find working encoder"sv;
            return NULL;
        }
        init = true;
    }

    static VideoPipeline pipeline = {};
    pipeline.mail = std::make_shared<safe::mail_raw_t>();
    pipeline.monitor = {1920, 1080, 60, 6000, 1, 0, 1, 0, 0};
    pipeline.soundcard = {5,2,audio::config_t::HIGH_QUALITY};
    pipeline.start = std::chrono::steady_clock::now();

    switch (video_codec) {
        case H265:  // h265
            BOOST_LOG(info) << ("starting pipeline with h265 codec\n");
            pipeline.monitor.videoFormat = 1;
            config::video.hevc_mode = 1;
            config::video.av1_mode = 0;
            break;
        case AV1:  // av1
            BOOST_LOG(info) << ("starting pipeline with av1 codec\n");
            pipeline.monitor.videoFormat = 2;
            config::video.hevc_mode = 0;
            config::video.av1_mode = 1;
            break;
        default:
            BOOST_LOG(info) << ("starting pipeline with h264 codec\n");
            pipeline.monitor.videoFormat = 0;
            config::video.hevc_mode = 0;
            config::video.av1_mode = 0;
            break;
    }

    auto video = std::thread{
        [&]() { video::capture(pipeline.mail, pipeline.monitor, NULL); }};
    video.detach();

    auto audio = std::thread{
        [&]() { audio::capture(pipeline.mail, pipeline.soundcard, NULL); }};
    audio.detach();

    return &pipeline;
}

long long duration_to_latency(std::chrono::steady_clock::duration duration) {
    const auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return std::clamp<decltype(duration_ns)>((duration_ns + 50) / 100, 0,
                                             std::numeric_limits<int>::max());
};

int __cdecl PopFromQueue(VideoPipeline *pipeline, void *data, int *duration) {
    auto packet =
        pipeline->mail->queue<video::packet_t>(mail::video_packets)->pop();
    // if (packet->frame_timestamp) {
    // 	*duration = duration_to_latency(*packet->frame_timestamp -
    // pipeline->start); 	pipeline->start = *packet->frame_timestamp;
    // }

    memcpy(data, packet->data(), packet->data_size());
    int size = packet->data_size();
    return size;
}

void __cdecl RaiseEventS(VideoPipeline *pipeline, EventType event,
                         char *value) {
    switch (event) {
        case CHANGE_DISPLAY:  // IDR FRAME
            pipeline->mail->event<std::string>(mail::switch_display)
                ->raise(std::string(value));
            break;
        default:
            break;
    }
}
void __cdecl RaiseEvent(VideoPipeline *pipeline, EventType event, int value) {
    switch (event) {
        case IDR_FRAME:  // IDR FRAME
            pipeline->mail->event<bool>(mail::idr)->raise(true);
            break;
        case STOP:  // IDR FRAME
            pipeline->mail->event<bool>(mail::shutdown)->raise(true);
            break;
        case POINTER_VISIBLE:  // IDR FRAME
            pipeline->mail->event<bool>(mail::toggle_cursor)->raise(value != 0);
            break;
        case CHANGE_BITRATE:  // IDR FRAME
            pipeline->mail->event<int>(mail::bitrate)->raise(value);
            break;
        case CHANGE_FRAMERATE:  // IDR FRAME
            pipeline->mail->event<int>(mail::framerate)->raise(value);
            break;
        default:
            break;
    }
}

void __cdecl WaitEvent(VideoPipeline *pipeline, EventType event) {
    while (!PeekEvent(pipeline, event)) std::this_thread::sleep_for(10ms);
}

int __cdecl PeekEvent(VideoPipeline *pipeline, EventType event) {
    switch (event) {
        case STOP:  // IDR FRAME
            return pipeline->mail->event<bool>(mail::shutdown)->peek();
        default:
            return 0;
    }
}
