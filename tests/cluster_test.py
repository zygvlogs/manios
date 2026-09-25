#!/usr/bin/env python3
"""Cluster test (M13): the three roles of ADR-0003 on one network.

    fs1       10.0.0.1  file server: exports its disk (export=)
    cpu1      10.0.0.2  CPU server: runs cpud (rc=/boot/etc/rc.cpu)
    term1     10.0.0.3  terminal: mounts the file server, runs cpu
    stranger  10.0.0.4  a machine with the wrong key
    the host  10.0.0.9  this script: a ZRP client and servers of its own

All but the stranger share a cluster key. The machines' NE2000s are
connected to a hub in this script (QEMU's socket netdev), which is also
the host: its own implementation of ZRP2's authentication checks the
guests' from outside -- right and wrong keys, no proof, a replayed
proof, and servers that can't prove the key.

Then the roles: the terminal mounts the file server; runs commands on
the CPU server, which see the terminal's console and namespace (and
through it the file server: three machines); a remote shell that waits
longer than UDP's timeout; exit statuses; a program on the CPU server
opening a window on the terminal's desktop; and the CPU server dying
under a job.

Usage: tests/cluster_test.py [path-to-kernel-elf]
"""
import hashlib
import hmac
import os
import queue
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
from console_test import PROMPT, SHELL_PROMPT, Machine, TestFailure, fnv1a, run_command
from desktop_test import ACCENT, DARK, Desktop, read_text, title
from net_test import (EBADF, ENOENT, ENOSYS, EPROTO, RATTACH, RAUTH, RCLUNK, RERROR, RPENDING,
                      RREAD, RVERSION, RWALK, RWRITE, TATTACH, TAUTH, TCLUNK, TREAD, TVERSION,
                      TWALK, TWRITE, VERSION, ZKT_DIR, ZKT_FILE, Reader, checksum, eth, ip_bytes,
                      ipv4, parse_ipv4, udp, zmsg, zstat, zstr)

EACCES = 13
ZRP_PORT = 5640
HOST_IP = "10.0.0.9"
HOST_MAC = bytes.fromhex("5254000000ff")
BROADCAST = b"\xff" * 6
PASSPHRASE = "cluster-secret-1"
WRONG = "not-the-secret"


def mac_of(ip):
    return bytes.fromhex("52540000000" + ip.split(".")[3])


# --- ZRP2 authentication, the host's own implementation ----------------------

def derive(passphrase):
    return hashlib.sha256(b"ZRP2 key" + passphrase.encode()).digest()


def proof(key, role, cnonce, snonce):
    return hmac.new(key, b"ZRP2 " + role.encode() + cnonce + snonce, hashlib.sha256).digest()


# --- the hub -----------------------------------------------------------------

