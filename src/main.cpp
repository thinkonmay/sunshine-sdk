/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <Windows.h>

#include <iostream>
#include <thread>

#include "dll.h"

static HMODULE hModule;
static STARTQUEUE callstart;
static POPFROMQUEUE callpop;
static WAITEVENT callwait;
static RAISEEVENT callraise;
static RAISEEVENTS callraiseS;
static PEEKEVENT callpeek;

int initlibrary() {
    char szFullPath[MAX_PATH] = {};
    GetCurrentDirectoryA(MAX_PATH, szFullPath);
    strcat(szFullPath, "\\libsunshine.dll");
    hModule = LoadLibraryA(szFullPath);
    if (hModule == 0) return 1;

    callstart = (STARTQUEUE)GetProcAddress(hModule, "StartQueue");
    callpop = (POPFROMQUEUE)GetProcAddress(hModule, "PopFromQueue");
    callraise = (RAISEEVENT)GetProcAddress(hModule, "RaiseEvent");
    callraiseS = (RAISEEVENTS)GetProcAddress(hModule, "RaiseEventS");
    callwait = (WAITEVENT)GetProcAddress(hModule, "WaitEvent");
    callpeek = (PEEKEVENT)GetProcAddress(hModule, "PeekEvent");

    if (callpop == 0 || callstart == 0 || callraise == 0 || callwait == 0)
        return 1;

    return 0;
}

int main(int argc, char *argv[]) {
    if (initlibrary()) {
        printf("failed to load libsunshine.dll");
        return 1;
    }

    // second encode session
    for (int i = 0; i < 30; i++) {
        VideoPipeline *pipeline = callstart(1);
        auto video = std::thread{[&]() {
            // Video traffic is sent on this thread
            int duration = 0;
            void *data = malloc(100 * 1000 * 1000);

            auto start = std::chrono::system_clock::now();
            int count = 0;
            while (true) {
                int size = callpop(pipeline, data, &duration);
                if (callpeek(pipeline, STOP) || count == 1000) {
                    break;
                } else if (count % 100 == 0) {
                    callraise(pipeline, CHANGE_BITRATE, 2000);
                } else if (count % 100 == 50) {
                    // callraiseS(pipeline, CHANGE_DISPLAY, "\\\\.\\DISPLAY1");
                    callraise(pipeline, CHANGE_FRAMERATE, 50);
                }

                auto end = std::chrono::system_clock::now();
                auto time = std::chrono::duration<double, std::milli>(end - start);
                start = end;
                printf("received packet with size %d, timestamp %f\n", size,time.count());
                count++;
            }

            callraise(pipeline, STOP, 0);
            free(data);
        }};

        callwait(pipeline, STOP);
        video.join();
    }
    return 0;
}