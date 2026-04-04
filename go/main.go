package main

/*
#cgo CXXFLAGS: -std=c++20 -IC:/scoop/apps/msys2/current/mingw64/include
#cgo LDFLAGS: -lwindowsapp -lwtsapi32 -ld3d11 -ldxgi
#include "wgc.h"
extern void goFrameCallback(int width, int height, void* frame_data, void* user_data);
*/
import "C"

import (
	"fmt"
	"os"
	"os/signal"
	"runtime"
	"runtime/cgo"
	"syscall"
	"unsafe"
)

func init() {
	runtime.LockOSThread()
}

type Frame struct {
	Width  int
	Height int
}

type Capturer struct {
	OnFrame   <-chan Frame
	frameChan chan Frame
	handle    cgo.Handle
}

func NewCapturer() *Capturer {
	c := &Capturer{
		frameChan: make(chan Frame, 10),
	}
	c.OnFrame = c.frameChan
	return c
}

func (c *Capturer) Start() error {
	c.handle = cgo.NewHandle(c)
	ret := C.start_capture((C.frame_callback_t)(unsafe.Pointer(C.goFrameCallback)), unsafe.Pointer(c.handle))
	if ret != 0 {
		c.handle.Delete()
		return fmt.Errorf("failed to start capture: %d", int(ret))
	}
	return nil
}

func (c *Capturer) Stop() {
	C.stop_capture()
	if c.handle != 0 {
		c.handle.Delete()
		c.handle = 0
	}
}

func (c *Capturer) handleFrame(width, height int, data unsafe.Pointer) {
	select {
	case c.frameChan <- Frame{Width: width, Height: height}:
	default:
		// Drop frame if channel is full
	}
}

//export goFrameCallback
func goFrameCallback(width C.int, height C.int, frame_data unsafe.Pointer, user_data unsafe.Pointer) {
	handle := cgo.Handle(user_data)
	capturer := handle.Value().(*Capturer)
	capturer.handleFrame(int(width), int(height), frame_data)
}

func main() {
	fmt.Println("Starting WGC Screen Capture in Go...")

	capturer := NewCapturer()
	if err := capturer.Start(); err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	defer capturer.Stop()

	fmt.Println("Capture started. Press Ctrl+C to stop.")

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	count := 0
	for {
		select {
		case frame := <-capturer.OnFrame:
			count++
			if count%60 == 0 {
				fmt.Printf("Received 60 frames... Latest Resolution: %dx%d\n", frame.Width, frame.Height)
			}
		case <-sigChan:
			fmt.Println("\nStopping capture...")
			return
		}
	}
}
