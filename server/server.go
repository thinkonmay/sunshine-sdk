package main

import (
	"fmt"
	"log"
	"net"
)

func main() {
	// listen to incoming udp packets
	pc, err := net.ListenPacket("udp", ":63400")
	if err != nil {
		log.Fatal(err)
	}
	defer pc.Close()

	fmt.Printf("start serving\n")
	for {
		buf := make([]byte, 1024 * 1024 * 10)
		n, addr, err := pc.ReadFrom(buf)
		if err != nil {
			fmt.Printf("error serving %s\n",err.Error())
		}
		go serve(pc, addr, buf[:n])
	}

}

func serve(pc net.PacketConn, addr net.Addr, buf []byte) {
	fmt.Printf("udp packet size %d from %s\n",len(buf),addr.String())
}
