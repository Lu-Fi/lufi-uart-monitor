/*
 * abus-uartmon - ESP32 WiFi UART monitor for the ABUS PPIC90520
 *
 * Listens (receive only by default) on three UARTs and makes them available
 * over WiFi:
 *   A  RX GPIO16  (TX GPIO17, off by default)  T30 -> XR871 link (T30 TX)
 *   B  RX GPIO18  (TX GPIO19, off by default)  XR871 -> T30 link (XR871 TX)
 *   C  RX GPIO21  (receive only)               pad "TX" on the power board (XR871 console?)
 * All channels 115200 8N1 by default, changeable on the web page. Every
 * channel is a 3.3 V input; connect GND of the camera to GND of the ESP32.
 *
 * Access
 *   http://<ip>/         live view (text / decoded frames), settings, WiFi
 *   tcp <ip>:2300        all channels, timestamped records (tools/uartmon.py)
 *   tcp <ip>:2301..2303  raw byte stream of A, B, C (e.g. "nc <ip> 2303");
 *                        bytes sent to 2301/2302 go out on TX A/B if enabled
 *   telnet <ip> 2321..2323  interactive console: shows A, B, C (with escape
 *                        sequences, so a shell or menu renders properly),
 *                        keystrokes go out on the TX channel set by
 *                        /cfg?ctx<n>= (defaults A->TX A, B->TX B, C->TX B, -1 =
 *                        read only). TX is switched on only at the first
 *                        keystroke and back off (high impedance) when the
 *                        last console using it disconnects, unless it is on
 *                        permanently via /cfg?tx<n>=1. Telnet runs in
 *                        character mode with remote echo; Enter is sent as CR.
 *   OTA                  ArduinoOTA on port 3232, password = AP password
 *                        (pio run -e ota -t upload)
 * WiFi: always an access point "abus-uartmon-XXXX" (WPA2, password generated
 * on first boot and printed on the USB console at every boot). Station mode
 * to the home network is configured on the web page; the credentials stay in
 * the ESP32's NVS and are never printed.
 *
 * Ring buffer: NSLOTS records of up to SLOT_DATA bytes, written by a UART
 * task, read by every client with its own cursor, so a slow client never
 * blocks reception; if it falls behind by more than the ring it skips ahead
 * and gets a "lost" record.
 */
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <mbedtls/version.h>

#include "page.h"
#include "web_assets.h"

#define NCH 3
#define SLOT_DATA 120
#define NSLOTS 640
#define MAX_CLIENTS 6
#define MUX_PORT 2300
#define CON_PORT 2320	/* telnet consoles on 2321..2323 */
#define MODE_CON 10	/* client mode MODE_CON + n: console of channel n */
#define REC_MAGIC 0xa5
#define REC_LOST 0xff
#define LED_PIN 2

struct Slot {
	uint32_t t_ms;
	uint8_t ch;
	uint8_t len;
	uint8_t d[SLOT_DATA];
};

struct ChanCfg {
	HardwareSerial *ser;
	int rx, tx;
	uint32_t baud;
	bool tx_on;
	uint32_t bytes;
	uint32_t overflows;
};

static ChanCfg chans[NCH] = {
	{ &Serial1, 16, 17, 115200, false, 0, 0 },
	{ &Serial2, 18, 19, 115200, false, 0, 0 },
	{ &Serial, 21, 1, 115200, false, 0, 0 },	/* UART0: TX stays on USB */
};

static Slot *ring;
static volatile uint32_t head;	/* next sequence number to write */
static Preferences prefs;
static WebServer web(80);
static WiFiServer mux_srv(MUX_PORT);
static WiFiServer raw_srv[NCH] = { WiFiServer(MUX_PORT + 1), WiFiServer(MUX_PORT + 2),
				   WiFiServer(MUX_PORT + 3) };
static WiFiServer con_srv[NCH] = { WiFiServer(CON_PORT + 1), WiFiServer(CON_PORT + 2),
				   WiFiServer(CON_PORT + 3) };
static int con_tx[NCH] = { 0, 1, 1 };	/* TX channel for console n, -1 = none */
static int tx_temp[NCH];		/* consoles that switched TX on */
static String ch_name[NCH];		/* user label per channel (NVS nm<n>) */
static String ap_ssid, ap_pass;
static bool sta_configured;
static bool ap_auto;		/* AP only while home WiFi is down (NVS apauto, default) */
static bool ap_running;
static uint32_t sta_seen_ms, ap_idle_ms;
static SemaphoreHandle_t cfg_lock;

struct TcpClient {
	WiFiClient c;
	int mode;	/* -1 mux, 0..NCH-1 raw channel, MODE_CON + n console */
	uint32_t cur;
	bool used;
	int armed;	/* console: TX channel it switched on, -1 none */
	uint8_t tstate;	/* console: telnet parser state */
	bool last_cr;
};
static TcpClient clients[MAX_CLIENTS];

/* ---------------------------------------------------------------- ring */

