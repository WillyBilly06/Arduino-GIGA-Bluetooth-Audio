#!/usr/bin/env python3
"""Browser-based serial monitor.

Serves a web UI on the local network and bridges it to one or more serial
ports. Several ports can be open at once; every browser tab that connects sees
the same live data. Ports that disappear (a board resetting, unplugged, or
dropping into its bootloader) are reopened automatically when they come back.

usage:
    python serial_monitor.py [--host 0.0.0.0] [--port 8765] [--open]

Anyone who can reach the host and port can read and write the serial ports, so
use --host 127.0.0.1 on an untrusted network.

Requires pyserial (python -m pip install pyserial).
"""
import argparse
import base64
import json
import os
import socket
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover - depends on the host environment
    sys.exit("pyserial is required: python -m pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
INDEX = os.path.join(HERE, "index.html")
MAX_BUFFER_BYTES = 8 * 1024 * 1024   # history kept per port for late joiners
RECONNECT_SECONDS = 1.0

PARITY = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN, "O": serial.PARITY_ODD,
          "M": serial.PARITY_MARK, "S": serial.PARITY_SPACE}
STOPBITS = {"1": serial.STOPBITS_ONE, "1.5": serial.STOPBITS_ONE_POINT_FIVE,
            "2": serial.STOPBITS_TWO}
# Friendly names for boards this project uses; everything else shows the OS name.
KNOWN_USB = {
    (0x2341, 0x0266): "Arduino GIGA R1 WiFi",
    (0x2341, 0x0366): "Arduino GIGA R1 WiFi (bootloader)",
    (0x2341, 0x025B): "Arduino Portenta H7",
    (0x2341, 0x035B): "Arduino Portenta H7 (bootloader)",
}


def touch_1200(port):
    """1200-baud touch: the Arduino convention for 'reset into the bootloader'.

    The board only reacts if its running sketch services the USB CDC line-coding
    request; a sketch that never yields will ignore it.
    """
    t = serial.Serial()
    t.port, t.baudrate = port, 1200
    t.dtr = True
    t.open()
    time.sleep(0.2)
    t.dtr = False
    time.sleep(0.2)
    t.close()