class Hub:
    """Every frame one machine sends goes to all the others. Frames for
    the host (10.0.0.9) are answered here: ARP, and UDP to registered
    ports."""

    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(8)
        self.port = self.listener.getsockname()[1]
        self.conns = []
        self.lock = threading.Lock()
        self.udp_handlers = {}
        self.frames_seen = []  # every UDP payload on the wire, for looking at
        threading.Thread(target=self._accept, daemon=True).start()

    def netdev(self, ip):
        mac = ":".join(f"{b:02x}" for b in mac_of(ip))
        return ["-netdev", f"socket,id=n0,connect=127.0.0.1:{self.port}",
                "-device", f"ne2k_isa,netdev=n0,iobase=0x300,irq=9,mac={mac}"]

    def _accept(self):
        while True:
            try:
                conn, _ = self.listener.accept()
            except OSError:
                return
            with self.lock:
                self.conns.append(conn)
            threading.Thread(target=self._reader, args=(conn,), daemon=True).start()

    def _broadcast(self, frame, source=None):
        data = struct.pack(">I", len(frame)) + frame
        with self.lock:
            for c in self.conns:
                if c is not source:
                    try:
                        c.sendall(data)
                    except OSError:
                        pass

    def _reader(self, conn):
        def exact(n):
            buf = b""
            while len(buf) < n:
                chunk = conn.recv(n - len(buf))
                if not chunk:
                    raise EOFError
                buf += chunk
            return buf
        try:
            while True:
                frame = exact(struct.unpack(">I", exact(4))[0])
                self._broadcast(frame, conn)
                self._host(frame)
        except (EOFError, OSError):
            with self.lock:
                if conn in self.conns:
                    self.conns.remove(conn)

    def _host(self, frame):
        if frame[12:14] == b"\x08\x06" and len(frame) >= 42:
            op, target = struct.unpack(">H", frame[20:22])[0], socket.inet_ntoa(frame[38:42])
            if op == 1 and target == HOST_IP:
                reply = (struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2) + HOST_MAC + ip_bytes(HOST_IP)
                         + frame[22:28] + frame[28:32])
                self._broadcast(eth(frame[6:12], HOST_MAC, 0x0806, reply))
            return
        pkt = parse_ipv4(frame)
        if pkt and pkt[2] == 17 and len(pkt[3]) >= 8:
            sport, dport, ulen = struct.unpack(">HHH", pkt[3][:6])
            self.frames_seen.append(pkt[3][8:ulen])
            if pkt[1] == HOST_IP and dport in self.udp_handlers:
                self.udp_handlers[dport](pkt[0], sport, pkt[3][8:ulen])

    def send_udp(self, dst_ip, sport, dport, data):
        packet = ipv4(HOST_IP, dst_ip, 17, udp(HOST_IP, dst_ip, sport, dport, data))
        self._broadcast(eth(mac_of(dst_ip), HOST_MAC, 0x0800, packet))

    def close(self):
        self.listener.close()
        with self.lock:
            for c in self.conns:
                c.close()


# --- the host's ZRP client and servers ----------------------------------------

class Client:
    """A ZRP session from host port `port` to a guest's server."""

    def __init__(self, hub, ip, port):
        self.hub, self.ip, self.port, self.tag = hub, ip, port, 0
        self.replies = queue.Queue()
        hub.udp_handlers[port] = lambda src, sport, data: self.replies.put(data)

    def rpc(self, mtype, body=b"", tag=None):
        if tag is None:
            self.tag = self.tag % 0xFFFE + 1
            tag = self.tag
        request = zmsg(mtype, tag, body)
        for _ in range(5):  # the hub doesn't lose frames, but be patient
            self.hub.send_udp(self.ip, self.port, ZRP_PORT, request)
            try:
                reply = self.replies.get(timeout=2)
            except queue.Empty:
                continue
            r = Reader(reply)
            size, rtype, rtag = r.u32(), r.u8(), r.u16()
            if size != len(reply) or rtag != tag:
                raise TestFailure(f"bad reply header: {reply!r}")
            return rtype, r
        raise TestFailure(f"no reply to ZRP type {mtype} from {self.ip}")

    def rpc_waiting(self, mtype, body, timeout):
        """A request the server may take a while over: asks again (the same
        tag) until the answer is something other than Rpending."""
        self.tag = self.tag % 0xFFFE + 1
        deadline = time.time() + timeout
        while time.time() < deadline:
            rtype, r = self.rpc(mtype, body, tag=self.tag)
            if rtype != RPENDING:
                return rtype, r
            time.sleep(1)
        raise TestFailure(f"ZRP type {mtype} to {self.ip}: still pending after {timeout} s")

    def version(self):
        rtype, r = self.rpc(TVERSION, struct.pack("<I", 1472) + zstr(VERSION), tag=0xFFFF)
        check(rtype == RVERSION and r.u32() == 1472 and r.str() == VERSION, "Tversion")

    def auth(self, key):
        """Tauth; returns (cnonce, snonce, whether the server's proof was right)."""
        cnonce = os.urandom(16)
        rtype, r = self.rpc(TAUTH, cnonce)
        check(rtype == RAUTH, f"Tauth got type {rtype}")
        snonce, server_proof = r.take(16), r.take(32)
        return cnonce, snonce, hmac.compare_digest(server_proof, proof(key, "server", cnonce, snonce))

    def attach(self, fid, mac=b"", aname=""):
        return self.rpc(TATTACH, struct.pack("<I", fid) + zstr(aname) + mac)


