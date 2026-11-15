# Lu-Fi UART Monitor

An ESP32 that listens on up to three 3.3 V serial lines and makes them
available over WiFi: a live view in the browser, a full terminal (xterm.js),
telnet consoles and raw TCP streams. No software needed on the PC, and it
works offline: everything the web UI needs is built into the firmware.

It was built to watch the internal UARTs of an IP camera (T30 ⇄ XR871 link
plus a debug console) and is now used as a general-purpose serial console for
any 3.3 V device, e.g. Thingino cameras.

## Features

- **Monitor**: live view of all three channels with timestamps. It has:
  - channel filters and names;
  - ANSI rendering: colours, CR progress bars, backspace, UTF-8;
  - decoding of `aa 55 aa 55` binary frames;
  - pause, Clear that survives a reload, and Save log;
  - a **send line** under the view (Enter sends; ↑/↓ history; CR LF / CR /
    LF / none; `hex:…` for raw bytes).
- **Terminal**: xterm.js in the browser over a WebSocket, with cursor keys,
  Ctrl-C, `top` and `vi`, and 10,000 lines of scrollback. It also shows where
  your keystrokes go (e.g. `keys → TX A (GPIO17)` or `read only`).
- **Settings**:
  - per channel: name, baud rate, "keys to" (which TX pin a channel's
    keystrokes use), TX on;
  - home WiFi (shows the network name, never the password);
  - the access point, automatic or always on.
- **Activity LEDs**: the dots in the console frames light up on receive and
  transmit.
- **TCP access**: telnet consoles, raw byte streams, and a multiplexed record
  stream for the PC logger.
- **Firmware update over WiFi**: `./update.sh`, HTTP push. No inbound
  connection to the PC is needed, so it works with a firewall.

## Safety

TX pins are **not driven** unless needed. A driven TX pin can back-power an
unpowered board through its RX pin.

- An idle TX pin is an input with the weak internal pull-up (~45 kΩ, < 0.1 mA).
  That keeps the device's RX line from floating low (which a UART sees as a
  *break*), but it cannot power anything.
- The terminal, telnet and the send line connect the TX signal at the first
  keystroke or send. They release it after the send, or when the last terminal
  closes. Just watching never transmits.
- "TX on" in Settings drives the pin permanently. Use it only while the device
  is powered.
- Switching TX does not restart the UART. It only connects or disconnects the
  pin, so no received bytes are lost and the line does not glitch.

## Hardware and wiring (ESP32 DevKit, `esp32dev`)

| ESP32 pin | Channel | Connect to |
|---|---|---|
| GND | – | device GND (**required**) |
| GPIO16 | A RX | device TX |
| GPIO17 | A TX | device RX (only needed for typing) |
| GPIO18 | B RX | device TX |
| GPIO19 | B TX | device RX |
| GPIO21 | C RX | device TX (channel C is receive only) |

- 3.3 V logic only. Never connect 5 V signals.
- Power the ESP32 from its own USB, not from the device.
- Default 115200 8N1 on all channels, changeable per channel.
- Channel C has no TX pin of its own. By default its keystrokes go out on
  TX B. You can change that under Settings → *keys to*.

## Build and first flash (USB)

```bash
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run -t upload --upload-port /dev/ttyUSB0
.venv/bin/pio device monitor -p /dev/ttyUSB0 -b 115200
```

At every boot the USB console prints the access point name
(`abus-uartmon-XXXX`) and its password. The password is generated on the
first boot and stored in the ESP32.

## Updates over WiFi

```bash
./update.sh [IP]          # IP is remembered after the first successful update
```

`update.sh` builds the firmware and pushes it with `POST /update` (user
`admin`, password = AP password). It takes the password from:

1. `~/.config/lufi-uartmon/appass`, or
2. the USB boot log, if the ESP32 is plugged in (then saved there with
   mode 600), or
3. a hidden prompt.

After the first time no cable is needed. If an update stops with
`HTTP 000`, the WiFi signal is weak: just run it again.

## WiFi

- **Home WiFi**: set under Settings. The ESP32 is then at its DHCP address
  or `http://abus-uartmon.local/`.
