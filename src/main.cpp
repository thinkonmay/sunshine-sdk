/**
 * @file src/main.cpp
 * @brief Main entry point for Sunshine.
 */

// standard includes
#include <iostream>
#include <thread>
#include <Windows.h>

#include "dll.h"



static HMODULE 			hModule;
static STARTQUEUE 		callstart;
static POPFROMQUEUE 	callpop;
static WAITEVENT		callwait;
static RAISEEVENT       callraise;
static PEEKEVENT		callpeek;

int
initlibrary() {
	hModule 	= LoadLibraryA(".\\libsunshine.dll");
	callstart 	= (STARTQUEUE)		GetProcAddress( hModule,"StartQueue");
	callpop 	= (POPFROMQUEUE)	GetProcAddress( hModule,"PopFromQueue");
	callraise 	= (RAISEEVENT)		GetProcAddress( hModule,"RaiseEvent");
	callwait	= (WAITEVENT)		GetProcAddress( hModule,"WaitEvent");
	callpeek    = (PEEKEVENT)		GetProcAddress( hModule,"PeekEvent");

	if(callpop ==0 || callstart == 0 || callraise == 0 || callwait == 0)
		return 1;

	return 0;
}

int main(int argc, char *argv[])
{
	initlibrary();
	// second encode session
	for (int i =0; i < 30; i++)
	{
		VideoPipeline *pipeline = callstart(1920, 1080, 6000, 60, 1);
		auto video = std::thread{[&]() {
			// Video traffic is sent on this thread
			int duration = 0;
			void *data = malloc(100 * 1000 * 1000);

			int count = 0;
			while (true) {
				int size = callpop(pipeline, data, &duration);
				if (callpeek(pipeline,STOP,NULL) || count == 1000) {
					break;
				}

				printf("received packet with size %d\n", size);
				count++;
			}

			callraise(pipeline,STOP,0);
			free(data);
		}};

		callwait(pipeline,STOP,NULL);
		video.join();
	}
	return 0;
}