def errno_of(reply):
    rtype, r = reply
    return r.u16() if rtype == RERROR else None


class KeyedServer:
    """A small ZRP server of the host's own, for the guest's client: with
    mode "right" it knows the key (and checks the guest's proof); "wrong"
    answers Tauth with a bad proof; "none" doesn't do authentication."""

    def __init__(self, hub, port, mode):
        self.hub, self.port, self.mode = hub, port, mode
        self.key = derive(PASSPHRASE)
        self.sessions = {}
        self.proofs_checked = 0
        self.tree = {"hello.txt": b"from the host's keyed server\n"}
        hub.udp_handlers[port] = self.handle

    def handle(self, ip, sport, data):
        r = Reader(data)
        size, mtype, tag = r.u32(), r.u8(), r.u16()
        s = self.sessions.setdefault((ip, sport), {"fids": {}})
        try:
            if mtype == TVERSION:
                msize, version = r.u32(), r.str()
                s.clear()
                s["fids"] = {}
                reply = zmsg(RVERSION, tag, struct.pack("<I", min(msize, 1472)) + zstr(version))
            elif mtype == TAUTH:
                if self.mode == "none":
                    raise OSError(ENOSYS)
                s["cnonce"], s["snonce"] = r.take(16), os.urandom(16)
                key = self.key if self.mode == "right" else derive(WRONG)
                reply = zmsg(RAUTH, tag, s["snonce"] + proof(key, "server", s["cnonce"], s["snonce"]))
            elif mtype == TATTACH:
                fid, aname = r.u32(), r.str()
                mac = data[r.p:]
                if "snonce" not in s or not hmac.compare_digest(
                        mac, proof(self.key, "client", s["cnonce"], s["snonce"])):
                    raise OSError(EACCES)
                self.proofs_checked += 1
                s["fids"][fid] = None
                reply = zmsg(RATTACH, tag, zstat(ZKT_DIR, 0, "/"))
            elif mtype == TWALK:
                fid, newfid, name = r.u32(), r.u32(), r.str()
                if s["fids"][fid] is not None or name not in self.tree:
                    raise OSError(ENOENT)
                s["fids"][newfid] = name
                reply = zmsg(RWALK, tag, zstat(ZKT_FILE, len(self.tree[name]), name))
            elif mtype == TREAD:
                fid, offset, count = r.u32(), r.u32(), r.u32()
                name = s["fids"][fid]
                if name is None:
                    out = b"".join(zstat(ZKT_FILE, len(v), k) for k, v in sorted(self.tree.items()))
                    out = out if offset == 0 else b""
                else:
                    out = self.tree[name][offset:offset + count]
                reply = zmsg(RREAD, tag, struct.pack("<I", len(out)) + out)
            elif mtype == TCLUNK:
                s["fids"].pop(r.u32())
                reply = zmsg(RCLUNK, tag)
            else:
                raise OSError(EPROTO)
        except OSError as e:
            reply = zmsg(RERROR, tag, struct.pack("<H", e.args[0]))
        except KeyError:
            reply = zmsg(RERROR, tag, struct.pack("<H", EBADF))
        self.hub.send_udp(ip, self.port, sport, reply)


# --- the test ------------------------------------------------------------------

def check(ok, what):
    if not ok:
        raise TestFailure(what)


