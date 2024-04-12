package main

/*
#include "smemory.h"
#include "Input.h"
#include <string.h>


void
write(Queue* queue, void* data, int size) {
	int new_index = queue->index + 1;
	Packet* block = &queue->array[new_index % QUEUE_SIZE];
	block->size = size;
	memcpy(block->data,data,size);
	queue->index = new_index;
}


void
keyboard_passthrough(Queue* queue, int keycode, int up, int scancode) {
	NV_KEYBOARD_PACKET packet = {0};
	packet.header.magic = up == 0
		? KEY_DOWN_EVENT_MAGIC
		: KEY_UP_EVENT_MAGIC;
	packet.keyCode = keycode;
	write(queue,&packet,sizeof(NV_KEYBOARD_PACKET));
}


*/
import "C"
import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"
	"unsafe"

	"github.com/thinkonmay/sunshine-sdk/codec"
	"github.com/thinkonmay/sunshine-sdk/codec/av1"
	"github.com/thinkonmay/sunshine-sdk/codec/h264"
	"github.com/thinkonmay/sunshine-sdk/codec/h265"
	"github.com/thinkonmay/sunshine-sdk/codec/opus"
	"golang.org/x/sys/windows"
)

func byteSliceToString(s []byte) string {
	n := bytes.IndexByte(s, 0)
	if n >= 0 {
		s = s[:n]
	}
	return string(s)
}

func ConvertBEEdian(in uint16) C.short {
	bytes := binary.BigEndian.AppendUint16([]byte{}, in)
	return C.short(binary.LittleEndian.Uint16(bytes))
}

func main() {
	mod, err := syscall.LoadDLL("libparent.dll")
	if err != nil {
		panic(err)
	}
	deinit, err := mod.FindProc("deinit_shared_memory")
	if err != nil {
		panic(err)
	}
	defer deinit.Call()

	obtain, err := mod.FindProc("obtain_shared_memory")
	if err != nil {
		panic(err)
	}
	proc, err := mod.FindProc("allocate_shared_memory")
	if err != nil {
		panic(err)
	}

	buffer := make([]byte, 128)
	_, _, err = proc.Call(
		uintptr(unsafe.Pointer(&buffer[0])),
	)
	if !errors.Is(err, windows.ERROR_SUCCESS) {
		panic(err)
	}

	pointer, _, err := obtain.Call(
		uintptr(unsafe.Pointer(&buffer[0])),
	)
	if !errors.Is(err, windows.ERROR_SUCCESS) {
		panic(err)
	}

	memory := (*C.SharedMemory)(unsafe.Pointer(pointer))
	memory.queues[C.Video0].metadata.codec = C.int(0)
	memory.queues[C.Video1].metadata.codec = C.int(0)
	memory.queues[C.Audio].metadata.codec = C.int(3)

	payloaders := map[C.int]func() codec.Payloader{
		0: func() codec.Payloader { return &h264.Payloader{} },
		1: func() codec.Payloader { return &h265.Payloader{} },
		2: func() codec.Payloader { return &av1.Payloader{} },
		3: func() codec.Payloader { return &opus.Payloader{} },
	}

	indexes := make([]*int, C.QueueMax)
	for i, _ := range indexes {
		j := int(memory.queues[i].index)
		indexes[i] = &j
	}

	go func(queue *C.Queue, index *int) {
		buffer := make([]byte, int(C.PACKET_SIZE))
		payloader := payloaders[queue.metadata.codec]()

		go func() {
			for {
				queue.events[C.Idr].value_number = 1
				queue.events[C.Idr].read = 0
				time.Sleep(time.Second)
			}
		}()

		for {
			for int(queue.index) > *index {
				new_index := *index + 1
				real_index := new_index % C.QUEUE_SIZE
				block := queue.array[real_index]

				C.memcpy(unsafe.Pointer(&buffer[0]), unsafe.Pointer(&block.data[0]), C.ulonglong(block.size))
				payloads := payloader.Payload(1200, buffer[:block.size])
				fmt.Printf("downstream index %d, upstream index %d, receive size %d\n", new_index, queue.index, len(payloads))

				*index = new_index
			}

			time.Sleep(time.Microsecond * 100)
		}
	}(&memory.queues[C.Video0], indexes[C.Video0])

	go func(queue *C.Queue, index *int) {
		buffer := make([]byte, 32)
		for {
			_, err := os.Stdin.Read(buffer)
			if err != nil {
				return
			}

			command := buffer[0]
			switch command {
			// case []byte("n")[0]:
			// 	packet := C.NV_REL_MOUSE_MOVE_PACKET{
			// 		header: C.NV_INPUT_HEADER{
			// 			magic: C.MOUSE_MOVE_REL_MAGIC_GEN5,
			// 		},
			// 		deltaX: ConvertBEEdian(10),
			// 		deltaY: ConvertBEEdian(10),
			// 	}

			// 	Write(queue, unsafe.Pointer(&packet), int(unsafe.Sizeof(packet)))
			// 	_ = packet // use packet here to avoid go gc clear packet var
			// case []byte("m")[0]:
			// 	packet := C.NV_ABS_MOUSE_MOVE_PACKET{
			// 		header: C.NV_INPUT_HEADER{
			// 			magic: C.MOUSE_MOVE_ABS_MAGIC,
			// 		},
			// 		x:      ConvertBEEdian(1920),
			// 		y:      ConvertBEEdian(1080),
			// 		width:  ConvertBEEdian(3840),
			// 		height: ConvertBEEdian(2160),
			// 	}

			// 	Write(queue, unsafe.Pointer(&packet), int(unsafe.Sizeof(packet)))
			// 	_ = packet // use packet here to avoid go gc clear packet var
			case []byte("k")[0]:
				C.keyboard_passthrough(queue, 0, 1, 0)
			}
		}
	}(&memory.queues[C.Input], indexes[C.Input])

	fmt.Printf("execute sunshine with command : ./sunshine.exe \"%s\" 0\n", byteSliceToString(buffer))
	chann := make(chan os.Signal, 16)
	signal.Notify(chann, syscall.SIGTERM, os.Interrupt)
	<-chann
	fmt.Println("Stopped.")
}
