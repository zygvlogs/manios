#!/usr/bin/env python3
"""Network test: the NE2000 driver, the IPv4 stack and ZRP, on the wire.

QEMU's socket netdev carries the guest NIC's Ethernet frames over TCP,
so this script is the other end of the cable. It speaks ARP, IPv4, ICMP
and UDP itself, and has its own ZRP server and client -- a second,
independent implementation of docs/zrp.md -- to test the guest's client
and server against. It also feeds the guest malformed packets and drops
some requests to force retransmission. Finally it connects two ManiOS
machines to each other: one exports a FAT volume, the other mounts it.

Usage: tests/net_test.py [path-to-kernel-elf]
"""
import os
import queue
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

import console_test
from console_test import (PROMPT, SHELL_PROMPT, Machine, TestFailure, fnv1a, run_command,
                          write_fat_disk)

HOST_MAC = bytes.fromhex("525400aabb01")
GUEST_MAC = bytes.fromhex("525400123456")
HOST_IP, GUEST_IP = "10.0.0.1", "10.0.0.2"
ZRP_PORT = 5640
BROADCAST = b"\xff" * 6

# The network cards the tests run over: QEMU's device, the interface
# ManiOS makes of it, and what its driver says at boot.
MAC = "52:54:00:12:34:56"
NICS = {
    "ne2k": (f"ne2k_isa,netdev=n0,iobase=0x300,irq=9,mac={MAC}", "ne0",
             f"ne0: NE2000 at 0x300 irq 9, {MAC}"),
    "pcnet": (f"pcnet,netdev=n0,mac={MAC}", "pcn0", "pcn0: AMD PCnet at pci "),
    "e1000": (f"e1000,netdev=n0,mac={MAC}", "em0", "em0: Intel 8254x (100e) at pci "),
}

ENOENT, EBADF, EINVAL, EROFS, ENOSYS, EPROTO = 2, 9, 22, 30, 38, 71
ZKT_DIR, ZKT_FILE = 0, 1
(TVERSION, RVERSION, TATTACH, RATTACH, TWALK, RWALK, TREAD, RREAD, TWRITE, RWRITE,
 TCLUNK, RCLUNK, TSTAT, RSTAT, TAUTH, RAUTH) = range(1, 17)
RERROR, RPENDING = 64, 65
VERSION = "ZRP2"


