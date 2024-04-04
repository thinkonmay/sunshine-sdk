
#include <boost/interprocess/managed_shared_memory.hpp>
#include <cstdlib> //std::system
#include <thread>
#include <sstream>

#include <iostream>
#include <thread>

#include "interprocess.h"


using namespace boost::interprocess;
using namespace std::literals;
int main (int argc, char *argv[]) {

    //Remove shared memory on construction and destruction
    struct shm_remove {
        shm_remove() {  shared_memory_object::remove("MySharedMemory"); }
        ~shm_remove(){  shared_memory_object::remove("MySharedMemory"); }
    } remover;

    //Create a managed shared memory segment
    managed_shared_memory segment(create_only, "MySharedMemory", 2 * sizeof(SharedMemory));

    //Allocate a portion of the segment (raw memory)
    std::size_t free_memory = segment.get_free_memory();
    SharedMemory* memory = (SharedMemory*)segment.allocate(sizeof(SharedMemory));
    init_shared_memory(memory);

    //Check invariant
    if(free_memory <= segment.get_free_memory())
        return 1;

    auto running = true;
    auto thread = std::thread{[&](){
        int size;
        char buffer[PACKET_SIZE] = {0};
        while (running) {
            while(peek_audio_packet(memory)) {
                pop_audio_packet(memory,buffer,&size);
                std::cout << "Audio buffer received : " << size << "\n";
            }
            while(peek_video_packet(memory)) {
                pop_video_packet(memory,buffer,&size);
                std::cout << "Video buffer received : " << size << "\n";
            }
            std::this_thread::sleep_for(100us);
        }
    }};

    // auto thread_test = std::thread{[&](){
    //     int size = 120;
    //     char buffer[PACKET_SIZE] = {0};
    //     while (running) {
    //         std::this_thread::sleep_for(1ms);
    //         push_audio_packet(memory,buffer,size);
    //         push_video_packet(memory,buffer,size,VideoMetadata{0});
    //         size++;
    //     }
    // }};

    // while (running)
    //     std::this_thread::sleep_for(1s);


    //An handle from the base address can identify any byte of the shared 
    //memory segment even if it is mapped in different base addresses
    managed_shared_memory::handle_t handle = segment.get_handle_from_address((void*)memory);

#ifdef _WIN32
    auto binary = "sunshine.exe";
#else
    auto binary = "./sunshine";
#endif

    std::stringstream s; s << binary << " " << handle; s << std::ends;

    //Launch child process
    if(0 != std::system(s.str().c_str()))
        return 1;
    if(free_memory != segment.get_free_memory())
        return 1;

    running = false;
    thread.join();
    // thread_test.join();
    return 0;
}