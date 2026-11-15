#!/bin/bash
# Build the ESP32 monitor firmware and push it over WiFi (POST /update).
# The update password is the ESP32's AP password. It is taken from
#   1. ~/.config/lufi-uartmon/appass (saved on the first successful update),
#   2. the ESP32's USB boot log if it is plugged in (then saved, mode 600),
#   3. a hidden prompt.
# It is never printed.
#   ./update.sh [IP]   (IP is remembered in ~/.config/lufi-uartmon/ip)
set -e
cd "$(dirname "$0")"
IPF=~/.config/lufi-uartmon/ip
IP=${1:-${UARTMON_IP:-$(cat "$IPF" 2>/dev/null || echo abus-uartmon.local)}}
CONF=~/.config/lufi-uartmon/appass
.venv/bin/pio run -e esp32dev | tail -n 1
PASS=
[ -r "$CONF" ] && PASS=$(cat "$CONF")
if [ -z "$PASS" ] && [ -e /dev/ttyUSB0 ]; then
	PASS=$(.venv/bin/python -I -c "
import serial,time,re
s=serial.Serial('/dev/ttyUSB0',115200,timeout=0.2)
s.dtr=False;s.rts=True;time.sleep(0.1);s.rts=False
end=time.time()+6;buf=b''
while time.time()<end: buf+=s.read(4096)
m=re.search(rb'password \"([^\"]+)\"',buf);print(m.group(1).decode() if m else '')
" 2>/dev/null || true)
	[ -n "$PASS" ] && { echo "password read from USB console, waiting for WiFi ..."; sleep 8; }
fi
if [ -z "$PASS" ]; then
	echo "No saved password and no ESP32 on USB."
	echo "Plug the ESP32 in via USB once (the password is then read and saved),"
	read -rsp "or type the AP password (Enter = cancel): " PASS
	echo
	[ -n "$PASS" ] || exit 1
fi
OUT=$(curl -s -w $'\n%{http_code}' -u "admin:$PASS" \
	-F fw=@.pio/build/esp32dev/firmware.bin "http://$IP/update" || true)
CODE=${OUT##*$'\n'}
[ "$CODE" = 401 ] || printf '%s\n' "${OUT%$'\n'*}"
case "$CODE" in
200)	mkdir -p "$(dirname "$IPF")"; printf '%s\n' "$IP" > "$IPF"
	if [ ! -r "$CONF" ] || [ "$(cat "$CONF")" != "$PASS" ]; then
		mkdir -p "$(dirname "$CONF")"
		(umask 077; printf '%s' "$PASS" > "$CONF")
		echo "password saved to $CONF - next updates need no cable"
	fi ;;
401)	echo "wrong password"; [ -r "$CONF" ] && rm -f "$CONF" && echo "saved password removed"; exit 1 ;;
*)	echo "update failed (HTTP $CODE) - is the ESP32 reachable at $IP?"; exit 1 ;;
esac