class Session:
    """One open serial port, its reader thread and its replayable history."""

    def __init__(self, port, settings):
        self.port = port
        self.settings = settings
        self.cond = threading.Condition()
        self.write_lock = threading.Lock()
        self.chunks = []          # (seq, unix_time, kind, bytes); kind is "rx" or "tx"
        self.size = 0
        self.seq = 0
        self.status_seq = 0
        self.state = "opening"
        self.error = ""
        self.rx = 0
        self.tx = 0
        self.ser = None
        self.removed = False
        self.stop = threading.Event()
        self.touch = threading.Event()
        self.thread = threading.Thread(target=self._run, name="serial-" + port, daemon=True)
        self.thread.start()

    # --- history -----------------------------------------------------------------
    def _push(self, data, kind):
        with self.cond:
            self.seq += 1
            self.chunks.append((self.seq, time.time(), kind, data))
            self.size += len(data)
            if self.size > MAX_BUFFER_BYTES:
                drop, freed = 0, 0
                while drop < len(self.chunks) - 1 and self.size - freed > MAX_BUFFER_BYTES * 3 // 4:
                    freed += len(self.chunks[drop][3])
                    drop += 1
                del self.chunks[:drop]
                self.size -= freed
            self.cond.notify_all()

    def since(self, seq, limit=512):
        """Chunks newer than seq (called with self.cond held)."""
        if not self.chunks or seq >= self.seq:
            return []
        first = self.chunks[0][0]
        start = max(0, seq - first + 1)
        return self.chunks[start:start + limit]

    def _set_state(self, state, error=""):
        with self.cond:
            self.state, self.error = state, error
            self.status_seq += 1
            self.cond.notify_all()

    def status(self):
        return {"port": self.port, "state": self.state, "error": self.error,
                "rx": self.rx, "tx": self.tx, "settings": self.settings}

    # --- port I/O ----------------------------------------------------------------
    def _open(self):
        s = self.settings
        ser = serial.serial_for_url(self.port, do_not_open=True)
        ser.baudrate = s["baud"]
        ser.bytesize = s["bytesize"]
        ser.parity = PARITY[s["parity"]]
        ser.stopbits = STOPBITS[s["stopbits"]]
        ser.timeout = 0.05
        ser.write_timeout = 2
        try:
            ser.dtr = s["dtr"]   # applied when the port opens
            ser.rts = s["rts"]
        except (AttributeError, ValueError, serial.SerialException):
            pass                 # URL handlers such as socket:// have no modem lines
        ser.open()
        return ser

    def _wait(self, seconds):
        self.stop.wait(seconds)

    def _do_touch(self):
        try:
            touch_1200(self.port)
            self._push(b"[1200-baud touch sent]\n", "sys")
        except Exception as exc:  # noqa: BLE001 - report every failure to the UI
            self._push(("[1200-baud touch failed: %s]\n" % exc).encode(), "sys")

    def _wait_for_departure(self, limit):
        """Keep the port closed until the board drops it (it resets only after
        the host releases DTR); reopening early delays or wedges the reset."""
        end = time.time() + limit
        while not self.stop.is_set() and time.time() < end:
            if not any(p.device == self.port for p in serial.tools.list_ports.comports()):
                self._push(b"[port left; board is resetting]\n", "sys")
                return
            self._wait(0.25)

    def _run(self):
        while not self.stop.is_set():
            if self.touch.is_set():
                self.touch.clear()
                self._do_touch()
                self._wait_for_departure(10.0)
                continue
            try:
                self.ser = self._open()
            except Exception as exc:  # noqa: BLE001
                self.ser = None
                if not self.settings["reconnect"]:
                    self._set_state("error", str(exc))
                    return
                self._set_state("waiting", str(exc))
                self._wait(RECONNECT_SECONDS)
                continue
            self._set_state("open")
            err = ""
            try:
                while not self.stop.is_set() and not self.touch.is_set():
                    data = self.ser.read(max(1, self.ser.in_waiting))
                    if data:
                        more = self.ser.in_waiting
                        if more:
                            data += self.ser.read(more)
                        self.rx += len(data)
                        self._push(data, "rx")
            except Exception as exc:  # noqa: BLE001 - device unplugged, reset, ...
                err = str(exc)
            ser, self.ser = self.ser, None
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
            if self.stop.is_set():
                break
            if self.touch.is_set():
                continue
            if not self.settings["reconnect"]:
                self._set_state("closed", err or "port closed")
                return
            self._set_state("waiting", err or "port closed")
            self._wait(RECONNECT_SECONDS)
        self._set_state("closed")

    def write(self, data):
        with self.write_lock:
            ser = self.ser
            if ser is None:
                raise IOError("port is not open (%s)" % self.state)
            # Allow twice the time the bytes need on the wire (10 bits per byte), plus slack.
            ser.write_timeout = 2 + 20.0 * len(data) / max(1, self.settings["baud"])
            ser.write(data)
            ser.flush()
        self.tx += len(data)
        self._push(data, "tx")

    def set_lines(self, dtr=None, rts=None):
        if dtr is not None:
            self.settings["dtr"] = bool(dtr)
        if rts is not None:
            self.settings["rts"] = bool(rts)
        ser = self.ser
        if ser is not None:
            if dtr is not None:
                ser.dtr = bool(dtr)
            if rts is not None:
                ser.rts = bool(rts)
        self._set_state(self.state, self.error)

    def request_touch(self):
        self.touch.set()
        ser = self.ser
        if ser is not None:
            try:
                ser.cancel_read()
            except Exception:  # noqa: BLE001
                pass

    def close(self):
        self.stop.set()
        ser = self.ser
        if ser is not None:
            try:
                ser.cancel_read()
            except Exception:  # noqa: BLE001
                pass
        self.thread.join(timeout=3)
        with self.cond:
            self.removed = True
            self.cond.notify_all()


SESSIONS = {}
SESSIONS_LOCK = threading.Lock()


def list_ports():
    held = set(SESSIONS)
    out = []
    for p in sorted(serial.tools.list_ports.comports(), key=lambda p: p.device):
        known = KNOWN_USB.get((p.vid, p.pid)) if p.vid is not None else None
        out.append({
            "device": p.device,
            "description": known or (p.description if p.description and p.description != "n/a" else ""),
            "manufacturer": p.manufacturer or "",
            "serial_number": p.serial_number or "",
            "vid_pid": "%04X:%04X" % (p.vid, p.pid) if p.vid is not None else "",
            "open": p.device in held,
        })
    return out


def parse_settings(body):
    try:
        baud = int(body.get("baud", 115200))
        bytesize = int(body.get("bytesize", 8))
    except (TypeError, ValueError):
        raise ValueError("baud and data bits must be numbers")
    if not 50 <= baud <= 20_000_000:
        raise ValueError("baud rate out of range")
    if bytesize not in (5, 6, 7, 8):
        raise ValueError("data bits must be 5, 6, 7 or 8")
    parity = str(body.get("parity", "N")).upper()
    stopbits = str(body.get("stopbits", "1"))
    if parity not in PARITY:
        raise ValueError("parity must be one of N, E, O, M, S")
    if stopbits not in STOPBITS:
        raise ValueError("stop bits must be 1, 1.5 or 2")
    return {"baud": baud, "bytesize": bytesize, "parity": parity, "stopbits": stopbits,
            "dtr": bool(body.get("dtr", True)), "rts": bool(body.get("rts", True)),
            "reconnect": bool(body.get("reconnect", True))}