class Cluster:
    def __init__(self, kernel, workdir):
        self.hub = Hub()
        disk = os.path.join(workdir, "fs.img")
        console_test.write_fat_disk(disk)
        self.frag = subprocess.run(["mtype", "-i", f"{disk}@@{2048 * 512}", "::/FRAG.BIN"],
                                   check=True, capture_output=True).stdout
        self.readme = subprocess.run(["mtype", "-i", f"{disk}@@{2048 * 512}", "::/README.TXT"],
                                     check=True, capture_output=True).stdout
        key = f"key={PASSPHRASE}"
        self.fs = Machine(kernel, self.hub.netdev("10.0.0.1")
                          + ["-drive", f"file={disk},format=raw,if=ide",
                             "-append", f"ip=10.0.0.1/24 {key} sysname=fs1 export=/n/ata0p1"])
        self.cpu = Machine(kernel, self.hub.netdev("10.0.0.2")
                           + ["-append", f"ip=10.0.0.2/24 {key} sysname=cpu1 rc=/boot/etc/rc.cpu"])
        self.term = Machine(kernel, self.hub.netdev("10.0.0.3")
                            + ["-append", f"ip=10.0.0.3/24 {key} sysname=term1"])
        self.stranger = Machine(kernel, self.hub.netdev("10.0.0.4")
                                + ["-append", "ip=10.0.0.4/24 key=wrong sysname=stranger"])

    def machines(self):
        return [self.fs, self.cpu, self.term, self.stranger]

    def close(self):
        for m in self.machines():
            try:
                m.close()
            except Exception:
                pass
        self.hub.close()

    # The steps.

    def boot(self):
        outputs = [m.expect(SHELL_PROMPT, timeout=90) for m in self.machines()]
        for name, out in zip(["fs1", "cpu1", "term1", "stranger"], outputs):
            check("zrp: cluster key set: sessions are authenticated" in out, f"{name}: no key:\n{out[-800:]}")
            check("Milestone M13" in out, f"{name}: no M13 marker")
        check("zrp: exporting /n/ata0p1 on udp port 5640" in outputs[0], "the file server did not export")
        # cpud goes into the background, so it may speak after the prompt.
        deadline = time.time() + 30
        while b'cpud: serving attach name "cpu"' not in self.cpu.output and time.time() < deadline:
            self.cpu._pump(0.2)
        check(b'cpud: serving attach name "cpu"' in self.cpu.output,
              "the CPU server's rc did not start cpud")

    def terminal_mounts_file_server(self):
        t = self.term
        run_command(t, "serial", "mount udp!10.0.0.1 /n", ["!mount:"], SHELL_PROMPT)
        run_command(t, "serial", "ls /n", ["readme.txt", "frag.bin", "bin/"], SHELL_PROMPT)
        run_command(t, "serial", "sum /n/frag.bin",
                    [f"{len(self.frag)} bytes, fnv1a {fnv1a(self.frag):08x}"], SHELL_PROMPT)

    def stranger_is_refused(self):
        s = self.stranger
        run_command(s, "serial", "mount udp!10.0.0.1 /n",
                    ["mount: udp!10.0.0.1: permission denied"], SHELL_PROMPT)
        run_command(s, "serial", "cpu udp!10.0.0.2 cat /dev/sysname",
                    ["cpu: udp!10.0.0.2: permission denied", "!cpu1"], SHELL_PROMPT)

    def host_client_authentication(self):
        key = derive(PASSPHRASE)
        # The right key: both proofs check out, and the files are there.
        c = Client(self.hub, "10.0.0.1", 7100)
        c.version()
        cnonce, snonce, server_ok = c.auth(key)
        check(server_ok, "the file server's proof of the key is wrong")
        check(c.attach(1, proof(key, "client", cnonce, snonce))[0] == RATTACH,
              "the right proof was refused")
        rtype, r = c.rpc(TWALK, struct.pack("<II", 1, 2) + zstr("readme.txt"))
        check(rtype == RWALK, "walking after authenticating")
        rtype, r = c.rpc(TREAD, struct.pack("<III", 2, 0, 1000))
        check(rtype == RREAD and r.take(r.u32()) == self.readme, "reading after authenticating")
        replayed = proof(key, "client", cnonce, snonce)

        # No proof at all.
        c = Client(self.hub, "10.0.0.1", 7101)
        c.version()
        check(errno_of(c.attach(1)) == EACCES, "an attach without proof was not EACCES")
        # A proof, but no Tauth in this session.
        c.version()
        check(errno_of(c.attach(1, replayed)) == EACCES, "a proof without Tauth was not EACCES")
        # A replayed proof: this session's server nonce is a new one.
        c.version()
        c.auth(key)
        check(errno_of(c.attach(1, replayed)) == EACCES, "a replayed proof was accepted")
        # The wrong key: the server's proof doesn't check out, and ours is refused.
        c.version()
        wrong = derive(WRONG)
        cnonce, snonce, server_ok = c.auth(wrong)
        check(not server_ok, "a server's proof checked out against the wrong key")
        check(errno_of(c.attach(1, proof(wrong, "client", cnonce, snonce))) == EACCES,
              "the wrong key's proof was accepted")
        c.version()
        check(errno_of(c.rpc(TAUTH, b"short")) == EPROTO, "a short Tauth was not EPROTO")
        # The key itself never crosses the wire.
        check(not any(key in p or PASSPHRASE.encode() in p for p in self.hub.frames_seen),
              "the key was seen on the wire")

    def guest_client_authentication(self):
        t = self.term
        right = self.keyed = KeyedServer(self.hub, 5640, "right")
        KeyedServer(self.hub, 5650, "wrong")
        KeyedServer(self.hub, 5651, "none")
        run_command(t, "serial", "mount udp!10.0.0.9 /mnt/term; cat /mnt/term/hello.txt",
                    ["from the host's keyed server"], SHELL_PROMPT)
        check(right.proofs_checked == 1, "the terminal's proof was not checked by the host")
        run_command(t, "serial", "unbind /mnt/term", [], SHELL_PROMPT)
        run_command(t, "serial", "mount udp!10.0.0.9!5650 /mnt/term",
                    ["mount: udp!10.0.0.9!5650: permission denied"], SHELL_PROMPT)
        run_command(t, "serial", "mount udp!10.0.0.9!5651 /mnt/term",
                    ["mount: udp!10.0.0.9!5651: permission denied"], SHELL_PROMPT)

    def job_started(self, what):
        """Waits for cpud to log a job running `what`; its number."""
        before = self.cpu.expect(f": {what} for udp!10.0.0.3", timeout=30)
        number = re.search(r"cpud: job (\d+)$", before)
        check(number, f"cpud's log: {before[-80:]!r}")
        return int(number.group(1))

    def expect_job(self, what, status):
        number = self.job_started(what)
        self.cpu.expect(f"cpud: job {number}: {status}", timeout=60)

    def cpu_runs_commands(self):
        t = self.term
        run_command(t, "serial", "cpu udp!10.0.0.2 cat /dev/sysname", ["\r\ncpu1\r\n"], SHELL_PROMPT)
        self.expect_job("/bin/cat", "exit 0")
        run_command(t, "serial", "cat /dev/sysname", ["\r\nterm1\r\n"], SHELL_PROMPT)
        # Shell syntax travels as one argument to a remote sh.
        run_command(t, "serial", "cpu udp!10.0.0.2 sh -c 'cat /dev/sysname | wc'",
                    ["\r\n      1       1       5\r\n"], SHELL_PROMPT)
        self.expect_job("/bin/sh", "exit 0")

    def remote_shell(self):
        t = self.term
        t.type_serial("cd /n; cpu udp!10.0.0.2\r")
        t.expect(SHELL_PROMPT, timeout=30)  # the remote shell's prompt
        # It starts where the terminal was, in the terminal's namespace --
        # which has the file server mounted: three machines.
        run_command(t, "serial", "pwd", ["\r\n/mnt/term/n\r\n"], SHELL_PROMPT)
        run_command(t, "serial", "sum frag.bin",
                    [f"frag.bin: {len(self.frag)} bytes, fnv1a {fnv1a(self.frag):08x}"], SHELL_PROMPT)
        run_command(t, "serial", "cat /dev/sysname; ls /mnt/term/boot/etc",
                    ["\r\ncpu1\r\n", "motd", "rc.cpu"], SHELL_PROMPT)
        # Longer than UDP's 9.5 s timeout: the terminal keeps asking, and
        # the CPU server keeps answering Rpending.
        t.type_serial("sleep 12; echo slept\r")
        t.expect("\r\nslept\r\n", timeout=40)
        t.expect(SHELL_PROMPT)
        run_command(t, "serial", "exit 7", [], SHELL_PROMPT)
        self.expect_job("/bin/sh", "exit 7")
        run_command(t, "serial", "pwd; cat /dev/sysname", ["\r\n/n\r\nterm1\r\n"], SHELL_PROMPT)

    def statuses(self):
        t = self.term
        run_command(t, "serial", "exit", ["console: the shell has exited"])
        run_command(t, "serial", "run /bin/cpu udp!10.0.0.2 /bin/false",
                    ["run: /bin/cpu exited with status 1"])
        self.expect_job("/bin/false", "exit 1")
        run_command(t, "serial", "run /bin/cpu udp!10.0.0.2 /boot/test/fault null",
                    ["cpu: /boot/test/fault: killed (vector 14)",
                     "run: /bin/cpu exited with status 142"])
        self.expect_job("/boot/test/fault", "exit 142")
        run_command(t, "serial", "run /bin/cpu udp!10.0.0.2 nosuch",
                    ["cpu: /bin/nosuch: not found", "run: /bin/cpu exited with status 127"])
        self.expect_job("/bin/nosuch", "exit 127")
        t.type_serial("run /bin/sh\r")
        t.expect(SHELL_PROMPT)

    def jobs_that_cannot_start(self):
        # The host plays a terminal whose namespace isn't there (an
        # export name nobody made): N/wait says why, not "exit 125".
        key = derive(PASSPHRASE)
        c = Client(self.hub, "10.0.0.2", 7200)
        c.version()
        cnonce, snonce, server_ok = c.auth(key)
        check(server_ok, "the CPU server's proof of the key is wrong")
        check(c.attach(1, proof(key, "client", cnonce, snonce), "cpu")[0] == RATTACH,
              "attaching to cpud")
        check(c.rpc(TWALK, struct.pack("<II", 1, 2) + zstr("new"))[0] == RWALK, "walking to new")
        job = b"udp!10.0.0.3\nno-such-export\n/\n/bin/cat"
        rtype, r = c.rpc(TWRITE, struct.pack("<III", 2, 0, len(job)) + job)
        check(rtype == RWRITE and r.u32() == len(job), "writing a job to new")
        rtype, r = c.rpc(TREAD, struct.pack("<III", 2, 0, 16))
        check(rtype == RREAD, "reading the job's number")
        number = r.take(r.u32()).decode().strip()
        check(c.rpc(TWALK, struct.pack("<II", 1, 3) + zstr(number))[0] == RWALK, "walking to the job")
        check(c.rpc(TWALK, struct.pack("<II", 3, 4) + zstr("wait"))[0] == RWALK, "walking to wait")
        rtype, r = c.rpc_waiting(TREAD, struct.pack("<III", 4, 0, 256), timeout=60)
        check(rtype == RREAD, f"reading wait got type {rtype}")
        said = r.take(r.u32()).decode()
        expected = "error cannot reach the terminal's namespace (udp!10.0.0.3): "
        check(said.startswith(expected) and said.endswith("\n"), f"wait said {said!r}")
        self.cpu.expect(f"cpud: job {number}: {said.strip()}", timeout=10)
        for fid in (4, 3, 2):
            c.rpc(TCLUNK, struct.pack("<I", fid))

        # A real terminal that gives the CPU server an address where
        # nothing answers (its /dev/net, in a namespace of its own, is a
        # file of the host's saying 10.0.0.99): cpu prints why and exits
        # with 125 -- which this shell (run from the monitor, see
        # statuses) passes on when it exits.
        t = self.term
        self.keyed.tree["net"] = b"ne0 10.0.0.99/24 gateway 0.0.0.0\n"
        why = "cannot reach the terminal's namespace (udp!10.0.0.99): "
        out = run_command(t, "serial", "sh -c 'newns; mount udp!10.0.0.9 /n; bind /n/net /dev/net; "
                          "cpu udp!10.0.0.2 cat /dev/sysname'",
                          [f"cpu: udp!10.0.0.2: {why}", "!cpu1"], SHELL_PROMPT)
        said = out[out.index(why):].split("\r\n")[0]
        before = self.cpu.expect(": /bin/cat for udp!10.0.0.99", timeout=10)
        number = re.search(r"cpud: job (\d+)$", before).group(1)
        self.cpu.expect(f"cpud: job {number}: error {said}", timeout=10)
        run_command(t, "serial", "exit", ["run: /bin/sh exited with status 125"])
        t.type_serial("run /bin/sh\r")
        t.expect(SHELL_PROMPT)
        run_command(t, "serial", "cat /dev/sysname", ["\r\nterm1\r\n"], SHELL_PROMPT)

    def remote_window(self, tmpdir):
        t = self.term
        d = Desktop(t, tmpdir)
        t.type_serial("desktop\r")
        t.expect("F1 opens the menu", timeout=30)
        d.key("f1")
        d.key("ret")
        t.expect('window 1 "Terminal"', timeout=30)
        time.sleep(1.5)
        d.type("cpu udp!10.0.0.2 clock\n")
        # The clock runs on the CPU server; its window is on this screen.
        before = t.expect(' "Clock" 200x70 at ', timeout=60)
        where = t.expect("\r\n")
        number = re.search(r"window (\d+)$", before)
        x, y = (int(v) for v in where.split(","))
        job = self.job_started("/bin/clock")

        def reading(s):
            return read_text(s, x + 28, y + 10, 8, ACCENT, 3)

        s = d.wait(lambda s: re.fullmatch(r"\d\d:\d\d:\d\d", reading(s)), "the remote clock's time",
                   timeout=30)
        first = reading(s)
        d.wait(lambda s: re.fullmatch(r"\d\d:\d\d:\d\d", reading(s)) and reading(s) != first,
               "the remote clock ticking", timeout=30)
        check(title(s, x, y, 5, DARK) == "Clock", "the remote window has the focus")
        # A key reaches it across the network: q quits it.
        d.key("q")
        t.expect(f"window {number.group(1)} closed", timeout=30)
        self.cpu.expect(f"cpud: job {job}: exit 0", timeout=30)
        d.key("f1")
        d.key("up")
        d.key("ret")
        t.expect("desktop: bye", timeout=30)
        t.expect(SHELL_PROMPT)

    def cpu_server_dies(self):
        t = self.term
        t.type_serial("cpu udp!10.0.0.2 /bin/sleep 300\r")
        self.job_started("/bin/sleep")
        time.sleep(2)
        self.cpu.close()
        t.expect("cpu: waiting for the job on udp!10.0.0.2: timed out", timeout=40)
        t.expect(SHELL_PROMPT)
        run_command(t, "serial", "echo still here", ["\r\nstill here\r\n"], SHELL_PROMPT)


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    console_test.BOOTFS_DIR = os.path.join(os.path.dirname(kernel), "bootfs")
    workdir = tempfile.mkdtemp(prefix="zkt-cluster-")
    failures = 0
    cluster = Cluster(kernel, workdir)
    try:
        for name, step in [
            ("four machines boot with their roles", cluster.boot),
            ("the terminal mounts the file server (authenticated)", cluster.terminal_mounts_file_server),
            ("a machine with the wrong key is refused by both servers", cluster.stranger_is_refused),
            ("the host's ZRP2 client: right key, none, no Tauth, replayed and wrong proofs",
             cluster.host_client_authentication),
            ("the terminal's client: a host server with the key; ones that can't prove it",
             cluster.guest_client_authentication),
            ("cpu runs a command on the CPU server, on the terminal's console",
             cluster.cpu_runs_commands),
            ("a remote shell: the terminal's namespace, the file server through it, a long wait",
             cluster.remote_shell),
            ("exit statuses come back: failure, a fault, a missing program", cluster.statuses),
            ("a job that can't start: why comes back to the terminal", cluster.jobs_that_cannot_start),
            ("a program on the CPU server opens a window on the terminal's desktop",
             lambda: cluster.remote_window(workdir)),
            ("the CPU server dies under a job: the terminal notices and carries on",
             cluster.cpu_server_dies),
        ]:
            try:
                step()
                print(f"PASS: {name}")
            except TestFailure as e:
                print(f"FAIL: {name}: {e}")
                failures += 1
                if name.startswith("four machines"):
                    break
    finally:
        cluster.close()
        shutil.rmtree(workdir, ignore_errors=True)
    print("cluster test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
