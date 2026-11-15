#!/usr/bin/env python3
"""Client for the ESP32 UART monitor (esp32mon/): decode and log all channels.

usage: uartmon.py [host[:port]] [-o DIR] [--all] [--replay FILE]

  host       ESP32 address (default abus-uartmon.local, port 2300)
  -o DIR     log directory (default work/uartmon-YYYYmmdd-HHMMSS):
               decoded.log   one line per frame / text line, timestamped
               ch{A,B,C}.bin raw bytes per channel, WiFi config redacted
  --all      also show heartbeats and pings (hidden by default, counted)
  --replay   decode a saved record stream (for tests) instead of connecting

Channels: A = T30 -> XR871, B = XR871 -> T30, C = XR871 debug console.
Frames (aa 55 aa 55, u16 len, cmd, 01, u16 0, u16 param, payload) are shown
with the command name; stock 8-byte frames without parameter are recognised
when the next magic or a pause > 20 ms follows. The payload of cmd 0xc0
(WiFi credentials) is never shown and is zeroed in every file written; only
the lengths are logged. Stop with Ctrl-C.
"""
import argparse
import os
import socket
import struct
import sys
import time

NAMES = {0x00: "hello", 0x02: "ping", 0x12: "version", 0x13: "factory-default",
         0x18: "basic-params", 0x19: "version+uuid", 0x21: "force-iframe",
         0x40: "info/clock", 0x83: "power/sleep", 0x85: "wake-source",
         0xa2: "audio-test", 0xa4: "fw-update", 0xa9: "heartbeat",
         0xc0: "wifi-config", 0xc4: "probe-pin", 0xc5: "ready-pin", 0xf1: "marker"}
QUIET = {0xa9, 0x02}
WAKE = {0x004: "cold", 0x101: "pir", 0x102: "key", 0x103: "wlan", 0x104: "timer"}
MAGIC = b"\xaa\x55\xaa\x55"
CH = "ABC"
MAX_PAYLOAD = 116


def redact(f):
    """zero the WiFi credentials in a complete frame (bytes -> bytes)"""
    if len(f) > 12 and f[6] == 0xc0:
        return f[:12] + bytes(len(f) - 12)
    return f


class Chan:
    """byte stream parser for one channel; emit(t, line), raw(bytes)"""

    def __init__(self, ch, emit, raw, show_all):
        self.ch, self.emit, self.raw, self.all = ch, emit, raw, show_all
        self.mi = 0
        self.f = None
        self.ft = self.last = 0
        self.text, self.tt = "", 0
        self.quiet = 0

    def flush_text(self):
        if self.text:
            self.emit(self.tt, self.ch, self.text)
            self.text = ""

    def text_byte(self, b, t):
        self.raw(bytes([b]))
        if b == 0:
            return
        if not self.text:
            self.tt = t
        if b == 10:
            self.flush_text()
        elif b != 13:
            self.text += chr(b) if 32 <= b < 127 else "<%02x>" % b
            if len(self.text) > 200:
                self.flush_text()

    def frame(self, f, t):
        f = bytes(f)
        lens = (f[12], f[45]) if f[6] == 0xc0 and len(f) >= 46 else None
        f = redact(f)
        self.raw(f)
        plen, cmd = f[4] | f[5] << 8, f[6]
        if cmd in QUIET and not self.all:
            self.quiet += 1
            return
        par = f[10] | f[11] << 8 if len(f) >= 12 else None
        pl = f[12:]
        s = "cmd 0x%02x %s" % (cmd, NAMES.get(cmd, "?"))
        s += " p=%d" % par if par is not None else " (8-byte)"
        s += " len=%d" % plen
        if cmd == 0xc0 and plen:
            s += " [credentials redacted%s]" % (
                ", ssid %d B, psk %d B" % lens if lens else "")
        elif cmd == 0xf1 and plen:
            s += ' "%s"' % "".join(chr(b) for b in pl if 32 <= b < 127)
        elif cmd == 0x85 and plen >= 4:
            r = struct.unpack_from("<I", pl)[0]
            s += " reason 0x%x %s" % (r, WAKE.get(r, ""))
        elif cmd == 0x40 and plen >= 8:
            ms = struct.unpack_from("<Q", pl)[0]
            s += " clock %s" % time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(ms / 1000))
        elif plen:
            s += " " + pl[:32].hex(" ") + (" ..." if plen > 32 else "")
        self.flush_text()
        self.emit(t, self.ch, s)

    def tick(self, t):
        """call with the current time: completes an 8-byte frame after a pause"""
        if self.f is not None and len(self.f) == 8 and \
                (self.f[4] | self.f[5] << 8) == 0 and t - self.last > 20:
            self.frame(self.f, self.ft)
            self.f = None

    def feed(self, data, t):
        for b in data:
            self.tick(t)
            if self.f is not None:
                self.f.append(b)
                self.last = t
                n, plen = len(self.f), self.f[4] | self.f[5] << 8 if len(self.f) >= 6 else 0
                if n >= 6 and plen > MAX_PAYLOAD:
                    junk, self.f = self.f, None
                    for x in junk:
                        self.text_byte(x, t)
                elif n == 12 and plen == 0 and bytes(self.f[8:12]) == MAGIC:
                    self.frame(self.f[:8], self.ft)
                    self.f, self.ft = bytearray(MAGIC), t
                elif n >= 6 and n == 12 + plen:
                    self.frame(self.f, self.ft)
                    self.f = None
                continue
            if b == MAGIC[self.mi]:
                self.mi += 1
                if self.mi == 4:
                    self.mi = 0
                    self.f, self.ft, self.last = bytearray(MAGIC), t, t
                continue
            for x in MAGIC[:self.mi]:
                self.text_byte(x, t)
            self.mi = 1 if b == MAGIC[0] else 0
            if not self.mi:
                self.text_byte(b, t)


