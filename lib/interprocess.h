/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#if _WIN32
#define EXPORT(x)  extern __declspec(dllexport) x __cdecl
#else
#define EXPORT(x) __attribute__((visibility("default"))) extern x
#endif

extern "C" {
#include <smemory.h>



EXPORT(SharedMemory*) allocate_shared_memory(char* rand) ;
EXPORT(SharedMemory*) obtain_shared_memory(char* rand) ;
EXPORT(void) lock_shared_memory(SharedMemory* memory);
EXPORT(void) unlock_shared_memory(SharedMemory* memory);
EXPORT(void) free_shared_memory(SharedMemory* buffer);
EXPORT(void) deinit_shared_memory();
}