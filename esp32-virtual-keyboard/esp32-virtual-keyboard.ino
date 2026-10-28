// Wirtualna klawiatura USB HID sterowana przez WiFi.
// Płytka: ESP32-S2 Mini (Arduino-ESP32 core 3.x). Tools -> USB Mode: "USB-OTG (TinyUSB)".
// Przycisk BOOT (GPIO0) przytrzymany 3 s przy działaniu: kasuje WiFi i hasło, uruchamia portal.

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDSystemControl.h>
#include <time.h>
#include "webui.h"

#define BUTTON_PIN 0
#define AP_NAME "ESP-Keyboard-Setup"
#define MACRO_DIR "/m"

const uint8_t IN_PINS[] = {3, 5, 7, 9, 11, 12};
const uint8_t OUT_PINS[] = {39, 37, 35, 33, 18, 16};
#define SCHED_FILE "/sched.txt"
#define INPUTS_FILE "/inputs.txt"
#define DEFAULT_NTP "pool.ntp.org"
#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

USBHIDKeyboard Keyboard;
USBHIDSystemControl SystemControl;
WebServer server(80);
DNSServer dns;
Preferences prefs;

bool apMode = false;
String webPw;

// ---------- makra ----------

struct KeyDef { const char *name; uint8_t code; };
const KeyDef KEYS[] = {
  {"ctrl", KEY_LEFT_CTRL}, {"sh", KEY_LEFT_SHIFT}, {"alt", KEY_LEFT_ALT}, {"win", KEY_LEFT_GUI},
  {"bs", KEY_BACKSPACE}, {"sp", ' '}, {"cr", KEY_RETURN}, {"tab", KEY_TAB}, {"esc", KEY_ESC},
  {"up", KEY_UP_ARROW}, {"down", KEY_DOWN_ARROW}, {"left", KEY_LEFT_ARROW}, {"right", KEY_RIGHT_ARROW},
  {"home", KEY_HOME}, {"end", KEY_END}, {"del", KEY_DELETE}, {"ins", KEY_INSERT},
  {"pgup", KEY_PAGE_UP}, {"pgdn", KEY_PAGE_DOWN},
};

// Zwraca kod klawisza lub 0, gdy token nie jest nazwą klawisza.
uint8_t keyCode(String t) {
  String l = t;
  l.toLowerCase();
  for (auto &k : KEYS) if (l == k.name) return k.code;
  if (l.length() >= 2 && l[0] == 'f') {
    int n = l.substring(1).toInt();
    if (n >= 1 && n <= 12 && l == String("f") + String(n)) return KEY_F1 + n - 1;
    if (n >= 13 && n <= 24 && l == String("f") + String(n)) return KEY_F13 + n - 13;
  }
  return 0;
}

void sendSystem(uint8_t v) {
  SystemControl.press(v);
  delay(100);
  SystemControl.release();
}

void delaySeconds(long s) {
  for (long i = 0; i < s * 10; i++) {
    delay(100);
    if (i % 10 == 0) yield();
  }
}

void runLine(String line) {
  line.trim();
  if (!line.length()) return;
  String l = line;
  l.toLowerCase();
  if (l.startsWith("pause ")) { delaySeconds(l.substring(6).toInt()); return; }
  if (l.startsWith("pin") && (l.endsWith("up") || l.endsWith("down"))) {
    bool up = l.endsWith("up");
    int n = l.substring(3, l.length() - (up ? 2 : 4)).toInt();
    for (uint8_t o : OUT_PINS) if (o == n) { digitalWrite(o, up ? HIGH : LOW); return; }
    return;
  }
  if (l == "power") { sendSystem(SYSTEM_CONTROL_POWER_OFF); return; }
  if (l == "sleep") { sendSystem(SYSTEM_CONTROL_STANDBY); return; }

  if (line.length() == 1) { Keyboard.write(line[0]); return; }
  uint8_t single = keyCode(line);
  bool combo = line.indexOf('+') > 0 && line.indexOf('+') < (int)line.length() - 1;
  if (!combo) {
    if (single) { Keyboard.write(single); }
    else Keyboard.print(line);  // zwykły tekst
    return;
  }
  int start = 0;
  while (start <= (int)line.length()) {
    int p = line.indexOf('+', start);
    if (p < 0) p = line.length();
    String tok = line.substring(start, p);
    if (tok.length() == 1) Keyboard.press(tok[0]);
    else if (uint8_t c = keyCode(tok)) Keyboard.press(c);
    start = p + 1;
    delay(10);
  }
  delay(30);
  Keyboard.releaseAll();
}