static void ring_put(uint8_t ch, const uint8_t *d, size_t n)
{
	while (n) {
		size_t k = n > SLOT_DATA ? SLOT_DATA : n;
		Slot *s = &ring[head % NSLOTS];

		s->t_ms = millis();
		s->ch = ch;
		s->len = k;
		memcpy(s->d, d, k);
		__sync_synchronize();
		head = head + 1;
		d += k;
		n -= k;
	}
}

/* copy slot seq into out; false if it was overwritten (reader too slow) */
static bool ring_get(uint32_t seq, Slot *out)
{
	if (head - seq > NSLOTS || seq >= head)
		return false;
	memcpy(out, &ring[seq % NSLOTS], sizeof(*out));
	__sync_synchronize();
	return head - seq <= NSLOTS;
}

static uint32_t ring_oldest(void)
{
	return head > NSLOTS ? head - NSLOTS + 8 : 0;
}

/* ---------------------------------------------------------------- UARTs */

/*
 * TX pin routing without touching the UART: switching TX by end()/begin()
 * dropped the device's echo arriving in that moment and pulled the line low
 * (the device saw a break and getty restarted the login). The UART keeps
 * running RX-only; its TX signal is just connected to / cut from the pin.
 * Not driven = input with the weak internal pull-up (~45 kOhm, < 0.1 mA,
 * cannot power anything, keeps the device's RX from floating into a break).
 */
static void tx_route(int i)
{
	ChanCfg *c = &chans[i];
	gpio_num_t p = (gpio_num_t)c->tx;

	if (i == 2)
		return;		/* UART0: TX stays on the USB bridge */
	if (c->tx_on || tx_temp[i] > 0) {
		esp_rom_gpio_pad_select_gpio(p);
		gpio_set_level(p, 1);
		gpio_set_direction(p, GPIO_MODE_OUTPUT);
		esp_rom_gpio_connect_out_signal(p, i == 0 ? U1TXD_OUT_IDX : U2TXD_OUT_IDX, false, false);
	} else {
		c->ser->flush();	/* let the last bytes leave first */
		gpio_set_level(p, 1);
		esp_rom_gpio_connect_out_signal(p, SIG_GPIO_OUT_IDX, false, false);
		gpio_set_direction(p, GPIO_MODE_INPUT);
		gpio_pullup_en(p);
	}
}

/* (re)start a channel's UART (baud change, boot); RX only, TX via tx_route() */
static void chan_apply(int i)
{
	ChanCfg *c = &chans[i];

	if (i == 2) {	/* UART0: keep TX on the USB bridge for the boot log */
		c->ser->updateBaudRate(c->baud);
		return;
	}
	c->ser->end();
	c->ser->setRxBufferSize(4096);
	c->ser->begin(c->baud, SERIAL_8N1, c->rx, -1);
	c->ser->setRxTimeout(2);
	gpio_pullup_en((gpio_num_t)c->rx);	/* idle high when unconnected */
	tx_route(i);
}

static void uart_task(void *)
{
	uint8_t buf[SLOT_DATA];

	for (;;) {
		bool any = false;

		for (int i = 0; i < NCH; i++) {
			HardwareSerial *s = chans[i].ser;
			int n;

			xSemaphoreTake(cfg_lock, portMAX_DELAY);
			while ((n = s->available()) > 0) {
				if (n > SLOT_DATA)
					n = SLOT_DATA;
				n = s->readBytes(buf, n);
				if (n <= 0)
					break;
				chans[i].bytes += n;
				ring_put(i, buf, n);
				any = true;
			}
			xSemaphoreGive(cfg_lock);
		}
		if (!any)
			vTaskDelay(1);
	}
}

/* ---------------------------------------------------------------- config */

static void load_cfg(void)
{
	char k[8];

	prefs.begin("uartmon", false);
	for (int i = 0; i < NCH; i++) {
		snprintf(k, sizeof(k), "baud%d", i);
		chans[i].baud = prefs.getUInt(k, 115200);
		snprintf(k, sizeof(k), "tx%d", i);
		chans[i].tx_on = i < 2 && prefs.getBool(k, false);
		snprintf(k, sizeof(k), "ctx%d", i);
		con_tx[i] = prefs.getInt(k, con_tx[i]);
		snprintf(k, sizeof(k), "nm%d", i);
		ch_name[i] = prefs.getString(k, "");
	}
	ap_pass = prefs.getString("appass", "");
	if (ap_pass.length() < 8) {
		static const char cs[] = "abcdefghjkmnpqrstuvwxyz23456789";

		ap_pass = "";
		for (int i = 0; i < 10; i++)
			ap_pass += cs[esp_random() % (sizeof(cs) - 1)];
		prefs.putString("appass", ap_pass);
	}
	sta_configured = prefs.getString("ssid", "").length() > 0;
	ap_auto = prefs.getBool("apauto", true);
}

static void save_chan(int i)
{
	char k[8];

	snprintf(k, sizeof(k), "baud%d", i);
	prefs.putUInt(k, chans[i].baud);
	snprintf(k, sizeof(k), "tx%d", i);
	prefs.putBool(k, chans[i].tx_on);
}

/* ---------------------------------------------------------------- web */

static void h_root(void)
{
	web.sendHeader("Cache-Control", "no-store");
	web.send_P(200, "text/html", PAGE_HTML);
}