def ip_bytes(ip):
    return socket.inet_aton(ip)


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    s = sum(struct.unpack(f">{len(data) // 2}H", data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def eth(dst, src, ethertype, payload):
    return dst + src + struct.pack(">H", ethertype) + payload


def ipv4(src, dst, proto, payload, ident=1, flags_frag=0x4000, ttl=64, bad_checksum=False,
         total=None):
    total = total if total is not None else 20 + len(payload)
    h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, total, ident, flags_frag, ttl, proto, 0,
                    ip_bytes(src), ip_bytes(dst))
    c = checksum(h) ^ (0x5555 if bad_checksum else 0)
    return h[:10] + struct.pack(">H", c) + h[12:] + payload


def udp(src, dst, sport, dport, data, bad_checksum=False):
    u = struct.pack(">HHHH", sport, dport, 8 + len(data), 0) + data
    pseudo = ip_bytes(src) + ip_bytes(dst) + struct.pack(">BBH", 0, 17, len(u))
    c = checksum(pseudo + u) or 0xFFFF
    if bad_checksum:
        c ^= 0x00FF
    return u[:6] + struct.pack(">H", c) + u[8:]


def icmp_echo(ident, seq, data, kind=8, bad_checksum=False):
    m = struct.pack(">BBHHH", kind, 0, 0, ident, seq) + data
    c = checksum(m) ^ (0x1111 if bad_checksum else 0)
    return m[:2] + struct.pack(">H", c) + m[4:]


def parse_ipv4(frame):
    """(src, dst, proto, payload) of a well-formed IPv4 frame, checking
    the guest's header checksum; None for anything else."""
    if len(frame) < 34 or frame[12:14] != b"\x08\x00":
        return None
    h = frame[14:]
    ihl = (h[0] & 15) * 4
    total = struct.unpack(">H", h[2:4])[0]
    if checksum(h[:ihl]) != 0:
        raise TestFailure("the guest sent an IPv4 header with a bad checksum")
    return socket.inet_ntoa(h[12:16]), socket.inet_ntoa(h[16:20]), h[9], h[ihl:total]


class Peer:
    """The host end of the guest's cable. A reader thread answers ARP for
    HOST_IP, hands UDP for registered ports to their handlers, and queues
    every other frame for the tests."""

    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.conn = None
        self.frames = queue.Queue()
        self.udp_handlers = {}
        self.lock = threading.Lock()
        self.alive = True
        self.wire_errors = []  # what the guest got wrong on the wire

    def error_note(self):
        return f" (the guest sent: {self.wire_errors[0]})" if self.wire_errors else ""

    def netdev(self, nic="ne2k"):
        return ["-netdev", f"socket,id=n0,connect=127.0.0.1:{self.port}", "-device", NICS[nic][0]]

    def start(self):
        self.listener.settimeout(15)
        self.conn, _ = self.listener.accept()
        threading.Thread(target=self._reader, daemon=True).start()

    def send(self, frame):
        with self.lock:
            self.conn.sendall(struct.pack(">I", len(frame)) + frame)

    def send_ip(self, proto, payload, **kw):
        self.send(eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, proto, payload, **kw)))

    def send_udp(self, sport, dport, data, bad_checksum=False):
        self.send_ip(17, udp(HOST_IP, GUEST_IP, sport, dport, data, bad_checksum))

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.conn.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    def _reader(self):
        try:
            while self.alive:
                n = struct.unpack(">I", self._recv_exact(4))[0]
                try:
                    self._dispatch(self._recv_exact(n))
                except TestFailure as e:  # reported by the next expect()
                    self.wire_errors.append(str(e))
                    self.frames.put(("error", str(e)))
        except (EOFError, OSError):
            pass

    def _dispatch(self, frame):
        if frame[12:14] == b"\x08\x06" and len(frame) >= 42:
            op, target = struct.unpack(">H", frame[20:22])[0], socket.inet_ntoa(frame[38:42])
            if op == 1 and target == HOST_IP:  # who-has HOST_IP: we do
                reply = (struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2) + HOST_MAC + ip_bytes(HOST_IP)
                         + frame[22:28] + frame[28:32])
                self.send(eth(frame[6:12], HOST_MAC, 0x0806, reply))
                return
        pkt = parse_ipv4(frame)
        if pkt and pkt[2] == 1 and pkt[1] == HOST_IP and pkt[3][:1] == b"\x08":  # a ping to us
            if checksum(pkt[3]) != 0:
                raise TestFailure("the guest sent an ICMP message with a bad checksum")
            ident, seq = struct.unpack(">HH", pkt[3][4:8])
            reply = icmp_echo(ident, seq, pkt[3][8:], kind=0)
            self.send(eth(frame[6:12], HOST_MAC, 0x0800, ipv4(HOST_IP, pkt[0], 1, reply)))
            return
        if pkt and pkt[2] == 17 and len(pkt[3]) >= 8:
            sport, dport, ulen, csum = struct.unpack(">HHHH", pkt[3][:8])
            if csum:
                pseudo = ip_bytes(pkt[0]) + ip_bytes(pkt[1]) + struct.pack(">BBH", 0, 17, ulen)
                if checksum(pseudo + pkt[3][:ulen]) != 0:
                    self.wire_errors.append("a UDP datagram with a bad checksum")
                    self.frames.put(("bad-udp-checksum", frame))
                    return
            handler = self.udp_handlers.get(dport)
            if handler:
                handler(pkt[0], sport, pkt[3][8:ulen])
                return
        self.frames.put(("frame", frame))

    def expect(self, match, timeout=3):
        """The first queued frame for which match(frame) is truthy."""
        deadline = time.time() + timeout
        while True:
            left = deadline - time.time()
            if left <= 0:
                return None
            try:
                kind, frame = self.frames.get(timeout=left)
            except queue.Empty:
                return None
            if kind == "bad-udp-checksum":
                raise TestFailure("the guest sent a UDP datagram with a bad checksum")
            if kind == "error":
                raise TestFailure(frame)
            result = match(frame)
            if result:
                return result

    def close(self):
        self.alive = False
        for s in (self.conn, self.listener):
            if s:
                s.close()


# --- ZRP, the host's own implementation ------------------------------------

def zstr(s):
    b = s.encode()
    return struct.pack("<H", len(b)) + b


def zmsg(mtype, tag, body=b""):
    return struct.pack("<IBH", 7 + len(body), mtype, tag) + body


def zstat(kind, length, name):
    return struct.pack("<BI", kind, length) + zstr(name)


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise TestFailure(f"truncated ZRP message: {self.d!r}")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def str(self):
        return self.take(self.u16()).decode()

    def stat(self):
        return self.u8(), self.u32(), self.str()


