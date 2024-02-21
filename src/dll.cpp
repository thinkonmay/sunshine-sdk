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
    safe::mail_t mail;
    std::chrono::steady_clock::time_point start;
    Codec codec;
};

static audio::config_t soundcard;
static video::config_t monitor;
std::unique_ptr<platf::deinit_t> deinit_guard;
extern Pipeline *__cdecl StartQueue(int codec) {
    static Pipeline video;
    static Pipeline audio;
    static bool init = false;
    if (!init) {
        init = true;
        monitor = {1920, 1080, 60, 6000, 1, 0, 1, 0, 0};
        soundcard = {10,2,3,0};
        deinit_guard = platf::init();
        if (!deinit_guard) {
            BOOST_LOG(error) << "Platform failed to initialize"sv;
        } else if (video::probe_encoders()) {
            BOOST_LOG(error) << "Video failed to find working encoder"sv;
            return NULL;
        }
    }


    bool is_video = true;
    switch (codec) {
        case H265:  // h265
            BOOST_LOG(info) << ("starting pipeline with h265 codec\n");
            monitor.videoFormat = 1;
            config::video.hevc_mode = 1;
            config::video.av1_mode = 0;
            break;
        case AV1:  // av1
            BOOST_LOG(info) << ("starting pipeline with av1 codec\n");
            monitor.videoFormat = 2;
            config::video.hevc_mode = 0;
            config::video.av1_mode = 1;
            break;
        case OPUS:  // av1
            BOOST_LOG(info) << ("starting pipeline with opus codec\n");
            is_video = false;
            break;
        default:
            BOOST_LOG(info) << ("starting pipeline with h264 codec\n");
            monitor.videoFormat = 0;
            config::video.hevc_mode = 0;
            config::video.av1_mode = 0;
            break;
    }


    


    
    auto pipeline = is_video 
        ? &video
        : &audio;

    pipeline->mail = std::make_shared<safe::mail_raw_t>();
    pipeline->start = std::chrono::steady_clock::now();
    pipeline->codec = (Codec)codec;
    auto thread = is_video 
        ? std::thread{ [&]() { 
            BOOST_LOG(info) << "Starting video thread"sv;
            video::capture(video.mail, monitor, NULL); 
        }}
        : std::thread{ [&]() { 
            BOOST_LOG(info) << "Starting audio thread"sv;
            audio::capture(audio.mail, soundcard, NULL); 
        }};
    thread.detach();
    return pipeline;
}

long long duration_to_latency(std::chrono::steady_clock::duration duration) {
    const auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return std::clamp<decltype(duration_ns)>((duration_ns + 50) / 100, 0,
                                             std::numeric_limits<int>::max());
};

int __cdecl PopFromQueue(Pipeline *pipeline, void *data, int *duration) {
    if (pipeline->codec != OPUS) {
        auto packet = pipeline->mail->queue<video::packet_t>(mail::video_packets)->pop();
        memcpy(data, packet->data(), packet->data_size());
        int size = packet->data_size();
        return size;
    } else {
        auto packet = pipeline->mail->queue<audio::packet_t>(mail::audio_packets)->pop();
        memcpy(data, packet->begin(),packet->size());
        return packet->size();
    }
}


void __cdecl RaiseEventS(Pipeline *pipeline, EventType event,
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
void __cdecl RaiseEvent(Pipeline *pipeline, EventType event, int value) {
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

void __cdecl WaitEvent(Pipeline *pipeline, EventType event) {
    while (!PeekEvent(pipeline, event)) std::this_thread::sleep_for(10ms);
}

int __cdecl PeekEvent(Pipeline *pipeline, EventType event) {
    switch (event) {
        case STOP:  // IDR FRAME
            return pipeline->mail->event<bool>(mail::shutdown)->peek();
        default:
            return 0;
    }
}