static String jesc(const String &s)
{
	String o;

	for (size_t i = 0; i < s.length(); i++) {
		char ch = s[i];

		if (ch == '"' || ch == '\\')
			o += '\\';
		if ((uint8_t)ch >= 0x20)
			o += ch;
	}
	return o;
}

static void h_status(void)
{
	String j = "{\"ap\":\"" + ap_ssid + "\",\"sta\":" + (sta_configured ? "true" : "false");

	j += ",\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
	j += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
	/* network name only, the WiFi password is never sent */
	j += ",\"sta_ssid\":\"" + jesc(prefs.getString("ssid", "")) + "\"";
	j += String(",\"ap_on\":") + (ap_running ? "true" : "false") + ",\"ap_mode\":\"" +
	     (ap_auto && sta_configured ? "auto" : "on") + "\",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",\"ap_clients\":" +
	     String(ap_running ? WiFi.softAPgetStationNum() : 0);
	j += ",\"uptime\":" + String(millis() / 1000) + ",\"head\":" + String(head);
	j += ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"ch\":[";
	for (int i = 0; i < NCH; i++) {
		j += String(i ? "," : "") + "{\"baud\":" + chans[i].baud + ",\"tx\":" +
		     (chans[i].tx_on ? "true" : "false") + ",\"tx_console\":" + tx_temp[i] +
		     ",\"ctx\":" + con_tx[i] + ",\"rx_pin\":" + chans[i].rx +
		     ",\"tx_pin\":" + (i < 2 ? chans[i].tx : -1) + ",\"name\":\"" + ch_name[i] +
		     "\",\"bytes\":" + chans[i].bytes + "}";
	}
	web.send(200, "application/json", j + "]}");
}

/* binary records from ?from=N (N < 0: the last -N records), header X-Next */
static void h_data(void)
{
	long from = web.hasArg("from") ? web.arg("from").toInt() : -100;
	uint32_t h = head, seq, oldest = ring_oldest();
	static uint8_t out[64 * (SLOT_DATA + 7)];
	size_t o = 0;
	Slot s;

	if (from < 0)
		seq = h > (uint32_t)-from ? h + from : 0;
	else
		seq = (uint32_t)from;
	if (seq < oldest || seq > h)
		seq = oldest > h ? h : oldest;
	for (int n = 0; n < 64 && seq < h; n++, seq++) {
		if (!ring_get(seq, &s))
			continue;
		out[o++] = REC_MAGIC;
		out[o++] = s.ch;
		memcpy(out + o, &s.t_ms, 4);
		o += 4;
		out[o++] = s.len;
		memcpy(out + o, s.d, s.len);
		o += s.len;
	}
	web.sendHeader("X-Next", String(seq));
	web.sendHeader("Cache-Control", "no-store");
	web.send_P(200, "application/octet-stream", (const char *)out, o);
}

static void h_cfg(void)
{
	char k[8];

	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	for (int i = 0; i < NCH; i++) {
		bool changed = false;

		snprintf(k, sizeof(k), "baud%d", i);
		if (web.hasArg(k)) {
			long b = web.arg(k).toInt();

			if (b >= 1200 && b <= 3000000 && (uint32_t)b != chans[i].baud) {
				chans[i].baud = b;
				changed = true;
			}
		}
		snprintf(k, sizeof(k), "tx%d", i);
		if (i < 2 && web.hasArg(k)) {
			bool on = web.arg(k) == "1";

			if (on != chans[i].tx_on) {
				chans[i].tx_on = on;
				changed = true;
			}
		}
		if (changed) {
			chan_apply(i);
			save_chan(i);
		}
		snprintf(k, sizeof(k), "ctx%d", i);
		if (web.hasArg(k)) {
			long t = web.arg(k).toInt();

			if (t >= -1 && t < 2) {
				con_tx[i] = t;
				prefs.putInt(k, t);
			}
		}
		snprintf(k, sizeof(k), "name%d", i);
		if (web.hasArg(k)) {
			String in = web.arg(k), n;

			/* plain label only: it goes into JSON and HTML unescaped */
			for (size_t j = 0; j < in.length() && n.length() < 24; j++) {
				char ch = in[j];

				if ((uint8_t)ch >= 0x20 && !strchr("\"\\<>&'`", ch))
					n += ch;
			}
			n.trim();
			ch_name[i] = n;
			snprintf(k, sizeof(k), "nm%d", i);
			prefs.putString(k, n);
		}
	}
	xSemaphoreGive(cfg_lock);
	h_status();
}

