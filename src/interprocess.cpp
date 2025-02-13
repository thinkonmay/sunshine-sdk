/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "interprocess.h"
#include "logging.h"

#include <thread>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <windows.h>
#pragma comment(lib, "user32.lib")



void*
map_file(char* name)
{
   HANDLE hMapFile;

   hMapFile = OpenFileMappingA(
                   FILE_MAP_ALL_ACCESS,   // read/write access
                   FALSE,                 // do not inherit the name
                   name);               // name of mapping object

   if (hMapFile == NULL) {
      BOOST_LOG(error) << "Could not open file mapping object " <<  GetLastError();
      return nullptr;
   }

    void* pBuf = MapViewOfFile(hMapFile, // handle to map object
               FILE_MAP_ALL_ACCESS,  // read/write permission
               0,
               0,
               sizeof(Queue));

    if (pBuf == NULL) {
        BOOST_LOG(error) << "Could not map view of file (%d) " << GetLastError();
        CloseHandle(hMapFile);
        return nullptr;
    }

   return pBuf;
}
Queue*
init_shared_memory(char* data){
    Queue* memory = (Queue*)map_file(data);
    return memory;
}