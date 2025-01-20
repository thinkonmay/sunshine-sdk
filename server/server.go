package main

/*
#include <string.h>

typedef struct {
    int active;
    int codec;

    int env_width, env_height;
    int width, height;
    // Offset x and y coordinates of the client
    float client_offsetX, client_offsetY;
    float offsetX, offsetY;

    float scalar_inv;
}QueueMetadata;
*/
import "C"
import (
	"fmt"
	"log"
	"net"
	"os"
	"time"
	"unsafe"
)

func main() {
	// listen to incoming udp packets
	pc, err := net.ListenPacket("udp", ":32521")
	if err != nil {
		log.Fatal(err)
	}
	defer pc.Close()

	go func() {
		var metadata C.QueueMetadata
		for {
			data,err := os.ReadFile("C:\\ideacrawler\\binary\\metadata.bin")
			if err != nil {
				panic(err)
			}

			from := unsafe.Pointer(&data[0])
			to := unsafe.Pointer(&metadata)
			C.memcpy(to,from,C.ulonglong(len(data)))

			fmt.Printf("%v\n",metadata)
			time.Sleep(time.Second)
		}
	}()

	fmt.Printf("start serving\n")
	buf := make([]byte, 1024*1024*10)
	for {
		_, addr, err := pc.ReadFrom(buf)
		if err != nil {
			fmt.Printf("error serving %s\n", err.Error())
		}

		_, err = pc.WriteTo([]byte{6, 0}, addr)
		if err != nil {
			panic(err)
		}
	}
}