/* ?ch=0|1&hex=414243 or &text=... ; TX is driven just for this send */
static void h_send(void)
{
	int ch = web.arg("ch").toInt();
	String hex = web.arg("hex");
	uint8_t b[256];
	size_t n = 0;

	if (ch < 0 || ch > 1) {
		web.send(400, "text/plain", "no TX pin on this channel\n");
		return;
	}
	if (hex.length()) {
		for (size_t i = 0; i + 1 < hex.length() && n < sizeof(b); i += 2)
			b[n++] = strtoul(hex.substring(i, i + 2).c_str(), NULL, 16);
	} else {
		String t = web.arg("text");

		n = t.length() < sizeof(b) ? t.length() : sizeof(b);
		memcpy(b, t.c_str(), n);
	}
	/* like a console keystroke: drive TX only for this send unless it is on */
	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	if (tx_temp[ch]++ == 0 && !chans[ch].tx_on)
		tx_route(ch);
	xSemaphoreGive(cfg_lock);
	chans[ch].ser->write(b, n);
	chans[ch].ser->flush();
	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	if (--tx_temp[ch] == 0 && !chans[ch].tx_on)
		tx_route(ch);
	xSemaphoreGive(cfg_lock);
	web.send(200, "text/plain", String(n) + " bytes sent\n");
}

/*
 * /selftest: channel A in internal UART loopback for a moment, sends a few
 * frames as the T30 would (heartbeat, marker, a WiFi config with dummy
 * values, sleep) plus a text line. Checks reception, ring, web and TCP
 * decoding without any wire. A camera on GPIO16 is not disturbed (TX pin is
 * not driven) but its bytes during these ~50 ms are mixed in.
 */
static size_t mkframe(uint8_t *f, uint8_t cmd, uint16_t par, const uint8_t *pl, uint16_t len, bool pad)
{
	size_t n = 12 + len;

	memset(f, 0, 64 > n ? 64 : n);
	f[0] = 0xaa; f[1] = 0x55; f[2] = 0xaa; f[3] = 0x55;
	f[4] = len & 0xff; f[5] = len >> 8; f[6] = cmd; f[7] = 1;
	f[10] = par & 0xff; f[11] = par >> 8;
	if (len)
		memcpy(f + 12, pl, len);
	return pad && n < 64 ? 64 : n;
}

static void h_selftest(void)
{
	uint8_t f[128], wc[98] = { 0 };
	static const uint8_t sleep_s[4] = { 8, 0, 0, 0 }, wake[4] = { 0x03, 0x01, 0, 0 };
	static const char ss[] = "DUMMYSSID", pw[] = "DUMMYPASS12";
	HardwareSerial *s = chans[0].ser;

	wc[0] = sizeof(ss) - 1;
	memcpy(wc + 1, ss, sizeof(ss) - 1);
	wc[33] = sizeof(pw) - 1;
	memcpy(wc + 34, pw, sizeof(pw) - 1);
	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	uart_set_loop_back(UART_NUM_1, true);
	xSemaphoreGive(cfg_lock);
	s->write(f, mkframe(f, 0xa9, 0, NULL, 0, true));
	s->write(f, mkframe(f, 0xf1, 0, (const uint8_t *)"uartmon selftest", 16, true));
	s->write(f, mkframe(f, 0xc0, 0, wc, sizeof(wc), true));
	s->write(f, mkframe(f, 0x85, 0, wake, 4, false));
	s->write(f, mkframe(f, 0x83, 0, sleep_s, 4, true));
	s->write((const uint8_t *)"selftest text line\r\n", 20);
	{	/* ANSI test: colour, bold, CR overwrite, backspace, UTF-8 */
		static const char a[] = "\x1b[1;32mgreen bold\x1b[0m \x1b[34mblue\x1b[0m "
					"\x1b[33;44myellow on blue\x1b[0m \x1b[38;5;208morange\x1b[0m\r\n"
					"progress 10%\rprogress 100% \xc3\xa4\xc3\xb6\xc3\xbc ok!\b.\r\n";

		s->write((const uint8_t *)a, sizeof(a) - 1);
	}
	s->flush();
	delay(30);
	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	uart_set_loop_back(UART_NUM_1, false);
	xSemaphoreGive(cfg_lock);
	web.send(200, "text/plain", "selftest frames sent on channel A\n");
}

static void ap_start(void)
{
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
	ap_running = true;
}

static void ap_stop(void)
{
	WiFi.softAPdisconnect(true);
	WiFi.mode(WIFI_STA);
	ap_running = false;
}

/* /ap?mode=auto|on: auto = the access point only while the home WiFi is not
 * connected (loop() switches it), on = always */
static void h_ap(void)
{
	ap_auto = web.arg("mode") != "on";
	prefs.putBool("apauto", ap_auto);
	if (!ap_running && !ap_auto)
		ap_start();
	ap_idle_ms = millis();	/* auto: an AP in use is dropped only after 60 s idle */
	web.send(200, "text/plain", ap_auto ? "access point only without home WiFi\n" : "access point always on\n");
}

static void h_wifi(void)
{
	String ssid = web.arg("ssid"), pass = web.arg("pass");

	if (web.hasArg("forget")) {
		prefs.remove("ssid");
		prefs.remove("pass");
	} else if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 ||
		   (pass.length() && pass.length() < 8)) {
		web.send(400, "text/plain", "invalid SSID or password\n");
		return;
	} else {
		prefs.putString("ssid", ssid);
		prefs.putString("pass", pass);
	}
	web.send(200, "text/plain", "saved, restarting\n");
	delay(500);
	ESP.restart();
}

/* ---------------------------------------------------------------- TCP */

