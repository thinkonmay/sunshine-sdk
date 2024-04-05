package main

/*
#include "smemory.h"
*/
import "C"
import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"strings"
	"syscall"
	"time"
	"unsafe"

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
	go func() {
		for {
			memory.queues[C.Video0].metadata.active = C.int(0)
			memory.queues[C.Video1].metadata.active = C.int(0)
			memory.queues[C.Audio].metadata.active = C.int(0)

			time.Sleep(time.Second * 10)

			memory.queues[C.Video0].metadata.active = C.int(1)
			memory.queues[C.Video1].metadata.active = C.int(1)
			memory.queues[C.Audio].metadata.active = C.int(1)

			time.Sleep(time.Second * 10)
		}
	}()
	go func() {
		indexes := make([]int,C.QueueMax)
		for i,_  := range indexes {
			indexes[i] = int(memory.queues[i].index) 
		}

		for {
			for queue_type,_  := range indexes {
				if memory.queues[queue_type].metadata.running != 1 {
					continue
				}

				for int(memory.queues[queue_type].index) > indexes[queue_type] {
					new_index := indexes[queue_type] + 1
					real_index := new_index % C.QUEUE_SIZE
					block := memory.queues[queue_type].array[real_index]
					fmt.Printf("Queue type %d, downstream index %d, upstream index %d, receive size %d\n",queue_type,new_index, memory.queues[queue_type].index,block.size)
					indexes[queue_type] = new_index;
				}
			}
			time.Sleep(time.Microsecond * 100)
		}
	}()

	cmd := exec.Command("E:\\thinkmay\\worker\\sunshine\\build\\sunshine.exe",
		byteSliceToString(buffer),
	)

	stdoutIn, _ := cmd.StdoutPipe()
	stderrIn, _ := cmd.StderrPipe()
	cmd.Start()
	go copyAndCapture(stderrIn)
	go copyAndCapture(stdoutIn)

	chann := make(chan os.Signal, 16)
	signal.Notify(chann, syscall.SIGTERM, os.Interrupt)
	<-chann
	cmd.Process.Kill()
	fmt.Println("Stopped.")
}
