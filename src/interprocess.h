/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "thread_pool.h"
#include "thread_safe.h"

#include <smemory.h>


Queue*
init_shared_memory(char* data);