class HostZrpServer:
    """Serves an in-memory tree: {name: bytes or dict}. To imitate a lossy
    network it can drop every Nth request (the guest must retransmit),
    drop every Nth reply (the retransmission must be answered from the
    reply cache), and send every Nth reply twice (the guest must ignore
    the stale copy while it waits for its next reply)."""

    def __init__(self, peer, tree, drop_every=0, drop_reply_every=0, duplicate_reply_every=0):
        self.peer, self.tree, self.drop_every = peer, tree, drop_every
        self.drop_reply_every, self.duplicate_reply_every = drop_reply_every, duplicate_reply_every
        self.replies = self.replies_dropped = self.replies_duplicated = 0
        self.sessions = {}
        self.requests = self.dropped = self.duplicates = self.resent = 0
        self.dropped_requests = set()
        peer.udp_handlers[ZRP_PORT] = self.handle

    def lookup(self, path):
        node = self.tree
        for name in path:
            node = node[name]
        return node

    def stat(self, path):
        node = self.lookup(path)
        name = path[-1] if path else "/"
        if path == ["bad", "type"]:
            return struct.pack("<BI", 9, 0) + zstr(name)  # no such file type
        return zstat(ZKT_DIR, 0, name) if isinstance(node, dict) else zstat(ZKT_FILE, len(node), name)

    def handle(self, ip, port, data):
        key = (ip, port)
        s = self.sessions.get(key)
        if s and s["last"][0] == data:
            self.duplicates += 1
            self.peer.send_udp(ZRP_PORT, port, s["last"][1])
            return
        if data in self.dropped_requests:  # the retransmission of one we dropped
            self.dropped_requests.discard(data)
            self.resent += 1
        self.requests += 1
        if self.drop_every and self.requests % self.drop_every == 0:
            self.dropped += 1
            self.dropped_requests.add(data)
            return
        r = Reader(data)
        size, mtype, tag = r.u32(), r.u8(), r.u16()
        try:
            if mtype == TVERSION:
                msize, version = r.u32(), r.str()
                s = self.sessions[key] = {"fids": {}, "last": (None, None)}
                reply = zmsg(RVERSION, tag, struct.pack("<I", min(msize, 1472)) + zstr(version))
            elif s is None:
                raise OSError(EPROTO)
            elif mtype == TAUTH:
                raise OSError(ENOSYS)  # this server has no key
            elif mtype == TATTACH:
                fid, aname = r.u32(), r.str()
                s["fids"][fid] = []
                reply = zmsg(RATTACH, tag, self.stat([]))
            elif mtype == TWALK:
                fid, newfid, name = r.u32(), r.u32(), r.str()
                path = s["fids"][fid] + ([name] if name else [])
                try:
                    self.lookup(path)
                except (KeyError, TypeError):
                    raise OSError(ENOENT)
                s["fids"][newfid] = path
                reply = zmsg(RWALK, tag, self.stat(path))
            elif mtype == TREAD:
                fid, offset, count = r.u32(), r.u32(), r.u32()
                node = self.lookup(s["fids"][fid])
                if isinstance(node, dict):
                    out = b""
                    for name in sorted(node)[offset:]:
                        rec = self.stat(s["fids"][fid] + [name])
                        if len(out) + len(rec) > count:
                            break
                        out += rec
                elif s["fids"][fid] == ["bad", "count"]:
                    reply = zmsg(RREAD, tag, struct.pack("<I", 5000) + b"short")  # lies
                    out = None
                else:
                    out = node[offset:offset + min(count, 1472 - 11)]
                if out is not None:
                    reply = zmsg(RREAD, tag, struct.pack("<I", len(out)) + out)
            elif mtype == TCLUNK:
                del s["fids"][r.u32()]
                reply = zmsg(RCLUNK, tag)
            else:
                raise OSError(EPROTO)
        except OSError as e:
            reply = zmsg(RERROR, tag, struct.pack("<H", e.args[0]))
        except KeyError:
            reply = zmsg(RERROR, tag, struct.pack("<H", EBADF))
        if s is not None:
            s["last"] = (data, reply)
        self.replies += 1
        if self.drop_reply_every and self.replies % self.drop_reply_every == 0:
            self.replies_dropped += 1
            return
        self.peer.send_udp(ZRP_PORT, port, reply)
        if self.duplicate_reply_every and self.replies % self.duplicate_reply_every == 0:
            self.replies_duplicated += 1
            self.peer.send_udp(ZRP_PORT, port, reply)


class HostZrpClient:
    """Talks to the guest's server from host UDP port `port`."""

    def __init__(self, peer, port):
        self.peer, self.port, self.tag = peer, port, 0
        self.replies = queue.Queue()
        peer.udp_handlers[port] = lambda ip, sport, data: self.replies.put(data)

    def raw(self, data, timeout=2):
        self.peer.send_udp(self.port, ZRP_PORT, data)
        try:
            return self.replies.get(timeout=timeout)
        except queue.Empty:
            return None

    def rpc(self, mtype, body=b"", tag=None):
        if tag is None:
            self.tag = (self.tag + 1) % 0xFFFF
            tag = self.tag
        reply = self.raw(zmsg(mtype, tag, body))
        if reply is None:
            raise TestFailure(f"no reply to ZRP type {mtype}" + self.peer.error_note())
        r = Reader(reply)
        size, rtype, rtag = r.u32(), r.u8(), r.u16()
        if size != len(reply) or rtag != tag:
            raise TestFailure(f"bad ZRP reply header: {reply!r}")
        return rtype, r

    def expect_error(self, mtype, body, errno, what):
        rtype, r = self.rpc(mtype, body)
        if rtype != RERROR or r.u16() != errno:
            raise TestFailure(f"{what}: expected Rerror {errno}, got type {rtype}")


# --- the tests ---------------------------------------------------------------

def check(ok, what):
    if not ok:
        raise TestFailure(what)


def test_link(m, peer):
    arp = (struct.pack(">HHBBH", 1, 0x0800, 6, 4, 1) + HOST_MAC + ip_bytes(HOST_IP)
           + b"\0" * 6 + ip_bytes(GUEST_IP))
    peer.send(eth(BROADCAST, HOST_MAC, 0x0806, arp))
    got = peer.expect(lambda f: f[12:14] == b"\x08\x06" and f[20:22] == b"\x00\x02" and f)
    check(got and got[22:28] == GUEST_MAC and got[28:32] == ip_bytes(GUEST_IP)
          and got[0:6] == HOST_MAC, "no ARP reply for the guest's address")

    data = bytes(range(56))
    peer.send_ip(1, icmp_echo(0x1234, 7, data))

    def echo_reply(frame, ident=0x1234, seq=7):
        p = parse_ipv4(frame)
        if p and p[2] == 1 and p[3][0] == 0 and struct.unpack(">HH", p[3][4:8]) == (ident, seq):
            return p
    p = peer.expect(echo_reply)
    check(p and p[3][8:] == data and checksum(p[3]) == 0 and p[1] == HOST_IP,
          "no correct ICMP echo reply")


