package main

import (
	"fmt"
	"log"
	"net"
)

func main() {
	// listen to incoming udp packets
	pc, err := net.ListenPacket("udp", ":32521")
	if err != nil {
		log.Fatal(err)
	}
	defer pc.Close()

	fmt.Printf("start serving\n")
	for {
		buf := make([]byte, 1024*1024*10)
		n, addr, err := pc.ReadFrom(buf)
		if err != nil {
			fmt.Printf("error serving %s\n", err.Error())
		}

		fmt.Printf("udp packet size %d from %s\n", n, addr.String())
		_, err = pc.WriteTo([]byte{6, 0}, addr)
		if err != nil {
			panic(err)
		}
	}

}
