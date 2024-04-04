package main

/*
#define QUEUE_SIZE 16
#define PACKET_SIZE 32 * 1024

typedef struct {
    int is_idr;
}VideoMetadata;

typedef struct {
    int size;
    VideoMetadata metadata;
    char data[PACKET_SIZE];
} Packet;

typedef enum _EventType {
    POINTER_VISIBLE,
    CHANGE_BITRATE,
    CHANGE_FRAMERATE,
    CHANGE_DISPLAY,
    IDR_FRAME,

    STOP,
    HDR_CALLBACK,
    EVENT_TYPE_MAX
} EventType;

typedef enum _DataType {
    HDR_INFO,
} DataType;

typedef struct {
    int value_number;
    char value_raw[PACKET_SIZE];
    int data_size;

    DataType type;

    int read;
} Event;

typedef struct {
    Packet audio[QUEUE_SIZE];
    Packet video[QUEUE_SIZE];
    int audio_order[QUEUE_SIZE];
    int video_order[QUEUE_SIZE];


    Event events[EVENT_TYPE_MAX];
}SharedMemory;




*/
import "C"
import (
	"errors"
	"fmt"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)


func main(){
	mod,err := syscall.LoadDLL("libparent.dll")
	if err != nil {
		panic(err)
	}
	deinit,err := mod.FindProc("deinit_shared_memory")
	if err != nil {
		panic(err)
	}
	defer deinit.Call()

	proc,err := mod.FindProc("allocate_shared_memory")
	if err != nil {
		panic(err)
	}

	buffer := make([]byte,128) 
	handle := C.longlong(0)
	ptr,_,err := proc.Call(
		uintptr(unsafe.Pointer(&buffer[0])),
		uintptr(unsafe.Pointer(&handle)))
	if !errors.Is(err, windows.ERROR_SUCCESS) {
		panic(err)
	}

	// fun,err := syscall.GetProcAddress(lib, "allocate_shared_memory")
	// if err != nil {
	// 	panic(err)
	// }

	// i := C.longlong(0)
	// ret, _, callErr := syscall.SyscallN(fun,uintptr(unsafe.Pointer(&i)))
	// if callErr != 0 {
	// 	panic(err)
	// }

	// fmt.Println(i)

	fmt.Println(string(buffer))
	memory := (*C.SharedMemory)(unsafe.Pointer(ptr))
	fmt.Println(memory.video_order)
	fmt.Println(memory.audio_order)
}