void runMacro(const String &body) {
  int s = 0;
  while (s < (int)body.length()) {
    int e = body.indexOf('\n', s);
    if (e < 0) e = body.length();
    String line = body.substring(s, e);
    line.replace("\r", "");
    runLine(line);
    s = e + 1;
    delay(20);
  }
}

bool validName(const String &n) {
  if (!n.length() || n.length() > 32) return false;
  for (char c : n) if (!isalnum(c) && c != '_' && c != '-') return false;
  return true;
}

String macroPath(const String &n) { return String(MACRO_DIR "/") + n + ".txt"; }

String readFile(const String &p) {
  File f = LittleFS.open(p, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

bool writeFile(const String &p, const String &d) {
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  size_t written = f.print(d);
  f.close();
  return written == d.length();
}

String listMacros() {
  String out;
  File dir = LittleFS.open(MACRO_DIR);
  if (!dir) return out;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    n = n.substring(n.lastIndexOf('/') + 1);
    if (n.endsWith(".txt")) out += n.substring(0, n.length() - 4) + "\n";
  }
  return out;
}

// ---------- konfiguracja w pliku ----------
// Format: linie "ssid=..", "pass=..", "webpw=..", potem "@@macro NAZWA" + treść + "@@end".

String exportConfig() {
  String s = "ssid=" + prefs.getString("ssid", "") + "\n";
  s += "pass=" + prefs.getString("pass", "") + "\n";
  s += "webpw=" + prefs.getString("webpw", "") + "\n";
  s += "ntp=" + prefs.getString("ntp", DEFAULT_NTP) + "\n";
  s += "tz=" + prefs.getString("tz", DEFAULT_TZ) + "\n";
  s += "@@sched\n" + readFile(SCHED_FILE) + "@@end\n";
  s += "@@inputs\n" + readFile(INPUTS_FILE) + "@@end\n";
  String names = listMacros();
  int p = 0;
  while (p < (int)names.length()) {
    int e = names.indexOf('\n', p);
    String n = names.substring(p, e);
    String b = readFile(macroPath(n));
    if (b.length() && !b.endsWith("\n")) b += "\n";
    s += "@@macro " + n + "\n" + b + "@@end\n";
    p = e + 1;
  }
  return s;
}

void importConfig(const String &d) {
  String curName;
  String body;
  bool inMacro = false;
  int blockKind = 0;  // 0 makro, 1 harmonogram, 2 wejścia
  int p = 0;
  while (p < (int)d.length()) {
    int e = d.indexOf('\n', p);
    if (e < 0) e = d.length();
    String line = d.substring(p, e);
    line.replace("\r", "");
    p = e + 1;
    if (inMacro) {
      if (line == "@@end") {
        if (blockKind == 1) writeFile(SCHED_FILE, body);
        else if (blockKind == 2) writeFile(INPUTS_FILE, body);
        else if (validName(curName)) writeFile(macroPath(curName), body);
        inMacro = false;
      } else if (blockKind || validName(curName)) {
        body += line + "\n";
      }
    } else if (line == "@@sched" || line == "@@inputs") {
      blockKind = line == "@@sched" ? 1 : 2;
      body = "";
      inMacro = true;
    } else if (line.startsWith("@@macro ")) {
      curName = line.substring(8);
      curName.trim();
      body = "";
      blockKind = 0;
      inMacro = true;  // nieprawidłowa nazwa jest pomijana przy @@end
    } else if (line.startsWith("ssid=")) prefs.putString("ssid", line.substring(5));
    else if (line.startsWith("pass=")) prefs.putString("pass", line.substring(5));
    else if (line.startsWith("webpw=")) prefs.putString("webpw", line.substring(6));
    else if (line.startsWith("ntp=")) prefs.putString("ntp", line.substring(4));
    else if (line.startsWith("tz=")) prefs.putString("tz", line.substring(3));
  }
}

// ---------- harmonogram (cron) i wejścia ----------
// /sched.txt:  "<cron 5 pól>|<makro>" w linii
// /inputs.txt: "<źródło>|<zbocze>|<makro>", źródło: 3 5 7 9 11 12 lub scroll,
//              zbocze: fall (1->0), rise (0->1), change

volatile bool scrollLock = false;

bool cronField(String f, int v, int lo, int hi) {
  int start = 0;
  while (start <= (int)f.length()) {
    int c = f.indexOf(',', start);
    if (c < 0) c = f.length();
    String part = f.substring(start, c);
    start = c + 1;
    int step = 1;
    int sl = part.indexOf('/');
    if (sl >= 0) { step = part.substring(sl + 1).toInt(); part = part.substring(0, sl); }
    if (step < 1) continue;
    int a = lo, b = hi;
    if (part != "*") {
      int d = part.indexOf('-');
      if (d >= 0) { a = part.substring(0, d).toInt(); b = part.substring(d + 1).toInt(); }
      else { a = part.toInt(); b = sl >= 0 ? hi : a; }
    }
    if (v >= a && v <= b && (v - a) % step == 0) return true;
  }
  return false;
}

bool cronValid(const String &expr) {
  int n = 0;
  for (int i = 0; i < (int)expr.length(); i++) {
    char c = expr[i];
    if (c == ' ') { if (i && expr[i - 1] != ' ') n++; }
    else if (!isdigit(c) && c != '*' && c != '/' && c != ',' && c != '-') return false;
  }
  if (expr.length() && expr[expr.length() - 1] != ' ') n++;
  return n == 5;
}

bool cronMatch(const String &expr, const struct tm &t) {
  String f[5];
  int n = 0;
  String e = expr;
  e.trim();
  while (e.length() && n < 5) {
    int sp = e.indexOf(' ');
    if (sp < 0) sp = e.length();
    f[n++] = e.substring(0, sp);
    e = e.substring(sp);
    e.trim();
  }
  if (n != 5) return false;
  bool dow = cronField(f[4], t.tm_wday, 0, 7) || (t.tm_wday == 0 && cronField(f[4], 7, 0, 7));
  return cronField(f[0], t.tm_min, 0, 59) && cronField(f[1], t.tm_hour, 0, 23) &&
         cronField(f[2], t.tm_mday, 1, 31) && cronField(f[3], t.tm_mon + 1, 1, 12) && dow;
}

String field(const String &line, int idx) {
  int s = 0;
  for (int i = 0; i < idx; i++) {
    s = line.indexOf('|', s);
    if (s < 0) return "";
    s++;
  }
  int e = line.indexOf('|', s);
  return line.substring(s, e < 0 ? line.length() : e);
}

void runNamed(const String &n) {
  if (validName(n) && LittleFS.exists(macroPath(n))) runMacro(readFile(macroPath(n)));
}

// Zwraca plik bez linii o danym indeksie (idx < 0: bez zmian).
String withoutLine(const String &data, int idx) {
  String out;
  int s = 0, i = 0;
  while (s < (int)data.length()) {
    int e = data.indexOf('\n', s);
    if (e < 0) e = data.length();
    if (i++ != idx) out += data.substring(s, e) + "\n";
    s = e + 1;
  }
  return out;
}

bool inputSourceValid(const String &src) {
  if (src == "scroll") return true;
  for (uint8_t p : IN_PINS) if (src == String(p)) return true;
  return false;
}

int readInput(const String &src) {
  if (src == "scroll") return scrollLock ? 1 : 0;
  return digitalRead(src.toInt());
}

String inputStates() {
  String s;
  for (uint8_t p : IN_PINS) s += String(p) + "=" + digitalRead(p) + "\n";
  s += "scroll=" + String(scrollLock ? 1 : 0) + "\n";
  return s;
}

void onKeyboardLed(void *, esp_event_base_t, int32_t, void *data) {
  auto *d = (arduino_usb_hid_keyboard_event_data_t *)data;
  scrollLock = d->leds & LED_SCROLLLOCK;
}

void pollInputs() {
  static int last[7];
  static bool init = false;
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < 20) return;
  lastPoll = millis();
  String srcs[7];
  for (int i = 0; i < 6; i++) srcs[i] = String(IN_PINS[i]);
  srcs[6] = "scroll";
  for (int i = 0; i < 7; i++) {
    int v = readInput(srcs[i]);
    if (!init) { last[i] = v; continue; }
    if (v == last[i]) continue;
    bool rising = v > last[i];
    last[i] = v;
    String data = readFile(INPUTS_FILE);
    int s = 0;
    while (s < (int)data.length()) {
      int e = data.indexOf('\n', s);
      if (e < 0) e = data.length();
      String line = data.substring(s, e);
      s = e + 1;
      if (field(line, 0) != srcs[i]) continue;
      String edge = field(line, 1);
      if (edge == "change" || (edge == "rise" && rising) || (edge == "fall" && !rising))
        runNamed(field(line, 2));
    }
  }
  init = true;
}