def test_malformed(m, peer):
    """Garbage must be dropped and counted, and the guest keep working."""
    good = icmp_echo(0x4242, 1, b"x" * 20)
    raw = ipv4(HOST_IP, GUEST_IP, 1, good)
    bad = [
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 1, good, bad_checksum=True)),
        eth(GUEST_MAC, HOST_MAC, 0x0800, raw[:15]),                                  # truncated
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 1, good, total=900)),  # overlong
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 1, good, flags_frag=0x2000)),
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 1, good, flags_frag=0x0010)),
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 1,
                                               icmp_echo(0x4242, 1, b"y", bad_checksum=True))),
        eth(GUEST_MAC, HOST_MAC, 0x0800, b"\x46" + raw[1:]),                         # IHL lies
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, "10.0.0.77", 1, good)),       # not ours
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 17,
                                               udp(HOST_IP, GUEST_IP, 9, ZRP_PORT, b"z" * 9,
                                                   bad_checksum=True))),
        eth(GUEST_MAC, HOST_MAC, 0x0800, ipv4(HOST_IP, GUEST_IP, 17, b"\x00\x09\x16\x08\x00\xff")),
        eth(GUEST_MAC, HOST_MAC, 0x86DD, b"\x60" + b"\0" * 50),                       # IPv6
        eth(GUEST_MAC, HOST_MAC, 0x0806, b"\x00\x01"),                               # runt ARP
        b"\x00" * 10,                                                                # runt frame
        # Too big for one receive buffer: the PCnet spreads these over
        # several descriptors, which must go without stalling the ring.
        eth(GUEST_MAC, HOST_MAC, 0x0800, b"\x45" + b"\0" * 1985),
        eth(GUEST_MAC, HOST_MAC, 0x0800, b"\x45" + b"\0" * 2985),
    ]
    for frame in bad:
        peer.send(frame)
    time.sleep(0.3)
    stray = peer.expect(lambda f: (parse_ipv4(f) or (0, 0, 0))[2] == 1 and f, timeout=0.5)
    check(stray is None, "the guest answered a malformed packet")
    peer.send_ip(1, good)
    check(peer.expect(lambda f: (parse_ipv4(f) or (0, 0, 0))[2] == 1 and f) is not None,
          "the guest stopped answering after malformed packets")
    def pings(ident, count, wave):
        """Sends `count` pings, `wave` at a time; the sequence numbers answered."""
        answered = set()
        for start in range(0, count, wave):
            for seq in range(start, min(start + wave, count)):
                peer.send_ip(1, icmp_echo(ident, seq, bytes([seq % 251]) * (seq % 200 + 1)))
            deadline = time.time() + 3
            while len(answered) < min(start + wave, count) and time.time() < deadline:
                f = peer.expect(lambda f: parse_ipv4(f), timeout=0.5)
                if f and f[2] == 1 and struct.unpack(">H", f[3][4:6])[0] == ident:
                    answered.add(struct.unpack(">H", f[3][6:8])[0])
        return answered

    # 300 pings of every size, a few at a time: the receive ring wraps
    # around many times, and odd lengths exercise the 16-bit transfers.
    got = pings(0x5151, 300, 10)
    check(len(got) == 300, f"only {len(got)} of 300 paced pings answered")
    # A flood the ring can't hold: the card drops what doesn't fit (as a
    # real one does), and the guest must carry on.
    flood = pings(0x6161, 400, 400)
    check(len(flood) > 0, "no answers to the flood")
    check(pings(0x7171, 5, 5) == set(range(5)), "the guest stopped answering after a flood")
    print(f"      (flood: {len(flood)} of 400 answered)")


HOST_TREE = {
    "hello.txt": b"Hello from the host's ZRP server.\n",
    "big.bin": bytes(random.Random(5).randrange(256) for _ in range(40000)),
    "many": {f"file{i:03d}": f"{i}\n".encode() for i in range(60)},
    "dir": {"a": b"in a\n", "sub": {"deep.txt": b"deep\n"}},
    "bad": {"count": b"", "type": b""},  # malformed replies, below
}


