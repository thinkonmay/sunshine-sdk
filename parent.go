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

enum QueueType {
    Video,
    Audio,
    Microphone,
    Max
};

typedef struct _Queue{
    Packet array[QUEUE_SIZE];
    int order[QUEUE_SIZE];
}Queue;

typedef struct {
    Queue queues[Max];
    Event events[EVENT_TYPE_MAX];
}SharedMemory;




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

const (
	audio = 1
	video = 2
)

type DataType int

func peek(memory *C.SharedMemory, media DataType) bool {
	if media == video {
		return memory.queues[C.Video].order[0] != -1
	} else if media == audio {
		return memory.queues[C.Audio].order[0] != -1
	}

	panic(fmt.Errorf("unknown data type"))
}

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
	lock, err := mod.FindProc("lock_shared_memory")
	if err != nil {
		panic(err)
	}
	unlock, err := mod.FindProc("unlock_shared_memory")
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
	handle_video := func() {
		lock.Call(pointer)
		defer unlock.Call(pointer)

		block := memory.queues[C.Video].array[memory.queues[C.Video].order[0]]
		fmt.Printf("video buffer %d\n", block.size)

		for i := 0; i < C.QUEUE_SIZE-1; i++ {
			memory.queues[C.Video].order[i] = memory.queues[C.Video].order[i+1]
		}

		memory.queues[C.Video].order[C.QUEUE_SIZE-1] = -1
	}

	handle_audio := func() {
		lock.Call(pointer)
		defer unlock.Call(pointer)

		block := memory.queues[C.Audio].array[memory.queues[C.Audio].order[0]]
		fmt.Printf("audio buffer %d\n", block.size)

		for i := 0; i < C.QUEUE_SIZE-1; i++ {
			memory.queues[C.Audio].order[i] = memory.queues[C.Audio].order[i+1]
		}

		memory.queues[C.Audio].order[C.QUEUE_SIZE-1] = -1
	}

	go func() {
		for {
			for peek(memory, video) {
				handle_video()
			}
			for peek(memory, audio) {
				handle_audio()
			}

			time.Sleep(time.Millisecond)
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
