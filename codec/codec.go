package codec



type Payloader interface {
	Payload(mtu uint16, payload []byte) [][]byte
}