def test_guest_client(m, peer):
    srv = HostZrpServer(peer, HOST_TREE, drop_every=7, drop_reply_every=11, duplicate_reply_every=5)
    big = HOST_TREE["big.bin"]
    cases = [
        ("serial", "mount udp!10.0.0.1 /n", ["!mount:"]),
        ("serial", "ls /n", ["bad/", "big.bin", "dir/", "hello.txt", "many/"]),
        ("serial", "cat /n/hello.txt", ["Hello from the host's ZRP server."]),
        ("serial", "sum /n/big.bin", [f"/n/big.bin: {len(big)} bytes, fnv1a {fnv1a(big):08x}"]),
        # 60 entries take several directory reads.
        ("serial", "ls /n/many", ["file000", "file031", "file059"]),
        ("serial", "cd /n/dir/sub; cat deep.txt ../a; cd /", ["deep\r\nin a\r\n"]),
        ("serial", "cat /n/missing", ["cat: /n/missing: no such file or directory"]),
        # A server's replies are checked like any other input.
        ("serial", "cat /n/bad/count", ["cat: /n/bad/count: protocol error"]),
        ("serial", "cat /n/bad/type", ["cat: /n/bad/type: protocol error"]),
        ("serial", "ls /n/bad", ["ls: /n/bad: protocol error"]),
        ("serial", "mount udp!10.0.0.9 /n", ["mount: udp!10.0.0.9: host unreachable"]),
        ("serial", "mount udp!10.0.0.1!9 /n", ["mount: udp!10.0.0.1!9: timed out"]),
        ("serial", "mount nonsense /n", ["mount: nonsense: invalid argument"]),
        ("serial", "ls /n/dir", ["a", "sub/"]),  # the failed mounts left /n alone
    ]
    for how, command, needles in cases:
        run_command(m, how, command, needles, SHELL_PROMPT)
        print(f"PASS: [guest client] {command!r}")
    # Every dropped request came back byte for byte (same tag), except
    # those to port 9 and 10.0.0.9, which never reach this server.
    check(srv.dropped > 0 and srv.resent == srv.dropped,
          f"{srv.dropped} requests dropped, but {srv.resent} retransmitted")
    check(srv.replies_dropped > 0 and srv.duplicates >= srv.replies_dropped,
          f"{srv.replies_dropped} replies dropped, but only {srv.duplicates} requests repeated")
    print(f"      (host server: {srv.requests} requests; {srv.dropped} requests and "
          f"{srv.replies_dropped} replies dropped, all recovered by retransmission; "
          f"{srv.replies_duplicated} replies duplicated)")


def test_guest_server(peer, bootfs):
    c = HostZrpClient(peer, 7000)
    rtype, r = c.rpc(TATTACH, struct.pack("<I", 1) + zstr(""))
    check(rtype == RERROR and r.u16() == EPROTO, "a request before Tversion must be EPROTO")
    rtype, r = c.rpc(TVERSION, struct.pack("<I", 8192) + zstr(VERSION), tag=0xFFFF)
    check(rtype == RVERSION and r.u32() == 1472 and r.str() == VERSION, "Tversion")
    c.expect_error(TAUTH, bytes(16), ENOSYS, "Tauth on a server without a key")
    c.expect_error(TATTACH, struct.pack("<I", 1) + zstr("") + b"short", EPROTO,
                   "a Tattach with a malformed proof")
    rtype, r = c.rpc(TATTACH, struct.pack("<I", 1) + zstr(""))
    check(rtype == RATTACH and r.stat()[0] == ZKT_DIR, "Tattach")
    rtype, r = c.rpc(TWALK, struct.pack("<II", 1, 2) + zstr("etc"))
    check(rtype == RWALK and r.stat() == (ZKT_DIR, 0, "etc"), "Twalk to a directory")
    rtype, r = c.rpc(TWALK, struct.pack("<II", 2, 3) + zstr("motd"))
    check(rtype == RWALK and r.stat() == (ZKT_FILE, 19, "motd"), "Twalk to a file")
    rtype, r = c.rpc(TREAD, struct.pack("<III", 3, 0, 100))
    check(rtype == RREAD and r.take(r.u32()) == b"Welcome to ManiOS.\n", "Tread of a file")
    rtype, r = c.rpc(TREAD, struct.pack("<III", 3, 8, 3))
    check(rtype == RREAD and r.take(r.u32()) == b"to ", "Tread at an offset")
    rtype, r = c.rpc(TSTAT, struct.pack("<I", 3))
    check(rtype == RSTAT and r.stat() == (ZKT_FILE, 19, "motd"), "Tstat")

    # A directory read by entry index, a message at a time.
    rtype, r = c.rpc(TWALK, struct.pack("<II", 1, 4) + zstr("bin"))
    names, index = [], 0
    while True:
        rtype, r = c.rpc(TREAD, struct.pack("<III", 4, index, 200))
        end = r.p + r.u32()
        batch = []
        while r.p < end:
            batch.append(r.stat()[2])
        if not batch:
            break
        names += batch
        index += len(batch)
    check(sorted(names) == sorted(os.listdir(os.path.join(bootfs, "bin"))),
          f"the directory listing differs: {names}")

    # A whole program, compared with the build's copy.
    rtype, r = c.rpc(TWALK, struct.pack("<II", 4, 5) + zstr("sh"))
    size = r.stat()[1]
    data = b""
    while len(data) < size:
        rtype, r = c.rpc(TREAD, struct.pack("<III", 5, len(data), 1461))
        chunk = r.take(r.u32())
        check(chunk, "a short read before the end")
        data += chunk
    with open(os.path.join(bootfs, "bin", "sh"), "rb") as f:
        check(data == f.read(), "a file read over ZRP differs from the original")

    c.expect_error(TWALK, struct.pack("<II", 1, 6) + zstr("nope"), ENOENT, "a missing name")
    c.expect_error(TWALK, struct.pack("<II", 1, 6) + zstr(".."), EINVAL, "walking ..")
    c.expect_error(TWALK, struct.pack("<II", 1, 6) + zstr("etc/motd"), EINVAL, "a name with /")
    c.expect_error(TWALK, struct.pack("<II", 1, 2) + zstr("bin"), EINVAL, "a newfid in use")
    c.expect_error(TREAD, struct.pack("<III", 99, 0, 10), EBADF, "an unknown fid")
    c.expect_error(TATTACH, struct.pack("<I", 20) + zstr("other"), ENOENT, "an unknown export")
    c.expect_error(TWRITE, struct.pack("<III", 3, 0, 1) + b"x", EROFS, "writing a read-only file")
    c.expect_error(99, b"", EPROTO, "an unknown message type")
    check(c.raw(b"\x05\x00\x00\x00junk", timeout=0.5) is None, "garbage must get no reply")

    # A repeated request (same tag, same bytes) is answered from the
    # cache: a second walk to the same newfid would otherwise fail.
    walk = zmsg(TWALK, 777, struct.pack("<II", 1, 10) + zstr("etc"))
    first, second = c.raw(walk), c.raw(walk)
    check(first and first == second and first[4] == RWALK, "a retransmitted Twalk was not idempotent")
    rtype, r = c.rpc(TCLUNK, struct.pack("<I", 10))
    check(rtype == RCLUNK, "Tclunk")
    c.expect_error(TCLUNK, struct.pack("<I", 10), EBADF, "clunking a clunked fid")

    # A smaller msize bounds the reads.
    rtype, r = c.rpc(TVERSION, struct.pack("<I", 300) + zstr(VERSION), tag=0xFFFF)
    check(r.u32() == 300, "msize negotiation")
    c.rpc(TATTACH, struct.pack("<I", 1) + zstr(""))
    c.rpc(TWALK, struct.pack("<II", 1, 2) + zstr("bin"))
    c.rpc(TWALK, struct.pack("<II", 2, 3) + zstr("sh"))
    rtype, r = c.rpc(TREAD, struct.pack("<III", 3, 0, 1000))
    check(r.u32() == 300 - 11, "a read larger than msize allows")
    print("PASS: [guest server] protocol checks")


