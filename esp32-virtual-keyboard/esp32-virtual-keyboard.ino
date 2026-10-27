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
#include "webui.h"

#define BUTTON_PIN 0
#define AP_NAME "ESP-Keyboard-Setup"
#define MACRO_DIR "/m"

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
    if (n >= 1 && n <= 12) return KEY_F1 + n - 1;
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
  f.print(d);
  f.close();
  return true;
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
  int p = 0;
  while (p < (int)d.length()) {
    int e = d.indexOf('\n', p);
    if (e < 0) e = d.length();
    String line = d.substring(p, e);
    line.replace("\r", "");
    p = e + 1;
    if (inMacro) {
      if (line == "@@end") { writeFile(macroPath(curName), body); inMacro = false; }
      else body += line + "\n";
    } else if (line.startsWith("@@macro ")) {
      curName = line.substring(8);
      curName.trim();
      body = "";
      inMacro = validName(curName);
      if (!inMacro) { /* pomiń nieprawidłową nazwę do @@end */ }
    } else if (line.startsWith("ssid=")) prefs.putString("ssid", line.substring(5));
    else if (line.startsWith("pass=")) prefs.putString("pass", line.substring(5));
    else if (line.startsWith("webpw=")) prefs.putString("webpw", line.substring(6));
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
    if (server.arg("nopw") == "1") prefs.putString("webpw", "");
    else if (server.arg("webpw").length()) prefs.putString("webpw", server.arg("webpw"));
    server.send(200, "text/plain", "ok");
    restartSoon();
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
  Keyboard.begin();
  SystemControl.begin();
  USB.begin();
  LittleFS.begin(true);
  LittleFS.mkdir(MACRO_DIR);
  prefs.begin("kbd", false);

  String ssid = prefs.getString("ssid", "");
  webPw = prefs.getString("webpw", "");
  if (!ssid.length() || !connectWifi(ssid, prefs.getString("pass", ""))) startPortal();

  setupRoutes();
  server.begin();
}

void loop() {
  if (apMode) dns.processNextRequest();
  server.handleClient();

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