class Mux:
    """splits the ESP32 record stream: a5 ch u32(ms) u8(len) data"""

    def __init__(self, outdir, show_all, out=sys.stdout):
        self.buf = b""
        self.out = out
        self.files = {}
        self.dir = outdir
        self.dec = open(os.path.join(outdir, "decoded.log"), "a") if outdir else None
        self.chans = [Chan(i, self.emit, self.rawf(i), show_all) for i in range(3)]
        self.t = 0
        self.lost = 0

    def rawf(self, i):
        def w(b):
            if not self.dir:
                return
            if i not in self.files:
                self.files[i] = open(os.path.join(self.dir, "ch%s.bin" % CH[i]), "ab")
            self.files[i].write(b)
        return w

    def emit(self, t, ch, s):
        line = "%9.3f %s %s" % (t / 1000, CH[ch], s)
        print(line, file=self.out, flush=True)
        if self.dec:
            self.dec.write(line + "\n")
            self.dec.flush()

    def feed(self, data):
        self.buf += data
        while len(self.buf) >= 7:
            if self.buf[0] != 0xa5:
                i = self.buf.find(b"\xa5", 1)
                self.buf = self.buf[i:] if i > 0 else b""
                continue
            ch, t, n = self.buf[1], struct.unpack_from("<I", self.buf, 2)[0], self.buf[6]
            if ch == 0xff:
                self.lost += t
                print("%9.3f - %d records lost (client too slow)" % (self.t / 1000, t),
                      file=self.out)
                self.buf = self.buf[7:]
                continue
            if len(self.buf) < 7 + n:
                break
            rec, self.buf = self.buf[:7 + n], self.buf[7 + n:]
            if ch < 3:
                self.t = t
                self.chans[ch].feed(rec[7:], t)
        for c in self.chans:
            c.tick(self.t)

    def close(self):
        for c in self.chans:
            c.flush_text()
        q = sum(c.quiet for c in self.chans)
        if q:
            print("(%d heartbeats/pings hidden, --all shows them)" % q, file=self.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("host", nargs="?", default="abus-uartmon.local")
    ap.add_argument("-o", dest="outdir")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--replay")
    a = ap.parse_args()

    outdir = a.outdir
    if outdir is None and not a.replay:
        outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work",
                              time.strftime("uartmon-%Y%m%d-%H%M%S"))
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    m = Mux(outdir, a.all)
    try:
        if a.replay:
            with open(a.replay, "rb") as f:
                m.feed(f.read())
        else:
            host, _, port = a.host.partition(":")
            s = socket.create_connection((host, int(port or 2300)), timeout=10)
            s.settimeout(None)
            print("connected to %s, logging to %s" % (a.host, outdir), file=sys.stderr)
            while True:
                d = s.recv(4096)
                if not d:
                    print("connection closed", file=sys.stderr)
                    break
                m.feed(d)
    except KeyboardInterrupt:
        pass
    m.close()


if __name__ == "__main__":
    main()