static void accept_clients(WiFiServer &srv, int mode)
{
	WiFiClient c = srv.accept();

	if (!c)
		return;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (!clients[i].used) {
			clients[i].c = c;
			clients[i].c.setNoDelay(true);
			clients[i].mode = mode;
			clients[i].armed = -1;
			clients[i].tstate = 0;
			clients[i].last_cr = false;
			if (mode >= MODE_CON) {
				/* character mode; the camera side does the echo */
				static const uint8_t neg[] = { 255, 251, 1, 255, 251, 3, 255, 253, 3 };

				clients[i].c.write(neg, sizeof(neg));
			}
			/* new client gets the recent history first */
			clients[i].cur = head > 200 ? head - 200 : 0;
			if (clients[i].cur < ring_oldest())
				clients[i].cur = ring_oldest();
			clients[i].used = true;
			return;
		}
	}
	c.stop();
}

/* console TX: driven only while a console that typed is connected */
static void tx_release(int *armed)
{
	if (*armed < 0)
		return;
	xSemaphoreTake(cfg_lock, portMAX_DELAY);
	if (--tx_temp[*armed] == 0 && !chans[*armed].tx_on)
		tx_route(*armed);	/* last one: back to high impedance */
	xSemaphoreGive(cfg_lock);
	*armed = -1;
}

static void console_send(int *armed, int ch, const uint8_t *d, size_t n)
{
	int t = con_tx[ch];

	if (t < 0 || t >= 2)
		return;		/* read-only console */
	if (*armed != t) {	/* first keystroke: switch TX on */
		tx_release(armed);
		xSemaphoreTake(cfg_lock, portMAX_DELAY);
		if (tx_temp[t]++ == 0 && !chans[t].tx_on)
			tx_route(t);
		xSemaphoreGive(cfg_lock);
		*armed = t;
	}
	chans[t].ser->write(d, n);
}

static void client_close(TcpClient *cl)
{
	cl->c.stop();
	cl->used = false;
	tx_release(&cl->armed);
}

/* telnet input: strip negotiation, IAC IAC -> 0xff, CR LF / CR NUL -> CR;
 * returns the byte to send or -1 */
static int telnet_in(TcpClient *cl, uint8_t b)
{
	switch (cl->tstate) {
	case 1:			/* after IAC */
		if (b == 255) {
			cl->tstate = 0;
			return 255;
		}
		cl->tstate = b == 250 ? 3 : (b >= 251 ? 2 : 0);
		return -1;
	case 2:			/* option byte of WILL/WONT/DO/DONT */
		cl->tstate = 0;
		return -1;
	case 3:			/* subnegotiation until IAC SE */
		if (b == 255)
			cl->tstate = 4;
		return -1;
	case 4:
		cl->tstate = b == 240 ? 0 : 3;
		return -1;
	}
	if (b == 255) {
		cl->tstate = 1;
		return -1;
	}
	if (cl->last_cr && (b == 0 || b == '\n')) {
		cl->last_cr = false;
		return -1;
	}
	cl->last_cr = b == '\r';
	return b;
}

static void console_input(TcpClient *cl)
{
	uint8_t buf[64];
	size_t n = 0;
	while (cl->c.available() && n < sizeof(buf)) {
		int b = cl->c.read();

		if (b < 0)
			break;
		b = telnet_in(cl, (uint8_t)b);
		if (b >= 0)
			buf[n++] = (uint8_t)b;
	}
	if (n)
		console_send(&cl->armed, cl->mode - MODE_CON, buf, n);
}

static void serve_client(TcpClient *cl)
{
	static uint8_t out[16 * (2 * SLOT_DATA + 7)];	/* only loop() calls this */
	size_t o = 0;
	Slot s;
	bool con = cl->mode >= MODE_CON;
	int rch = con ? cl->mode - MODE_CON : cl->mode;

	if (!cl->c.connected()) {
		client_close(cl);
		return;
	}
	if (con) {
		console_input(cl);
	} else {
		/* bytes from the client go out on TX (raw A/B only, if enabled) */
		while (cl->c.available()) {
			int b = cl->c.read();

			if (cl->mode >= 0 && cl->mode < 2 && chans[cl->mode].tx_on && b >= 0)
				chans[cl->mode].ser->write((uint8_t)b);
		}
	}
	if (head - cl->cur > NSLOTS) {
		uint32_t skip = ring_oldest() - cl->cur;

		cl->cur = ring_oldest();
		if (cl->mode < 0) {
			out[o++] = REC_MAGIC;
			out[o++] = REC_LOST;
			memcpy(out + o, &skip, 4);
			o += 4;
			out[o++] = 0;
		}
	}
	for (int n = 0; n < 16 && cl->cur < head; n++, cl->cur++) {
		if (!ring_get(cl->cur, &s))
			continue;
		if (cl->mode < 0) {
			out[o++] = REC_MAGIC;
			out[o++] = s.ch;
			memcpy(out + o, &s.t_ms, 4);
			o += 4;
			out[o++] = s.len;
		} else if (s.ch != rch) {
			continue;
		}
		if (con) {	/* telnet: double 0xff */
			for (int k = 0; k < s.len; k++) {
				out[o++] = s.d[k];
				if (s.d[k] == 255)
					out[o++] = 255;
			}
			continue;
		}
		memcpy(out + o, s.d, s.len);
		o += s.len;
	}
	if (o && cl->c.write(out, o) != o)
		client_close(cl);
}

