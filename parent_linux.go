package main

/*
#include "smemory.h"
#include <string.h>
*/
import "C"
import (
	"bytes"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"
	"unsafe"

	"github.com/ebitengine/purego"

	"github.com/thinkonmay/sunshine-sdk/codec"
	"github.com/thinkonmay/sunshine-sdk/codec/av1"
	"github.com/thinkonmay/sunshine-sdk/codec/h264"
	"github.com/thinkonmay/sunshine-sdk/codec/h265"
	"github.com/thinkonmay/sunshine-sdk/codec/opus"
)

func byteSliceToString(s []byte) string {
	n := bytes.IndexByte(s, 0)
	if n >= 0 {
		s = s[:n]
	}
	return string(s)
}

func main() {
	libc, err := purego.Dlopen("./libparent.so", purego.RTLD_NOW|purego.RTLD_GLOBAL)
	if err != nil {
		panic(err)
	}

	var deinit func()
	purego.RegisterLibFunc(&deinit, libc, "deinit_shared_memory")
	defer deinit()

	buffer := make([]byte, 128)
	var allocate func(unsafe.Pointer) unsafe.Pointer
	purego.RegisterLibFunc(&allocate, libc, "allocate_shared_memory")
	pointer := allocate(unsafe.Pointer(&buffer[0]))

	memory := (*C.SharedMemory)(unsafe.Pointer(pointer))

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

	for queue_type, i := range indexes {
		go func(queue *C.Queue, index *int) {
			buffer := make([]byte, int(C.PACKET_SIZE))
			payloader := payloaders[queue.metadata.codec]()

			for {
				for int(queue.index) > *index {
					new_index := *index + 1
					real_index := new_index % C.QUEUE_SIZE
					block := queue.array[real_index]

					C.memcpy(unsafe.Pointer(&buffer[0]), unsafe.Pointer(&block.data[0]), C.ulong(block.size))
					payloads := payloader.Payload(1200, buffer[:block.size])
					fmt.Printf("Queue type %d, downstream index %d, upstream index %d, receive size %d\n", queue_type, new_index, queue.index, len(payloads))

					*index = new_index
				}

				time.Sleep(time.Microsecond * 100)
			}
		}(&memory.queues[queue_type], i)
	}

	fmt.Printf("execute sunshine with command : sudo setcap cap_sys_admin+p $(readlink -f sunshine) && ./sunshine %s 0\n", byteSliceToString(buffer))
	chann := make(chan os.Signal, 16)
	signal.Notify(chann, syscall.SIGTERM, os.Interrupt)
	<-chann
	fmt.Println("Stopped.")
}
