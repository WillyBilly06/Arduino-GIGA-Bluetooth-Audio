# Serial monitor

A serial monitor that runs in a browser. `serial_monitor.py` serves `index.html`
and bridges it to one or more serial ports. Every browser tab, on this machine
or elsewhere on the network, sees the same live data.

```
python -m pip install pyserial
python tools/serial_monitor/serial_monitor.py            # http://<this-pc>:8780
python tools/serial_monitor/serial_monitor.py --host 127.0.0.1 --port 8780 --open
```

Anyone who can reach the port can read and write your serial ports. On an
untrusted network, use `--host 127.0.0.1`.

## What it does

- **Ports.** Several ports can be open at once, each in its own tab. A port that
  disappears is reopened when it comes back: the board resets, is unplugged, or
  drops into its bootloader. The history (8 MB per port) is kept across
  reconnects and is replayed to browsers that connect later.
- **Board control.** DTR/RTS toggles, and *Reset to bootloader* (a 1200-baud
  touch; the GIGA re-enumerates in DFU).
- **Viewing.** Filter by text or `/regex/` (with invert) and highlight matches.
  Optional timestamps, `key=value` colouring, wrap, sent-data echo and hex view.
  Pause, clear, and save the log.
- **Sending.** A send bar with line-ending choice, hex send, and history.
- **Audio stream strip.** Built from the GIGA Receiver's `GBR_STATUS` lines:
  - codec, sample rate, bit depth and channels
  - negotiated bitrate, where the codec reports one; otherwise it shows
    "chosen by source"
  - measured bitrate (from `rx_bytes` deltas) and frame count

  The strip is dimmed while nothing is streaming. Saved logs start with a
  `# Audio stream:` summary line.

## Sharing COM ports on Windows

Windows lets only one process open a COM port. While the monitor holds a port,
other tools must go through the monitor:

- **Read:** `GET /api/stream?port=COM8` is a Server-Sent Events stream. `data`
  events carry `[[unix_ts, "rx"|"tx"|"sys", base64], ...]`, and `Last-Event-ID`
  resumes from a given point.
- **Write:** `POST /api/send` with `{"port": "COM8", "data": "<base64>"}`.
- **Reset to bootloader:** `POST /api/touch` with `{"port": "COM8"}`, then flash
  with dfu-util.

Other endpoints:

| Endpoint | Use |
|---|---|
| `GET /api/ports` | Lists ports and sessions |
| `POST /api/open` | Opens a port: `port`, `baud`, `bytesize`, `parity`, `stopbits`, `dtr`, `rts`, `reconnect` |
| `POST /api/close` | Closes a port |
| `POST /api/lines` | Sets DTR/RTS |