def test_pending(m, peer):
    """A read that waits (for the mouse to move): a repeat of it is
    answered Rpending, other requests are answered meanwhile, and the
    reply comes when the mouse moves."""
    run_command(m, "serial", "export /dev dev", ['exporting /dev as "dev"'])
    c = HostZrpClient(peer, 7001)
    c.rpc(TVERSION, struct.pack("<I", 1472) + zstr(VERSION), tag=0xFFFF)
    rtype, r = c.rpc(TATTACH, struct.pack("<I", 1) + zstr("dev"))
    check(rtype == RATTACH, "attaching a named export")
    rtype, r = c.rpc(TWALK, struct.pack("<II", 1, 2) + zstr("mouse"))
    check(rtype == RWALK, "walking to the mouse")
    read = zmsg(TREAD, 500, struct.pack("<III", 2, 0, 100))
    check(c.raw(read, timeout=0.5) is None, "the mouse was read before it moved")
    pending = c.raw(read, timeout=2)
    check(pending == zmsg(RPENDING, 500), f"a repeat of a waiting read got {pending!r}, not Rpending")
    rtype, r = c.rpc(TSTAT, struct.pack("<I", 1))
    check(rtype == RSTAT, "another request answered while the read waits")
    m.monitor.sendall(b"mouse_move 7 4\n")
    try:
        reply = c.replies.get(timeout=5)
    except queue.Empty:
        raise TestFailure("the waiting read was never answered")
    r = Reader(reply)
    size, rtype, tag = r.u32(), r.u8(), r.u16()
    check(rtype == RREAD and tag == 500 and r.take(r.u32()).startswith(b"m 7 4 "),
          f"the mouse's record: {reply!r}")
    check(c.raw(read) == reply, "a repeat after the reply got something else")


def single_machine(kernel, workdir, nic="ne2k"):
    """The host against one guest, over each network card."""
    peer = Peer()
    _, ifname, found = NICS[nic]
    tag = f"single, {ifname}"
    m = Machine(kernel, [*peer.netdev(nic), "-append", f"ip={GUEST_IP}/24 gw={HOST_IP} export=/boot"])
    failures = 0
    try:
        peer.start()
        boot = m.expect(SHELL_PROMPT)
        for needle in [found, MAC, f"{ifname}: 10.0.0.2/24, gateway 10.0.0.1",
                       "zrp: exporting /boot on udp port 5640", "Milestone M10"]:
            check(needle in boot, f"boot output lacks {needle!r}:\n{boot}")
        check("dhcp" not in boot, "DHCP ran although ip= was given")
        print(f"PASS: [{tag}] boot, the card found")
        for name, test in [("link", lambda: test_link(m, peer)),
                           ("malformed", lambda: test_malformed(m, peer)),
                           ("guest server", lambda: test_guest_server(peer, console_test.BOOTFS_DIR)),
                           ("guest client", lambda: test_guest_client(m, peer))]:
            try:
                test()
                print(f"PASS: [{tag}] {name}")
            except TestFailure as e:
                print(f"FAIL: [{tag}] {name}: {e}")
                failures += 1
        run_command(m, "serial", "exit", ["console: the shell has exited"])
        out = run_command(m, "serial", "net", [ifname, "10.0.0.2"])
        c = re.search(r"ip: (\d+) in, (\d+) bad, (\d+) fragments, (\d+) not ours; "
                      r"udp: (\d+) in, (\d+) bad", out)
        check(c and int(c[2]) >= 4 and int(c[3]) == 2 and int(c[4]) >= 1 and int(c[6]) >= 2,
              f"malformed packets were not counted: {out}")
        run_command(m, "serial", "ping 10.0.0.1 2", ["seq 1: reply in", "seq 2: reply in"])
        print(f"PASS: [{tag}] monitor net counters and ping")
        test_pending(m, peer)
        print(f"PASS: [{tag}] a waiting request: Rpending, and others answered meanwhile")
    except TestFailure as e:
        print(f"FAIL: [{tag}] {e}")
        failures += 1
    finally:
        m.close()
        peer.close()
    return failures


