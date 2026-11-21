#!/usr/bin/env python3
"""Flash a GIGA R1 M7 image over DFU without pressing RESET, then verify by readback.

usage: python tools/flash_giga.py <image.bin> [--port COM8] [--monitor http://127.0.0.1:8780]

Bootloader entry, first that works:
  1. the Receiver example's 'B' console command (sent through the browser serial
     monitor if it holds the port, else written to the port directly);
  2. a 1200-baud touch (needs a sketch that services USB CDC line coding);
  3. otherwise: double-press RESET; this script keeps waiting for DFU.
Requires pyserial and the Arduino mbed_giga dfu-util.
"""
import argparse, base64, glob, json, os, subprocess, sys, time, urllib.request
import serial

ADDR = "0x08040000"
VIDPID = "2341:0366"


def find_dfu():
    pats = [os.path.expandvars(r"%LOCALAPPDATA%\Arduino15\packages\arduino\tools\dfu-util\*\dfu-util.exe"),
            os.path.expanduser("~/.arduino15/packages/arduino/tools/dfu-util/*/dfu-util")]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[-1]
    return "dfu-util"


def in_dfu(dfu):
    return VIDPID in subprocess.run([dfu, "--list"], capture_output=True, text=True).stdout


def monitor_holds(base, port):
    try:
        d = json.loads(urllib.request.urlopen(base + "/api/ports", timeout=2).read())
        return any(s["port"] == port for s in d["sessions"])
    except Exception:
        return False


def post(base, path, body):
    req = urllib.request.Request(base + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=5).read())


def send_command(args, data):
    for base in args.monitor:
        if monitor_holds(base, args.port):
            post(base, "/api/send", {"port": args.port, "data": base64.b64encode(data).decode()})
            return "monitor " + base
    with serial.Serial(args.port, 921600, timeout=0.5) as s:
        s.write(data)
    return args.port


def touch(args):
    for base in args.monitor:
        if monitor_holds(base, args.port):
            post(base, "/api/touch", {"port": args.port})
            return
    t = serial.Serial(); t.port, t.baudrate, t.dtr = args.port, 1200, True
    t.open(); time.sleep(0.2); t.dtr = False; time.sleep(0.2); t.close()


def wait_dfu(dfu, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if in_dfu(dfu):
            return True
        time.sleep(0.5)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--monitor", action="append", default=None,
                    help="serial monitor base URL(s) to route through (default 8780 on localhost)")
    ap.add_argument("--serial", default="", help="board USB serial number when several boards are attached")
    args = ap.parse_args()
    args.monitor = args.monitor or ["http://127.0.0.1:8780"]
    dfu = find_dfu()
    sel = ["--device", "," + VIDPID, "--alt", "0"] + (["--serial", args.serial] if args.serial else [])

    if not in_dfu(dfu):
        try:
            print("bootloader: 'B' command via", send_command(args, b"B"))
        except Exception as exc:
            print("bootloader: 'B' command failed:", exc)
        if not wait_dfu(dfu, 45):
            print("bootloader: trying 1200-baud touch")
            try:
                touch(args)
            except Exception as exc:
                print("touch failed:", exc)
            if not wait_dfu(dfu, 15):
                print("no DFU yet: double-press RESET on the GIGA (waiting up to 10 min)")
                if not wait_dfu(dfu, 600):
                    sys.exit("gave up waiting for DFU")
    time.sleep(1.0)  # let Windows finish binding the DFU interface
    for attempt in range(5):
        r = subprocess.run([dfu, *sel, "--dfuse-address", ADDR, "--download", args.image], capture_output=True, text=True)
        if r.returncode == 0:
            break
        print("download attempt %d failed: %s" % (attempt + 1, (r.stdout + r.stderr).strip().splitlines()[-1:]))
        time.sleep(2)
    else:
        sys.exit("download failed; if every attempt says LIBUSB_ERROR_PIPE the bootloader is wedged: press RESET once")
    rb = args.image + ".readback"
    if os.path.exists(rb):
        os.remove(rb)
    subprocess.run([dfu, *sel, "--dfuse-address", "%s:%d:leave" % (ADDR, os.path.getsize(args.image)), "--upload", rb],
                   capture_output=True, text=True)
    ok = os.path.exists(rb) and open(rb, "rb").read() == open(args.image, "rb").read()
    print("readback", "MATCH" if ok else "MISMATCH")
    sys.exit(0 if ok else 3)


if __name__ == "__main__":
    main()
