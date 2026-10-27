# ESP32 Virtual Keyboard

USB HID keyboard controlled over WiFi. Board: **ESP32-S2 Mini** (the ESP32-C3 has no USB HID support).

Arduino IDE: board `LOLIN S2 Mini`, *USB Mode* = `USB-OTG (TinyUSB)`, core arduino-esp32 3.x. Sketch: `esp32-virtual-keyboard/`.

- First boot (or no/failed WiFi): AP `ESP-Keyboard-Setup` with captive portal – set SSID, WiFi password and optional WebUI password (login `admin`).
- Hold BOOT (GPIO0) for 3 s: erases WiFi and WebUI password, returns to the portal.
- WebUI tabs: Makro, Klawiatura, Ustawienia (incl. config download/upload).
- Run a macro: `http://<ip>/run/<name>`.

Macro format: one key or combo per line (`ctrl+c`, `alt+ctrl+del`), keys `ctrl sh alt win bs sp cr tab esc up down left right home end del ins pgup pgdn f1..f12 power sleep`, `pause N` seconds. Other longer lines without `+` are typed as text.