# --- DHCP -------------------------------------------------------------------

DHCP_MAGIC = bytes([99, 130, 83, 99])


def dhcp_options(data):
    opts, i = {}, 0
    while i < len(data) and data[i] != 255:
        if data[i] == 0:
            i += 1
            continue
        if i + 2 > len(data) or i + 2 + data[i + 1] > len(data):
            raise TestFailure(f"a DHCP option runs past the end: {data!r}")
        opts[data[i]] = data[i + 2:i + 2 + data[i + 1]]
        i += 2 + data[i + 1]
    return opts


class DhcpServer:
    """The host's DHCP server, awkward on purpose: it ignores the first
    DISCOVER (the guest must ask again); before its real OFFER it sends
    one for another transaction and one whose last option runs past the
    end (the guest must ignore both); and it refuses the first REQUEST
    (a NAK: the guest must start over)."""
    OFFERED = "10.0.0.77"

    def __init__(self, peer):
        self.peer = peer
        self.discovers, self.requests = [], []
        peer.udp_handlers[67] = self.handle

    def handle(self, src, sport, data):
        if len(data) < 244 or data[0] != 1 or data[236:240] != DHCP_MAGIC:
            self.peer.wire_errors.append(f"not a DHCP request: {data[:16]!r}")
            return
        for ok, what in [(src == "0.0.0.0", f"from {src}, not 0.0.0.0"), (sport == 68, f"from port {sport}"),
                         (data[28:34] == GUEST_MAC, "the wrong client address"),
                         (data[10:12] == b"\x80\x00", "no broadcast flag"),
                         (len(data) >= 300, f"only {len(data)} bytes")]:
            if not ok:
                self.peer.wire_errors.append(f"a DHCP request {what}")
        opts = dhcp_options(data[240:])
        mtype, xid = opts.get(53, b"\0")[0], data[4:8]
        if mtype == 1:  # DISCOVER
            self.discovers.append(xid)
            if len(self.discovers) == 1:
                return
            self.reply(bytes(a ^ 0xFF for a in xid), 2, "10.0.0.99")
            self.reply(xid, 2, "10.0.0.98", truncated=True)
            self.reply(xid, 2, self.OFFERED)
        elif mtype == 3:  # REQUEST
            self.requests.append(opts)
            if opts.get(50) != ip_bytes(self.OFFERED) or opts.get(54) != ip_bytes(HOST_IP):
                self.peer.wire_errors.append(f"a REQUEST for the wrong address or server: {opts}")
            if len(self.requests) == 1:
                self.reply(xid, 6, "0.0.0.0")  # NAK
            else:
                self.reply(xid, 5, self.OFFERED)
        else:
            self.peer.wire_errors.append(f"DHCP message type {mtype}")

    def reply(self, xid, mtype, yiaddr, truncated=False):
        b = bytearray(240)
        b[0:3] = bytes([2, 1, 6])
        b[4:8] = xid
        b[10:12] = b"\x80\x00"
        b[16:20] = ip_bytes(yiaddr)
        b[20:24] = ip_bytes(HOST_IP)
        b[28:34] = GUEST_MAC
        b[236:240] = DHCP_MAGIC
        opts = bytes([53, 1, mtype, 54, 4]) + ip_bytes(HOST_IP)
        if mtype in (2, 5):
            opts += (bytes([1, 4, 255, 255, 255, 0, 3, 4]) + ip_bytes(HOST_IP)
                     + bytes([51, 4]) + struct.pack(">I", 3600))
        opts += bytes([3, 40, 10, 0]) if truncated else b"\xff"
        data = bytes(b) + opts
        self.peer.send(eth(BROADCAST, HOST_MAC, 0x0800,
                           ipv4(HOST_IP, "255.255.255.255", 17,
                                udp(HOST_IP, "255.255.255.255", 67, 68, data))))