void pollSchedule() {
  static int lastMinute = -1;
  time_t now = time(nullptr);
  if (now < 1700000000) return;  // czas jeszcze niezsynchronizowany
  struct tm t;
  localtime_r(&now, &t);
  int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  if (key == lastMinute) return;
  lastMinute = key;
  String data = readFile(SCHED_FILE);
  int s = 0;
  while (s < (int)data.length()) {
    int e = data.indexOf('\n', s);
    if (e < 0) e = data.length();
    String line = data.substring(s, e);
    s = e + 1;
    if (cronMatch(field(line, 0), t)) runNamed(field(line, 1));
  }
}

// ---------- serwer ----------

bool authOk() {
  if (webPw.length() == 0) return true;
  if (server.authenticate("admin", webPw.c_str())) return true;
  server.requestAuthentication();
  return false;
}

void restartSoon() {
  delay(500);
  ESP.restart();
}

void setupRoutes() {
  if (apMode) {
    server.on("/setup", HTTP_POST, []() {
      if (!authOk()) return;
      prefs.putString("ssid", server.arg("ssid"));
      prefs.putString("pass", server.arg("pass"));
      prefs.putString("webpw", server.arg("webpw"));
      server.send(200, "text/plain; charset=utf-8", "Zapisano. Restart...");
      restartSoon();
    });
    server.onNotFound([]() {
      server.send_P(200, "text/html; charset=utf-8", PAGE_SETUP);
    });
    return;
  }

  server.on("/", HTTP_GET, []() {
    if (authOk()) server.send_P(200, "text/html; charset=utf-8", PAGE_MAIN);
  });
  server.on("/api/ssid", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain; charset=utf-8", prefs.getString("ssid", ""));
  });
  server.on("/api/macros", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain; charset=utf-8", listMacros());
  });
  server.on("/api/macro", HTTP_GET, []() {
    if (!authOk()) return;
    String n = server.arg("n");
    if (!validName(n)) { server.send(400, "text/plain", "bad name"); return; }
    server.send(200, "text/plain; charset=utf-8", readFile(macroPath(n)));
  });
  server.on("/api/macro", HTTP_POST, []() {
    if (!authOk()) return;
    String n = server.arg("n");
    if (!validName(n) || !writeFile(macroPath(n), server.arg("b"))) {
      server.send(400, "text/plain", "bad name");
      return;
    }
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/macro/del", HTTP_POST, []() {
    if (!authOk()) return;
    String n = server.arg("n");
    if (!validName(n)) { server.send(400, "text/plain", "bad name"); return; }
    LittleFS.remove(macroPath(n));
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/type", HTTP_POST, []() {
    if (!authOk()) return;
    String t = server.arg("t");
    server.send(200, "text/plain", "ok");
    Keyboard.print(t);
  });
  server.on("/api/settings", HTTP_POST, []() {
    if (!authOk()) return;
    prefs.putString("ssid", server.arg("ssid"));
    if (server.arg("pass").length()) prefs.putString("pass", server.arg("pass"));
    if (server.arg("ntp").length()) prefs.putString("ntp", server.arg("ntp"));
    if (server.arg("tz").length()) prefs.putString("tz", server.arg("tz"));
    if (server.arg("nopw") == "1") prefs.putString("webpw", "");
    else if (server.arg("webpw").length()) prefs.putString("webpw", server.arg("webpw"));
    server.send(200, "text/plain", "ok");
    restartSoon();
  });
  server.on("/api/sched", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain; charset=utf-8", readFile(SCHED_FILE));
  });
  server.on("/api/sched/add", HTTP_POST, []() {
    if (!authOk()) return;
    String c = server.arg("c"), m = server.arg("m");
    c.trim();
    if (!cronValid(c) || !validName(m)) { server.send(400, "text/plain", "bad"); return; }
    writeFile(SCHED_FILE, readFile(SCHED_FILE) + c + "|" + m + "\n");
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/sched/del", HTTP_POST, []() {
    if (!authOk()) return;
    writeFile(SCHED_FILE, withoutLine(readFile(SCHED_FILE), server.arg("i").toInt()));
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/inputs", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain; charset=utf-8", readFile(INPUTS_FILE));
  });
  server.on("/api/inputs/state", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain", inputStates());
  });
  server.on("/api/inputs/add", HTTP_POST, []() {
    if (!authOk()) return;
    String src = server.arg("s"), ed = server.arg("e"), m = server.arg("m");
    if (!inputSourceValid(src) || !(ed == "fall" || ed == "rise" || ed == "change") || !validName(m)) {
      server.send(400, "text/plain", "bad");
      return;
    }
    writeFile(INPUTS_FILE, readFile(INPUTS_FILE) + src + "|" + ed + "|" + m + "\n");
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/inputs/del", HTTP_POST, []() {
    if (!authOk()) return;
    writeFile(INPUTS_FILE, withoutLine(readFile(INPUTS_FILE), server.arg("i").toInt()));
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/ntp", HTTP_GET, []() {
    if (authOk())
      server.send(200, "text/plain; charset=utf-8",
                  prefs.getString("ntp", DEFAULT_NTP) + "\n" + prefs.getString("tz", DEFAULT_TZ));
  });
  server.on("/api/export", HTTP_GET, []() {
    if (authOk()) server.send(200, "text/plain; charset=utf-8", exportConfig());
  });
  server.on("/api/import", HTTP_POST, []() {
    if (!authOk()) return;
    importConfig(server.arg("d"));
    server.send(200, "text/plain", "ok");
    restartSoon();
  });
  // Bezpośrednie wywołanie makra: http://adres/run/NAZWA
  server.onNotFound([]() {
    if (!authOk()) return;
    String u = server.uri();
    if (!u.startsWith("/run/")) { server.send(404, "text/plain", "not found"); return; }
    String n = u.substring(5);
    if (!validName(n) || !LittleFS.exists(macroPath(n))) { server.send(404, "text/plain", "no macro"); return; }
    String body = readFile(macroPath(n));
    server.send(200, "text/plain", "ok");
    runMacro(body);
  });
}

