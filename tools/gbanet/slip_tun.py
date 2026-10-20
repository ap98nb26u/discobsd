#!/usr/bin/env python3
"""
slip_tun.py - connect the GBA's SLIP link to a Linux TUN interface.

    python3 slip_tun.py [-v] [--tun gba0] [--listen 127.0.0.1:5555]
    python3 slip_tun.py --fake        # answer pings itself (no GBA needed)

IP packets read from the TUN device are SLIP-framed and sent down a TCP
connection; SLIP frames coming back are unframed and written to the TUN.
On the other end of the TCP connection sits com_bridge.ps1 (Windows),
which copies bytes to and from the GBA's serial port (COM3).  This way the
WSL2 kernel, which has no SLIP driver, still gets a real IP interface:
`ping 192.168.7.2`, `telnet 192.168.7.2` and so on work against the GBA.

The TUN device is made once, as root, owned by the user who runs this:
    sudo ip tuntap add dev gba0 mode tun user $USER
    sudo ip addr add 192.168.7.1 peer 192.168.7.2 dev gba0
    sudo ip link set gba0 up

--fake answers ICMP echo requests to the peer address itself, to check
the TUN side without a GBA.  --pty speaks SLIP on a pseudo-terminal
instead of TCP and prints its name, so slipd built for the host
(usr.sbin/slipd: make slipd-host; ./slipd-host -d /dev/pts/N) can be
tested without a GBA.  -v prints a line per packet.
"""
import argparse
import fcntl
import pty
import tty
import os
import random
import select
import socket
import struct
import sys
import time

END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD
TUNSETIFF = 0x400454CA
IFF_TUN, IFF_NO_PI = 0x0001, 0x1000


def open_tun(name):
    fd = os.open("/dev/net/tun", os.O_RDWR)
    fcntl.ioctl(fd, TUNSETIFF, struct.pack("16sH", name.encode(), IFF_TUN | IFF_NO_PI))
    return fd


# Extra END bytes before each packet (SLIP ignores empty frames).  The GBA
# can drop a byte in the gap between waking its reader and the read()
# taking over the line (/dev/sio, gba_sio_uart.c); padding makes that
# byte one of these.  40 covers about 3.5 ms at 115200; 12 was too few
# there (lost bytes at packet starts) though enough at 38400.
LEAD = 40


def slip_encode(pkt, lead=LEAD):
    out = bytearray([END] * lead)
    for b in pkt:
        if b == END:
            out += bytes([ESC, ESC_END])
        elif b == ESC:
            out += bytes([ESC, ESC_ESC])
        else:
            out.append(b)
    out.append(END)
    return bytes(out)


class SlipDecoder:
    def __init__(self):
        self.buf = bytearray()
        self.esc = False

    def feed(self, data):
        """Return the complete packets found in data."""
        pkts = []
        for b in data:
            if self.esc:
                self.buf.append(END if b == ESC_END else ESC if b == ESC_ESC else b)
                self.esc = False
            elif b == ESC:
                self.esc = True
            elif b == END:
                if self.buf:
                    pkts.append(bytes(self.buf))
                self.buf = bytearray()
            else:
                self.buf.append(b)
        return pkts


def csum(data):
    if len(data) & 1:
        data += b"\0"
    s = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    s = (s >> 16) + (s & 0xFFFF)
    s += s >> 16
    return ~s & 0xFFFF


def describe(pkt):
    if len(pkt) < 20 or pkt[0] >> 4 != 4:
        return "non-IPv4 %d bytes" % len(pkt)
    ihl = (pkt[0] & 15) * 4
    proto = pkt[9]
    src = socket.inet_ntoa(pkt[12:16])
    dst = socket.inet_ntoa(pkt[16:20])
    extra = ""
    if proto == 1 and len(pkt) >= ihl + 8:
        t, _, _, ident, seq = struct.unpack("!BBHHH", pkt[ihl:ihl + 8])
        extra = " icmp type %d id %d seq %d" % (t, ident, seq)
    elif proto == 6 and len(pkt) >= ihl + 14:
        sp, dp, sq, ak, off, fl = struct.unpack("!HHIIBB", pkt[ihl:ihl + 14])
        names = "".join(n for bit, n in ((2, "S"), (16, "A"), (1, "F"), (4, "R"), (8, "P")) if fl & bit)
        win, = struct.unpack("!H", pkt[ihl + 14:ihl + 16])
        dlen = len(pkt) - ihl - (off >> 4) * 4
        extra = " tcp %d->%d [%s] seq %d ack %d win %d data %d" % (sp, dp, names, sq, ak, win, dlen)
    elif proto == 17:
        extra = " udp"
    return "%s -> %s proto %d len %d%s" % (src, dst, proto, len(pkt), extra)