class Handler(BaseHTTPRequestHandler):
    server_version = "SerialMonitor/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # keep the console quiet
        pass

    # --- helpers -----------------------------------------------------------------
    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 4 * 1024 * 1024:
            raise ValueError("request too large")
        raw = self.rfile.read(n) if n else b"{}"
        return json.loads(raw or b"{}")

    def _session(self, port):
        with SESSIONS_LOCK:
            sess = SESSIONS.get(port)
        if sess is None:
            raise KeyError("%s is not open" % port)
        return sess

    # --- routes ------------------------------------------------------------------
    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            with open(INDEX, "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if url.path == "/api/ports":
            return self._send(200, {"ports": list_ports(),
                                    "sessions": [s.status() for s in list(SESSIONS.values())]})
        if url.path == "/api/stream":
            q = parse_qs(url.query)
            try:
                sess = self._session(q.get("port", [""])[0])
            except KeyError as exc:
                return self._send(404, {"error": str(exc)})
            since = self.headers.get("Last-Event-ID") or q.get("since", ["0"])[0]
            try:
                since = int(since)
            except ValueError:
                since = 0
            return self._stream(sess, since)
        return self._send(404, {"error": "not found"})

    def do_POST(self):
        url = urlparse(self.path)
        try:
            body = self._body()
            if url.path == "/api/open":
                port = str(body.get("port", "")).strip()
                if not port:
                    raise ValueError("choose a port")
                settings = parse_settings(body)
                with SESSIONS_LOCK:
                    if port in SESSIONS:
                        raise ValueError("%s is already open" % port)
                    SESSIONS[port] = Session(port, settings)
                return self._send(200, {"ok": True})
            if url.path == "/api/close":
                with SESSIONS_LOCK:
                    sess = SESSIONS.pop(str(body.get("port", "")), None)
                if sess:
                    sess.close()
                return self._send(200, {"ok": True})
            if url.path == "/api/send":
                data = base64.b64decode(body.get("data", ""))
                self._session(str(body.get("port", ""))).write(data)
                return self._send(200, {"ok": True, "bytes": len(data)})
            if url.path == "/api/lines":
                self._session(str(body.get("port", ""))).set_lines(body.get("dtr"), body.get("rts"))
                return self._send(200, {"ok": True})
            if url.path == "/api/touch":
                port = str(body.get("port", ""))
                with SESSIONS_LOCK:
                    sess = SESSIONS.get(port)
                if sess:
                    sess.request_touch()
                else:
                    touch_1200(port)
                return self._send(200, {"ok": True})
        except KeyError as exc:
            return self._send(404, {"error": str(exc).strip("'\"")})
        except (ValueError, IOError, serial.SerialException) as exc:
            return self._send(400, {"error": str(exc)})
        return self._send(404, {"error": "not found"})

    def _stream(self, sess, since):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        sent_status = -1
        try:
            self.wfile.write(b"retry: 1000\n\n")
            while True:
                with sess.cond:
                    batch = sess.since(since)
                    if not batch and sess.status_seq == sent_status and not sess.removed:
                        sess.cond.wait(timeout=15)
                        batch = sess.since(since)
                    status_changed = sess.status_seq != sent_status
                    sent_status = sess.status_seq
                    status = sess.status()
                    removed = sess.removed
                    if since > sess.seq:          # server restarted under the client
                        since = 0
                        batch = sess.since(0)
                out = []
                if status_changed:
                    out.append("event: status\ndata: %s\n\n" % json.dumps(status))
                if batch:
                    since = batch[-1][0]
                    payload = [[c[1], c[2], base64.b64encode(c[3]).decode()] for c in batch]
                    out.append("id: %d\nevent: data\ndata: %s\n\n" % (since, json.dumps(payload)))
                if removed:
                    out.append("event: closed\ndata: {}\n\n")
                if not out:
                    out.append(": keepalive\n\n")
                self.wfile.write("".join(out).encode())
                self.wfile.flush()
                if removed:
                    return
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            return


def lan_address():
    """Best guess at this machine's LAN address (no packets are sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--host", default="0.0.0.0", help="address to listen on (default: all interfaces)")
    ap.add_argument("--port", type=int, default=8765, help="HTTP port (default: 8765)")
    ap.add_argument("--open", action="store_true", help="open the UI in the default browser")
    args = ap.parse_args()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.daemon_threads = True
    local = "http://127.0.0.1:%d/" % args.port
    print("Serial monitor running")
    print("  this machine:  " + local)
    if args.host in ("0.0.0.0", ""):
        print("  local network: http://%s:%d/" % (lan_address(), args.port))
    print("Press Ctrl+C to stop.")
    if args.open:
        webbrowser.open(local)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        with SESSIONS_LOCK:
            sessions = list(SESSIONS.values())
            SESSIONS.clear()
        for sess in sessions:
            sess.close()
        httpd.server_close()


if __name__ == "__main__":
    main()