void startPortal() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME);
  dns.start(53, "*", WiFi.softAPIP());
}

bool connectWifi(const String &ssid, const String &pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  for (uint8_t p : IN_PINS) pinMode(p, INPUT_PULLUP);
  for (uint8_t p : OUT_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, onKeyboardLed);
  Keyboard.begin();
  SystemControl.begin();
  USB.begin();
  LittleFS.begin(true);
  LittleFS.mkdir(MACRO_DIR);
  prefs.begin("kbd", false);

  String ssid = prefs.getString("ssid", "");
  webPw = prefs.getString("webpw", "");
  if (!ssid.length() || !connectWifi(ssid, prefs.getString("pass", ""))) startPortal();

  if (!apMode) configTzTime(prefs.getString("tz", DEFAULT_TZ).c_str(),
                            prefs.getString("ntp", DEFAULT_NTP).c_str());
  setupRoutes();
  server.begin();
}

void loop() {
  if (apMode) dns.processNextRequest();
  server.handleClient();
  if (!apMode) {
    pollInputs();
    pollSchedule();
  }

  static unsigned long pressed = 0;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!pressed) pressed = millis();
    if (millis() - pressed > 3000) {
      prefs.putString("ssid", "");
      prefs.putString("pass", "");
      prefs.putString("webpw", "");
      ESP.restart();
    }
  } else {
    pressed = 0;
  }
}