/* ------------------------------------------------- WebSocket (web terminal) */

/*
 * ws://<ip>/ws?ch=N (port 80, used by /term) or ws://<ip>:81/?ch=N - browser terminal of channel N (page /term). Server ->
 * browser: binary frames with the raw channel bytes; browser -> server: text
 * or binary frames with keystrokes, sent like a telnet console (same TX
 * arming rules).
 */
#define WS_PORT 81
#define WS_MAX 3
#define WS_RXBUF 1100

struct WsClient {
	WiFiClient c;
	bool used, open;
	int ch, armed;
	uint32_t cur, t0;
	size_t rn;
	uint8_t rx[WS_RXBUF];
};
static WiFiServer ws_srv(WS_PORT);
static WsClient wss[WS_MAX];

static void ws_close(WsClient *w)
{
	w->c.stop();
	w->used = false;
	tx_release(&w->armed);
}

static void ws_accept(WsClient *w, String key);

static bool ws_handshake(WsClient *w)
{
	while (w->c.available() && w->rn < WS_RXBUF - 1)
		w->rx[w->rn++] = w->c.read();
	w->rx[w->rn] = 0;
	char *h = (char *)w->rx, *e = strstr(h, "\r\n\r\n");
	if (!e) {
		if (w->rn >= WS_RXBUF - 1 || millis() - w->t0 > 5000)
			ws_close(w);
		return false;
	}
	char *q = strstr(h, "ch="), *k = strcasestr(h, "Sec-WebSocket-Key:");
	if (!k || strncmp(h, "GET ", 4)) {
		w->c.print("HTTP/1.1 400 Bad Request\r\n\r\n");
		ws_close(w);
		return false;
	}
	w->ch = q && q[3] >= '0' && q[3] < '0' + NCH ? q[3] - '0' : 2;
	k += 18;
	while (*k == ' ')
		k++;
	char *ke = strstr(k, "\r\n");

	ws_accept(w, String(k).substring(0, ke - k));
	return true;
}

