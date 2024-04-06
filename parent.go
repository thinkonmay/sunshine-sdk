package main

/*
#include "smemory.h"
#include <string.h>
*/
import "C"
import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strings"
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

func copyAndCapture(r io.Reader) {
	buf := make([]byte, 1024)
	for {
		n, err := r.Read(buf[:])
		if err != nil {
			return
		}

		if n < 1 {
			continue
		}

		lines := strings.Split(string(buf[:n]), "\n")
		for _, line := range lines {
			sublines := strings.Split(line, "\r")
			for _, subline := range sublines {
				if len(subline) == 0 {
					continue
				}

				fmt.Printf("sunshine: %s\n", subline)
			}
		}
	}
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
	memory.queues[C.Video0].metadata.active = C.int(1)
	memory.queues[C.Video0].metadata.codec = C.int(1)

	memory.queues[C.Video1].metadata.active = C.int(1)
	memory.queues[C.Video1].metadata.codec = C.int(0)

	memory.queues[C.Audio].metadata.active = C.int(1)
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

	for queue_type, i := range indexes {
		go func(queue *C.Queue, index *int) {
			buffer := make([]byte, int(C.PACKET_SIZE))
			payloader := payloaders[queue.metadata.codec]()

			for {
				if queue.metadata.running != 1 {
					continue
				}

				for int(queue.index) > *index {
					new_index := *index + 1
					real_index := new_index % C.QUEUE_SIZE
					block := queue.array[real_index]

					C.memcpy(unsafe.Pointer(&buffer[0]), unsafe.Pointer(&block.data[0]), C.ulonglong(block.size))
					payloads := payloader.Payload(1200, buffer[:block.size])
					fmt.Printf("Queue type %d, downstream index %d, upstream index %d, receive size %d\n", queue_type, new_index, queue.index, len(payloads))

					*index = new_index
				}

				time.Sleep(time.Microsecond * 100)
			}
		}(&memory.queues[queue_type], i)
	}

	fmt.Printf("execute sunshine with command : ./sunshine.exe \"%s\"\n", byteSliceToString(buffer))
	chann := make(chan os.Signal, 16)
	signal.Notify(chann, syscall.SIGTERM, os.Interrupt)
	<-chann
	fmt.Println("Stopped.")
}
