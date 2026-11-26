package client

import (
	"encoding/binary"
	"errors"
	"net"
	"strings"
	"syscall"
	"time"
)

// The box announces itself as box.local. Desktops resolve that through avahi
// or Bonjour, but a container (or a bare server) has neither, so Resolve asks
// the network directly, the way responders expect: from the mDNS port, in the
// multicast group, so the answer comes back the same way and through
// firewalls that already let mDNS in. The port is shared with any avahi.

var mdnsGroup = &net.UDPAddr{IP: net.IPv4(224, 0, 0, 251), Port: 5353}

const (
	typeA   = 1
	classIN = 1
)

// Query is an mDNS question for the IPv4 address of name.
func Query(name string) []byte {
	q := make([]byte, 12, 64)
	binary.BigEndian.PutUint16(q[4:], 1) // one question
	for _, label := range strings.Split(strings.TrimSuffix(name, "."), ".") {
		q = append(q, byte(len(label)))
		q = append(q, label...)
	}
	q = append(q, 0, 0, typeA, 0, classIN)
	return q
}

// skipName returns the offset just past a (possibly compressed) name.
func skipName(data []byte, off int) (int, error) {
	for {
		if off >= len(data) {
			return 0, errors.New("truncated")
		}
		n := int(data[off])
		switch {
		case n == 0:
			return off + 1, nil
		case n&0xC0 == 0xC0:
			return off + 2, nil
		}
		off += 1 + n
	}
}

func readName(data []byte, off int) string {
	var labels []string
	for range 64 { // bounded: a pointer loop must not hang us
		if off >= len(data) || data[off] == 0 {
			break
		}
		n := int(data[off])
		if n&0xC0 == 0xC0 {
			if off+1 >= len(data) {
				break
			}
			off = int(binary.BigEndian.Uint16(data[off:]) & 0x3FFF)
			continue
		}
		if off+1+n > len(data) {
			break
		}
		labels = append(labels, string(data[off+1:off+1+n]))
		off += 1 + n
	}
	return strings.Join(labels, ".")
}

// ParseAddress finds the IPv4 address for name in an mDNS response.
func ParseAddress(data []byte, name string) (string, bool) {
	if len(data) < 12 || data[2]&0x80 == 0 { // too short, or a question
		return "", false
	}
	questions := int(binary.BigEndian.Uint16(data[4:]))
	records := int(binary.BigEndian.Uint16(data[6:])) + int(binary.BigEndian.Uint16(data[8:])) +
		int(binary.BigEndian.Uint16(data[10:]))
	off := 12
	var err error
	for range questions {
		if off, err = skipName(data, off); err != nil {
			return "", false
		}
		off += 4
	}
	want := strings.ToLower(strings.TrimSuffix(name, "."))
	for range records {
		owner := readName(data, off)
		if off, err = skipName(data, off); err != nil || off+10 > len(data) {
			return "", false
		}
		rtype := binary.BigEndian.Uint16(data[off:])
		rclass := binary.BigEndian.Uint16(data[off+2:]) & 0x7FFF
		length := int(binary.BigEndian.Uint16(data[off+8:]))
		off += 10
		if off+length > len(data) {
			return "", false
		}
		if strings.ToLower(owner) == want && rtype == typeA && rclass == classIN && length == 4 {
			return net.IP(data[off : off+4]).String(), true
		}
		off += length
	}
	return "", false
}

// Resolve asks the local network for name's IPv4 address.
func Resolve(name string, wait time.Duration) (string, error) {
	lc := net.ListenConfig{Control: func(_, _ string, c syscall.RawConn) error {
		var serr error
		err := c.Control(func(fd uintptr) {
			serr = syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
			if serr == nil {
				serr = syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, soReusePort, 1)
			}
		})
		if err != nil {
			return err
		}
		return serr
	}}
	pc, err := lc.ListenPacket(nil, "udp4", ":5353")
	if err != nil {
		return "", err
	}
	defer pc.Close()
	conn := pc.(*net.UDPConn)
	if err := joinGroup(conn); err != nil {
		return "", err
	}
	if _, err := conn.WriteToUDP(Query(name), mdnsGroup); err != nil {
		return "", err
	}
	deadline := time.Now().Add(wait)
	buf := make([]byte, 9000)
	for {
		conn.SetReadDeadline(deadline)
		n, _, err := conn.ReadFromUDP(buf)
		if err != nil {
			return "", err // includes the timeout
		}
		if addr, ok := ParseAddress(buf[:n], name); ok {
			return addr, nil
		}
	}
}

func joinGroup(conn *net.UDPConn) error {
	raw, err := conn.SyscallConn()
	if err != nil {
		return err
	}
	var serr error
	err = raw.Control(func(fd uintptr) {
		mreq := &syscall.IPMreq{Multiaddr: [4]byte{224, 0, 0, 251}}
		serr = syscall.SetsockoptIPMreq(int(fd), syscall.IPPROTO_IP, syscall.IP_ADD_MEMBERSHIP, mreq)
		if serr == nil {
			serr = syscall.SetsockoptInt(int(fd), syscall.IPPROTO_IP, syscall.IP_MULTICAST_TTL, 255)
		}
	})
	if err != nil {
		return err
	}
	return serr
}