def fake_reply(pkt):
    """An ICMP echo reply for an echo request, else None."""
    if len(pkt) < 28 or pkt[0] >> 4 != 4 or pkt[9] != 1:
        return None
    ihl = (pkt[0] & 15) * 4
    if pkt[ihl] != 8:
        return None
    ip = bytearray(pkt[:ihl])
    ip[12:16], ip[16:20] = pkt[16:20], pkt[12:16]
    ip[8] = 64
    ip[10:12] = b"\0\0"
    ip[10:12] = struct.pack("!H", csum(bytes(ip)))
    icmp = bytearray(pkt[ihl:])
    icmp[0] = 0
    icmp[2:4] = b"\0\0"
    icmp[2:4] = struct.pack("!H", csum(bytes(icmp)))
    return bytes(ip + icmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--tun", default="gba0")
    ap.add_argument("--listen", default="127.0.0.1:5555")
    ap.add_argument("--fake", action="store_true", help="answer pings without a GBA")
    ap.add_argument("-v", action="store_true", help="print a line per packet")
    ap.add_argument("-x", action="store_true", help="also print each packet's bytes")
    ap.add_argument("--lead", type=int, default=LEAD, help="SLIP END bytes before each packet")
    ap.add_argument("--pty", action="store_true", help="SLIP on a pty (host slipd test)")
    ap.add_argument("--drop", type=float, default=0, help="drop this %% of packets each way (testing)")
    a = ap.parse_args()

    tun = open_tun(a.tun)
    t0 = time.time()
    log = (lambda *m: print("%8.3f" % (time.time() - t0), *m, flush=True)) if a.v else (lambda *m: None)

    if a.fake:
        print("fake mode on %s: answering pings" % a.tun, flush=True)
        while True:
            pkt = os.read(tun, 2048)
            log("tun>", describe(pkt))
            r = fake_reply(pkt)
            if r:
                log("<tun", describe(r))
                os.write(tun, r)

    if a.pty:
        master, slave = pty.openpty()
        tty.setraw(slave)
        print("tun %s ready; SLIP on %s" % (a.tun, os.ttyname(slave)), flush=True)
        dec = SlipDecoder()
        while True:
            r, _, _ = select.select([tun, master], [], [])
            if tun in r:
                pkt = os.read(tun, 2048)
                if pkt and pkt[0] >> 4 == 4:
                    if random.random() * 100 < a.drop:
                        log("-> gba DROPPED", describe(pkt))
                        continue
                    log("-> gba", describe(pkt))
                    os.write(master, slip_encode(pkt, a.lead))
            if master in r:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    data = b""
                if not data:
                    continue
                for pkt in dec.feed(data):
                    if random.random() * 100 < a.drop:
                        log("<- gba DROPPED", describe(pkt))
                        continue
                    log("<- gba", describe(pkt))
                    os.write(tun, pkt)

    host, port = a.listen.rsplit(":", 1)
    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind((host, int(port)))
    ls.listen(1)
    print("tun %s ready; waiting for com_bridge on %s" % (a.tun, a.listen), flush=True)
    while True:
        conn, peer = ls.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("bridge connected from %s:%d" % peer, flush=True)
        dec = SlipDecoder()
        try:
            while True:
                r, _, _ = select.select([tun, conn], [], [])
                if tun in r:
                    pkt = os.read(tun, 2048)
                    if not pkt or pkt[0] >> 4 != 4:
                        continue        # IPv4 only: spare the slow link
                    log("-> gba", describe(pkt))
                    if a.x:
                        print("   ", pkt.hex(" "), flush=True)
                    conn.sendall(slip_encode(pkt, a.lead))
                if conn in r:
                    data = conn.recv(4096)
                    if not data:
                        break
                    for pkt in dec.feed(data):
                        log("<- gba", describe(pkt))
                        if a.x:
                            print("   ", pkt.hex(" "), flush=True)
                        try:
                            os.write(tun, pkt)
                        except OSError as e:     # a garbled frame: drop it
                            log("<- gba rejected:", e)
        except (ConnectionError, OSError) as e:
            print("bridge error:", e, flush=True)
        conn.close()
        print("bridge disconnected", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