def dhcp_host_server(kernel):
    """No ip=: the guest asks the host's awkward server, and uses what it
    is given (over a PCnet, VirtualBox's default card)."""
    peer = Peer()
    server = DhcpServer(peer)
    m = Machine(kernel, peer.netdev("pcnet"))
    try:
        peer.start()
        boot = m.expect(SHELL_PROMPT, timeout=90)
        check("pcn0: 10.0.2.15/24, gateway 10.0.2.2" in boot, "the NAT defaults while asking")
        check("dhcp: pcn0: no answer yet; using 10.0.2.15 meanwhile, and still asking" in boot,
              f"the boot didn't say DHCP was still asking:\n{boot[-600:]}")
        m.expect("dhcp: pcn0: 10.0.0.77/24, gateway 10.0.0.1 (from 10.0.0.1, for 3600 s)", timeout=60)
        check(len(server.discovers) >= 3 and len(server.requests) == 2,
              f"{len(server.discovers)} DISCOVERs and {len(server.requests)} REQUESTs")
        check(not peer.wire_errors, f"on the wire: {peer.wire_errors}")
        run_command(m, "serial", "cat /dev/net", ["pcn0 10.0.0.77/24 gateway 10.0.0.1"], SHELL_PROMPT)
        run_command(m, "serial", "exit", ["console: the shell has exited"])
        run_command(m, "serial", "ping 10.0.0.1 2", ["seq 1: reply in", "seq 2: reply in"])
        print("PASS: [dhcp] the host's server: a lost DISCOVER, stray and malformed OFFERs, a NAK, "
              "then a lease that works")
        return 0
    except TestFailure as e:
        print(f"FAIL: [dhcp] the host's server: {e}{peer.error_note()}")
        return 1
    finally:
        m.close()
        peer.close()


def dhcp_qemu(kernel, nic):
    """QEMU's own DHCP server (its user network, on 192.168.76.0/24 so the
    lease isn't the default), and its gateway answering pings."""
    dev, ifname, _ = NICS[nic]
    m = Machine(kernel, ["-netdev", "user,id=n0,net=192.168.76.0/24", "-device", dev])
    try:
        boot = m.expect(SHELL_PROMPT, timeout=90)
        lease = f"dhcp: {ifname}: 192.168.76.15/24, gateway 192.168.76.2 (from 192.168.76.2, for 86400 s)"
        check(lease in boot, f"no lease from QEMU:\n{boot[-600:]}")
        run_command(m, "serial", "exit", ["console: the shell has exited"])
        run_command(m, "serial", "ping 192.168.76.2 2", ["seq 1: reply in", "seq 2: reply in"])
        print(f"PASS: [dhcp, {ifname}] QEMU's server: a lease, and its gateway answers")
        return 0
    except TestFailure as e:
        print(f"FAIL: [dhcp, {ifname}] QEMU's server: {e}")
        return 1
    finally:
        m.close()


def two_machines(kernel, workdir):
    """A file server and a terminal: ZRP between two ManiOS machines."""
    disk = os.path.join(workdir, "server.img")
    write_fat_disk(disk)
    # FRAG.BIN, a fragmented file on the first volume, as the host reads it.
    frag = subprocess.run(["mtype", "-i", f"{disk}@@{2048 * 512}", "::/FRAG.BIN"],
                          check=True, capture_output=True).stdout
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    server = Machine(kernel, ["-netdev", f"socket,id=n0,listen=127.0.0.1:{port}",
                              "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=9,mac=52:54:00:00:00:01",
                              "-drive", f"file={disk},format=raw,if=ide",
                              "-append", "ip=10.0.0.1/24 export=/n/ata0p1"])
    time.sleep(0.5)  # listening before the other connects
    terminal = Machine(kernel, ["-netdev", f"socket,id=n0,connect=127.0.0.1:{port}",
                                "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=9,mac=52:54:00:00:00:02",
                                "-append", "ip=10.0.0.2/24"])
    failures = 0
    try:
        check("zrp: exporting /n/ata0p1" in server.expect(SHELL_PROMPT), "the server did not export")
        terminal.expect(SHELL_PROMPT)
        cases = [
            ("serial", "mount udp!10.0.0.1 /n", ["!mount:"]),
            ("serial", "ls /n", ["readme.txt", "frag.bin", "bin/"]),
            ("serial", "sum /n/frag.bin", [f"{len(frag)} bytes, fnv1a {fnv1a(frag):08x}"]),
            ("keyboard", "cat /n/docs/nested.txt", ["nested file"]),
            # A program stored on the server's disk runs on the terminal.
            ("serial", "/n/bin/fault null", ["sh: /n/bin/fault: killed (vector 14)"]),
        ]
        for how, command, needles in cases:
            run_command(terminal, how, command, needles, SHELL_PROMPT)
            print(f"PASS: [two machines] {command!r}")
        run_command(terminal, "serial", "exit", ["console: the shell has exited"])
        run_command(terminal, "serial", "run /n/bin/fault exit42",
                    ["run: /n/bin/fault exited with status 42"])
        run_command(terminal, "serial", "ping 10.0.0.1 1", ["seq 1: reply in"])
        print("PASS: [two machines] the monitor shares the shell's mount; ping")
    except TestFailure as e:
        print(f"FAIL: [two machines] {e}")
        failures += 1
    finally:
        terminal.close()
        server.close()
    return failures


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    console_test.BOOTFS_DIR = os.path.join(os.path.dirname(kernel), "bootfs")
    workdir = tempfile.mkdtemp(prefix="zkt-net-")
    try:
        failures = sum(single_machine(kernel, workdir, nic) for nic in NICS)
        failures += two_machines(kernel, workdir)
        failures += dhcp_host_server(kernel)
        failures += sum(dhcp_qemu(kernel, nic) for nic in ("pcnet", "e1000"))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