/* answer the upgrade and start streaming (also used by /ws on port 80) */
static void ws_accept(WsClient *w, String key)
{
	static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	uint8_t sha[20];
	unsigned char acc[32];
	size_t alen = 0;

	key.trim();
	key += guid;
	mbedtls_sha1_context ctx;
	mbedtls_sha1_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
	mbedtls_sha1_starts(&ctx);
	mbedtls_sha1_update(&ctx, (const uint8_t *)key.c_str(), key.length());
	mbedtls_sha1_finish(&ctx, sha);
#else
	mbedtls_sha1_starts_ret(&ctx);
	mbedtls_sha1_update_ret(&ctx, (const uint8_t *)key.c_str(), key.length());
	mbedtls_sha1_finish_ret(&ctx, sha);
#endif
	mbedtls_sha1_free(&ctx);
	mbedtls_base64_encode(acc, sizeof(acc), &alen, sha, sizeof(sha));
	acc[alen] = 0;
	w->c.printf("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		    "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
	w->open = true;
	w->rn = 0;
	w->cur = head > 300 ? head - 300 : 0;	/* recent history first */
	if (w->cur < ring_oldest())
		w->cur = ring_oldest();
}

static void ws_send(WsClient *w, uint8_t op, const uint8_t *d, size_t n)
{
	uint8_t hd[4] = { (uint8_t)(0x80 | op) };
	size_t hn = 2;

	if (n < 126) {
		hd[1] = n;
	} else {
		hd[1] = 126;
		hd[2] = n >> 8;
		hd[3] = n & 0xff;
		hn = 4;
	}
	if (w->c.write(hd, hn) != hn || (n && w->c.write(d, n) != n))
		ws_close(w);
}

/* parse complete client frames in w->rx */
static void ws_input(WsClient *w)
{
	while (w->c.available() && w->rn < WS_RXBUF)
		w->rx[w->rn++] = w->c.read();
	for (;;) {
		if (w->rn < 2)
			return;
		uint8_t op = w->rx[0] & 0x0f;
		size_t len = w->rx[1] & 0x7f, hn = 2;

		if (len == 127 || !(w->rx[1] & 0x80)) {	/* too big / unmasked */
			ws_close(w);
			return;
		}
		if (len == 126) {
			if (w->rn < 4)
				return;
			len = (w->rx[2] << 8) | w->rx[3];
			hn = 4;
		}
		if (hn + 4 + len > WS_RXBUF) {
			ws_close(w);
			return;
		}
		if (w->rn < hn + 4 + len)
			return;
		uint8_t *m = w->rx + hn, *p = m + 4;

		for (size_t i = 0; i < len; i++)
			p[i] ^= m[i & 3];
		if (op == 8) {
			ws_send(w, 8, NULL, 0);
			ws_close(w);
			return;
		}
		if (op == 9)
			ws_send(w, 10, p, len);
		else if ((op == 1 || op == 2 || op == 0) && len)
			console_send(&w->armed, w->ch, p, len);
		if (!w->used)
			return;
		size_t used = hn + 4 + len;

		memmove(w->rx, w->rx + used, w->rn - used);
		w->rn -= used;
	}
}

static void ws_poll(void)
{
	static uint8_t out[16 * SLOT_DATA];
	WiFiClient c = ws_srv.accept();

	if (c) {
		int i;

		for (i = 0; i < WS_MAX && wss[i].used; i++)
			;
		if (i == WS_MAX) {
			c.stop();
		} else {
			WsClient *w = &wss[i];

			w->c = c;
			w->c.setNoDelay(true);
			w->used = true;
			w->open = false;
			w->armed = -1;
			w->rn = 0;
			w->t0 = millis();
		}
	}
	for (int i = 0; i < WS_MAX; i++) {
		WsClient *w = &wss[i];
		size_t o = 0;
		Slot s;

		if (!w->used)
			continue;
		if (!w->c.connected()) {
			ws_close(w);
			continue;
		}
		if (!w->open) {
			ws_handshake(w);
			continue;
		}
		ws_input(w);
		if (!w->used)
			continue;
		if (head - w->cur > NSLOTS)
			w->cur = ring_oldest();
		for (int n = 0; n < 16 && w->cur < head; n++, w->cur++) {
			if (!ring_get(w->cur, &s) || s.ch != w->ch)
				continue;
			memcpy(out + o, s.d, s.len);
			o += s.len;
		}
		if (o)
			ws_send(w, 2, out, o);
	}
}

/* /ws?ch=N on port 80: take the connection over from the WebServer (it only
 * drops its reference afterwards, the socket stays open in our copy) */
static void h_ws(void)
{
	int i;

	if (!web.hasHeader("Sec-WebSocket-Key") ||
	    !web.header("Upgrade").equalsIgnoreCase("websocket")) {
		web.send(400, "text/plain", "websocket only\n");
		return;
	}
	for (i = 0; i < WS_MAX && wss[i].used; i++)
		;
	if (i == WS_MAX) {
		web.send(503, "text/plain", "too many terminals\n");
		return;
	}
	WsClient *w = &wss[i];
	int ch = web.hasArg("ch") ? web.arg("ch").toInt() : 2;

	w->c = web.client();
	w->c.setNoDelay(true);
	w->used = true;
	w->armed = -1;
	w->rn = 0;
	w->t0 = millis();
	w->ch = ch >= 0 && ch < NCH ? ch : 2;
	ws_accept(w, web.header("Sec-WebSocket-Key"));
	/* ?from=N: start after the point where the user pressed Clear */
	if (web.hasArg("from")) {
		long f = web.arg("from").toInt();

		if (f > (long)w->cur && f <= (long)head)
			w->cur = f;
	}
}

/* POST /update (multipart file "fw"), HTTP basic auth admin:<AP password>:
 * firmware update pushed from the PC, no inbound connection to the PC needed
 *   curl -u admin:PASS -F fw=@.pio/build/esp32dev/firmware.bin http://<ip>/update */
static bool upd_ok;

static void h_update_done(void)
{
	if (!web.authenticate("admin", ap_pass.c_str()))
		return web.requestAuthentication();
	web.send(upd_ok ? 200 : 500, "text/plain",
		 upd_ok ? "update OK, restarting\n" : "update FAILED\n");
	if (upd_ok) {
		delay(500);
		ESP.restart();
	}
}

static void h_update_upload(void)
{
	HTTPUpload &u = web.upload();

	if (u.status == UPLOAD_FILE_START) {
		upd_ok = false;
		if (!web.authenticate("admin", ap_pass.c_str()))
			return;
		upd_ok = Update.begin(UPDATE_SIZE_UNKNOWN);
	} else if (u.status == UPLOAD_FILE_WRITE) {
		if (upd_ok && Update.write(u.buf, u.currentSize) != u.currentSize)
			upd_ok = false;
	} else if (u.status == UPLOAD_FILE_END) {
		if (upd_ok)
			upd_ok = Update.end(true);
	} else if (u.status == UPLOAD_FILE_ABORTED) {
		if (upd_ok)
			Update.abort();
		upd_ok = false;
	}
}

static void h_help(void)
{
	web.sendHeader("Cache-Control", "no-store");
	web.send_P(200, "text/html", HELP_HTML);
}

static void h_term(void)
{
	web.sendHeader("Cache-Control", "no-store");
	web.send_P(200, "text/html", TERM_HTML);
}

/* embedded xterm.js files, stored gzip-compressed */
static void h_asset(void)
{
	for (const WebAsset &a : WEB_ASSETS) {
		if (web.uri() == a.path) {
			web.sendHeader("Content-Encoding", "gzip");
			web.sendHeader("Cache-Control", "max-age=86400");
			web.send_P(200, a.type, (const char *)a.data, a.len);
			return;
		}
	}
	web.send(404, "text/plain", "not found\n");
}

/* ---------------------------------------------------------------- main */

void setup(void)
{
	uint8_t mac[6];

	pinMode(LED_PIN, OUTPUT);
	cfg_lock = xSemaphoreCreateMutex();
	ring = (Slot *)malloc(sizeof(Slot) * NSLOTS);
	load_cfg();

	/* UART0: boot log on USB TX, channel C received on GPIO21 */
	Serial.setRxBufferSize(4096);
	Serial.begin(chans[2].baud, SERIAL_8N1, chans[2].rx, 1);
	Serial.setRxTimeout(2);
	gpio_pullup_en((gpio_num_t)chans[2].rx);
	for (int i = 0; i < 2; i++)
		chan_apply(i);

	char sfx[8];

	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	snprintf(sfx, sizeof(sfx), "%02x%02x", mac[4], mac[5]);
	ap_ssid = String("abus-uartmon-") + sfx;
	if (ap_auto && sta_configured)
		WiFi.mode(WIFI_STA);	/* AP comes up only if the home WiFi fails */
	else
		ap_start();
	sta_seen_ms = millis();
	if (sta_configured) {
		WiFi.setHostname("abus-uartmon");
		WiFi.begin(prefs.getString("ssid", "").c_str(), prefs.getString("pass", "").c_str());
	}

	Serial.printf("\n\nabus-uartmon: AP \"%s\" password \"%s\" -> http://%s/\n", ap_ssid.c_str(),
		      ap_pass.c_str(), WiFi.softAPIP().toString().c_str());
	Serial.printf("abus-uartmon: station %s, ring %u x %u bytes, free heap %u\n",
		      sta_configured ? "configured" : "not configured", NSLOTS, SLOT_DATA,
		      (unsigned)ESP.getFreeHeap());
	Serial.printf("abus-uartmon: A=GPIO16 B=GPIO18 C=GPIO21 (RX), %u/%u/%u baud\n",
		      (unsigned)chans[0].baud, (unsigned)chans[1].baud, (unsigned)chans[2].baud);
	Serial.flush();

	MDNS.begin("abus-uartmon");
	MDNS.addService("http", "tcp", 80);
	web.on("/", h_root);
	web.on("/settings", h_root);	/* same page, settings view */
	web.on("/status", h_status);
	web.on("/data", h_data);
	web.on("/cfg", h_cfg);
	web.on("/term", h_term);
	web.on("/help", h_help);
	web.on("/ws", h_ws);
	web.on("/update", HTTP_POST, h_update_done, h_update_upload);
	{
		static const char *hk[] = { "Sec-WebSocket-Key", "Upgrade" };

		web.collectHeaders(hk, 2);
	}
	for (const WebAsset &a : WEB_ASSETS)
		web.on(a.path, h_asset);
	web.on("/send", h_send);
	web.on("/selftest", h_selftest);
	web.on("/wifi", HTTP_POST, h_wifi);
	web.on("/ap", h_ap);
	web.begin();
	mux_srv.begin();
	for (int i = 0; i < NCH; i++) {
		raw_srv[i].begin();
		con_srv[i].begin();
	}
	ws_srv.begin();

	ArduinoOTA.setHostname("abus-uartmon");
	ArduinoOTA.setPassword(ap_pass.c_str());
	ArduinoOTA.setMdnsEnabled(false);	/* MDNS already started above */
	ArduinoOTA.begin();

	xTaskCreatePinnedToCore(uart_task, "uart", 4096, NULL, 5, NULL, 1);
}

void loop(void)
{
	static uint32_t last_led, last_sta_log;
	static bool sta_up;

	web.handleClient();
	ArduinoOTA.handle();
	ws_poll();
	accept_clients(mux_srv, -1);
	for (int i = 0; i < NCH; i++) {
		accept_clients(raw_srv[i], i);
		accept_clients(con_srv[i], MODE_CON + i);
	}
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].used)
			serve_client(&clients[i]);

	if (sta_configured && (WiFi.status() == WL_CONNECTED) != sta_up) {
		sta_up = !sta_up;
		if (sta_up)
			Serial.printf("abus-uartmon: station up, http://%s/ (abus-uartmon.local)\n",
				      WiFi.localIP().toString().c_str());
		else if (millis() - last_sta_log > 10000)
			Serial.printf("abus-uartmon: station down\n");
		last_sta_log = millis();
	}
	/* auto access point: on once the home WiFi has been down for 30 s, off
	 * again when it is back and nobody has used the AP for 60 s */
	if (WiFi.status() == WL_CONNECTED)
		sta_seen_ms = millis();
	if (ap_auto && sta_configured) {
		if (!ap_running && millis() - sta_seen_ms > 30000) {
			ap_start();
			ap_idle_ms = millis();
			Serial.printf("abus-uartmon: home WiFi not connected, access point on\n");
		} else if (ap_running) {
			if (WiFi.status() != WL_CONNECTED || WiFi.softAPgetStationNum())
				ap_idle_ms = millis();
			else if (millis() - ap_idle_ms > 60000)
				ap_stop();
		}
	}
	/* LED: slow blink idle, fast flicker while data arrives */
	static uint32_t last_head;
	if (millis() - last_led > (head != last_head ? 50 : 500)) {
		last_led = millis();
		last_head = head;
		digitalWrite(LED_PIN, !digitalRead(LED_PIN));
	}
	delay(1);
}
