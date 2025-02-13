package main

/*
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

void*
create_file(char* path, int size, void** pHandle)
{
	HANDLE hMapFile = CreateFileMappingA(
					INVALID_HANDLE_VALUE,    // use paging file
					NULL,                    // default security
					PAGE_READWRITE,          // read/write access
					0,                       // maximum object size (high-order DWORD)
					size,                // maximum object size (low-order DWORD)
					path);                 // name of mapping object

	if (hMapFile == NULL)
		return NULL;

	*pHandle = hMapFile;
	return MapViewOfFile(hMapFile,   // handle to map object
							FILE_MAP_ALL_ACCESS, // read/write permission
							0, 0, size);
}

void
close_file(void* ptr) {
	CloseHandle((HANDLE)ptr);
}

*/
import "C"
import (
	"fmt"
	"os/exec"
	"syscall"
	"unsafe"

	"github.com/google/uuid"
)

func main() {
	id := uuid.NewString()
	var handle unsafe.Pointer
	buffer := C.create_file(C.CString(id), 5*1025*1024, &handle)
	if buffer == nil {
		panic("invalid buffer")
	}
	defer C.close_file(handle)

	cmd := exec.Command("C:/ideacrawler/thinkmay3/assets/shmsunshine.exe", "asdfa", id)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		panic(err)
	}
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	err = cmd.Start()
	if err != nil {
		panic(err)
	}

	go func() {
		buff := make([]byte, 5*1024*1024)
		for {
			n, err := stdout.Read(buff)
			if err != nil {
				panic(err)
			}

			fmt.Print(string(buff[:n]))
		}
	}()

	cmd.Wait()
}