- **Access point** `abus-uartmon-XXXX` (WPA2, `http://192.168.4.1/`), in
  one of two modes:
  - *automatic* (default): the AP comes on 30 s after the home WiFi is lost
    or when none is set up. It goes off again once the home WiFi is back and
    nobody has used the AP for 60 s.
  - *always on*.

## Network access

| Access | Purpose |
|---|---|
| `http://<ip>/` | Monitor |
| `http://<ip>/term?ch=N` | web terminal (N = 0/1/2 for A/B/C) |
| `http://<ip>/settings` | settings |
| `http://<ip>/help` | quick guide |
| `telnet <ip> 2321` / `2322` / `2323` | interactive console A / B / C (quit: Ctrl-] then `quit`) |
| `nc <ip> 2301` … `2303` | raw byte stream of one channel |
| `<ip>:2300` | all channels as timestamped records (`tools/uartmon.py`) |
| `ws://<ip>/ws?ch=N[&from=SEQ]` | WebSocket of the web terminal |

### HTTP API

| Endpoint | |
|---|---|
| `GET /status` | JSON: WiFi, uptime, ring position `head`, and per channel: baud, `tx`, `tx_console` (terminals using that TX pin), `ctx`, pins, name, byte count |
| `GET /data?from=N` | binary records from sequence N (`N<0`: the last −N records). The `X-Next` header holds the next sequence. Record: `0xa5, ch, t_ms (u32 LE), len (u8 ≤ 120), data` |
| `GET /cfg?baud<n>=…&tx<n>=0/1&ctx<n>=-1/0/1&name<n>=…` | channel settings, saved in NVS. `ctx` is the target TX *channel* (−1 = read only), not an on/off switch |
| `GET /send?ch=0/1&text=…` or `&hex=aa55…` | send once (TX is driven only for this send) |
| `GET /ap?mode=auto/on` | access point mode |
| `POST /wifi` (`ssid`, `pass` or `forget=1`) | home WiFi, then restart. `pass` may also be a 64-hex PSK |
| `POST /update` (multipart `fw`, basic auth) | firmware update |
| `GET /selftest` | puts channel A in internal loopback for ~50 ms and sends sample frames |

The ring buffer holds about 160 KB shared by all channels. For long
captures use `nc` on the raw ports or `tools/uartmon.py`.

## PC logger

```bash
tools/uartmon.py [host[:port]] [-o DIR] [--all]
```

It connects to port 2300 and decodes all channels. It writes `decoded.log`
plus raw `chA/B/C.bin`. WiFi configuration frames (cmd `0xc0`) are shown as
lengths only and are zeroed in every file it writes.

## Privacy

The web view and `uartmon.py` hide the payload of WiFi configuration frames.
`/data`, the raw TCP ports and the terminal carry the bytes unchanged, so
treat their logs as private. `/status` shows the home WiFi *name*, never its
password.

## Project layout

| Path | |
|---|---|
| `src/main.cpp` | firmware (UARTs, ring buffer, web server, TCP, WebSocket, OTA) |
| `src/page.h` | web pages: Monitor and Settings (one page), Terminal, Help |
| `src/web_assets.h` | generated: gzip-compressed xterm.js, CSS, logo, favicon |
| `tools/embed_web.py` | regenerates `web_assets.h` from `web-vendor/` and `web/` |
| `tools/uartmon.py` | PC logger and decoder |
| `web-vendor/` | xterm.js 5.3.0 and xterm-addon-fit 0.8.0 (MIT, unpacked npm packages, sha512-checked) |
| `web/` | logo and favicon sources |
| `update.sh` | build and push over WiFi |

After changing `web-vendor/` or `web/`, regenerate the embedded assets:

```bash
python3 -I tools/embed_web.py web-vendor src/web_assets.h
```

The firmware uses about 96 % of the 1.25 MB app partition. Larger additions
need a different partition table, which has to be flashed once over USB.

## Third-party code

- [xterm.js](https://github.com/xtermjs/xterm.js) and xterm-addon-fit, MIT
  licence (see `web-vendor/*/LICENSE`).
- Arduino core for ESP32 / ESP-IDF, mbedTLS (via PlatformIO).
