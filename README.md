# ESP32 Virtual Keyboard

USB HID keyboard controlled over WiFi. Board: **ESP32-S2 Mini** (the ESP32-C3 has no USB HID support).

Arduino IDE: board `LOLIN S2 Mini`, *USB Mode* = `USB-OTG (TinyUSB)`, core arduino-esp32 3.x. Sketch: `esp32-virtual-keyboard/`.

- First boot (or no/failed WiFi): AP `ESP-Keyboard-Setup` with captive portal – set SSID, WiFi password and optional WebUI password (login `admin`).
- Hold BOOT (GPIO0) for 3 s: erases WiFi and WebUI password, returns to the portal.
- WebUI tabs: Makro, Klawiatura, Harmonogram, Wejścia, Ustawienia (WiFi, WebUI password, NTP server + POSIX timezone, config download/upload).
- Harmonogram: cron triggers (`min hour day month weekday`, supports `*`, lists, ranges, steps) bound to macros; needs NTP time.
- Wejścia: GPIO3,5,7,9,11,12 are inputs with pull-up; macros can be triggered by falling/rising/any change, also of the keyboard Scroll Lock state (`scroll`).
- Outputs GPIO39,37,35,33,18,16 are set in macros with `pin39up` / `pin39down`.
- Run a macro: `http://<ip>/run/<name>`.

Macro format: one key or combo per line (`ctrl+c`, `alt+ctrl+del`), keys `ctrl sh alt win bs sp cr tab esc up down left right home end del ins pgup pgdn f1..f24 power sleep`, `pause N` seconds. Other longer lines without `+` are typed as text.
