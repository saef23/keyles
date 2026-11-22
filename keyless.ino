#include "WiFi.h"
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "index.h" // Memuat file HTML eksternal

// ==================== KONFIGURASI PIN ====================
const int PIN_RELAY_KEYLESS  = 2;
const int PIN_SIMULASI_GETAR = 4;
const int PIN_BUTTON_WAKEUP  = 13;
const int PIN_BUZZER         = 25;
const int PIN_VOLT_SENSOR    = 34;
const int PIN_LED_STATUS     = 14;

const int RST_PIN = 22;
const int SS_PIN  = 5;
MFRC522 mfrc522(SS_PIN, RST_PIN);

String SSIDkey[5];
String BSSIDkey[5]; // Disimpan bila ada, namun tidak membatasi pencocokan Wi-Fi
int RSSItresh[5];
int keynumber = 0;

String rfidKeyUID[5];
int rfidCount = 0;
bool rfidAktif = false;
bool rfidKontakState = false;

Preferences preferences;

unsigned long waktuMulaiHilang = 0;
bool sedangMencari = false;
bool berhentiTotalScan = false;
bool statusTerdeteksiSebelumnya = false;

unsigned long waktuMulaiStandbyRFID = 0;
bool sedangMenungguStandbyRFID = false;

// Mode wake up sementara setelah bangun / terputus Wi-Fi / dimatikan RFID sebelum ke standby
unsigned long waktuMasukModeWakeup = 0;
bool sedangDiModeWakeup = false;
const unsigned long TIMEOUT_WAKEUP_KE_STANDBY = 30000; // 30 detik batas pengaman

int hitungScanWakeup = 0;
const int MAKS_SCAN_WAKEUP = 3; // Maksimal 3 kali scan Wi-Fi saat wake up

int hitungGagalScanWiFi = 0;
const int BATAS_TOLERANSI_WIFI = 3;

unsigned long waktuGetarMulai = 0;
bool sedangMendeteksiGetar = false;
bool siapBangunGetar = false;

// Sensitivitas tersimpan di Preferences (dapat diubah lewat Web Config)
unsigned long durasiStabilGetarMs = 250; // ms (makin kecil makin responsif/sensitif)
unsigned long debounceTombolMs    = 60;  // ms
int jumlahTapConfigAP             = 3;   // 2 - 5 kali tekan

// ==================== SETELAN LED & BUZZER PER PROSES ====================
bool ledWakeup  = true,  buzWakeup  = true;
bool ledStandby = true,  buzStandby = true;
bool ledWifi    = true,  buzWifi    = true;
bool ledRfid    = true,  buzRfid    = true;
bool ledAp      = true,  buzAp      = true;
bool ledOli     = true,  buzOli     = true;

unsigned long totalJamKerjaMenit = 0;
unsigned long waktuMulaiMesinNyala = 0;
const unsigned long INTERVAL_GANTI_OLI_MENIT = 12000;
bool peringatanOliSudahBunyi = false;

float teganganAki = 0.0;

bool modeSettingWeb = false;
bool wifiApAktif = false;
bool sedangOTA = false;
unsigned long waktuMulaiAP = 0;
const unsigned long TIMEOUT_AP_AUTO_OFF = 120000;

int rfidTapCount = 0;
unsigned long waktuRfidTapPertama = 0;

// Variabel debounce tombol saat aktif
int buttonTapCount = 0;
unsigned long waktuButtonTapPertama = 0;
bool lastButtonReading = HIGH;
bool debouncedButtonState = HIGH;
unsigned long lastButtonChangeTime = 0;

// Variabel wake up tombol saat standby
unsigned long waktuMulaiTekanWakeup = 0;
bool sedangMenekanWakeup = false;

// Variabel indikator LED non-blocking
unsigned long lastLedToggle = 0;
bool ledState = LOW;

// ==================== LIVE SERIAL / EVENT LOG BUFFER ====================
const int MAX_LOG_LINES = 50;
String logBuffer[MAX_LOG_LINES];
int logHeadIndex = 0;
uint32_t totalLogCount = 0;

AsyncWebServer server(80);

// Deklarasi fungsi sebelum dipakai
void masukModeStandby();
void masukModeWakeup();
void keluarDariStandby();
void catetLog(const String &msg);
void updateIndikatorLED();

String formatWaktuSingkat() {
  unsigned long detik = millis() / 1000;
  unsigned long jam = (detik / 3600) % 100;
  unsigned long menit = (detik / 60) % 60;
  unsigned long dtk = detik % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", jam, menit, dtk);
  return String(buf);
}

void catetLog(const String &msg) {
  String line = "[" + formatWaktuSingkat() + "] " + msg;
  Serial.println(line);
  logBuffer[logHeadIndex] = line;
  logHeadIndex = (logHeadIndex + 1) % MAX_LOG_LINES;
  totalLogCount++;
}

String escapeJsonString(const String &s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '\"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

bool parseBoolParam(AsyncWebServerRequest *request, const char *name, bool currentVal) {
  if (!request->hasParam(name, true)) return currentVal;
  String v = request->getParam(name, true)->value();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "on") return true;
  if (v == "0" || v == "false" || v == "off") return false;
  return currentVal;
}

// ==================== HELPER NOTIFIKASI PER PROSES ====================
void bunyiPulsa(bool aktif, unsigned long durasiMs) {
  if (!aktif) return;
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durasiMs);
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiPola(bool aktif, int jumlah, unsigned long onMs, unsigned long offMs) {
  if (!aktif) return;
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(onMs);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < jumlah - 1) delay(offMs);
  }
}

void kedipEventLED(bool aktif, int jumlah, unsigned long onMs, unsigned long offMs) {
  if (!aktif) return;
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_LED_STATUS, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED_STATUS, LOW);
    if (i < jumlah - 1) delay(offMs);
  }
}

void updateIndikatorLED() {
  unsigned long sekarang = millis();

  // 4. Mode AP aktif -> kedip cepat (120 ms) jika ledAp aktif
  if (wifiApAktif) {
    if (!ledAp) {
      ledState = LOW;
      digitalWrite(PIN_LED_STATUS, LOW);
      return;
    }
    if (sekarang - lastLedToggle >= 120) {
      lastLedToggle = sekarang;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
    return;
  }

  // 1. Saat standby -> mati (LOW) atau kedip jika ledStandby aktif
  if (berhentiTotalScan) {
    if (!ledStandby) {
      ledState = LOW;
      digitalWrite(PIN_LED_STATUS, LOW);
      return;
    }
    if (sekarang - lastLedToggle >= 1500) {
      lastLedToggle = sekarang;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
    return;
  }

  // 3. Saat terhubung RFID / Wi-Fi / kontak ON -> mati (LOW)
  if (statusTerdeteksiSebelumnya || rfidKontakState) {
    ledState = LOW;
    digitalWrite(PIN_LED_STATUS, LOW);
    return;
  }

  // 2. Saat wake up / belum terhubung (cari Wi-Fi) -> kedip pelan (700 ms) bila ledWakeup aktif
  if (!ledWakeup) {
    ledState = LOW;
    digitalWrite(PIN_LED_STATUS, LOW);
    return;
  }
  if (sekarang - lastLedToggle >= 700) {
    lastLedToggle = sekarang;
    ledState = !ledState;
    digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
  }
}

void bacaTeganganAki() {
  int nilaiAnalog = analogRead(PIN_VOLT_SENSOR);
  teganganAki = (nilaiAnalog / 4095.0) * 3.3 * 5.0;
}

void aktifkanWiFiAP() {
  if (!wifiApAktif) {
    sedangDiModeWakeup = false;
    berhentiTotalScan = false;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Keyless-Config", "12345678");
    wifiApAktif = true;
    waktuMulaiAP = millis();
    lastLedToggle = millis();
    ledState = ledAp ? HIGH : LOW;
    digitalWrite(PIN_LED_STATUS, ledState);
    catetLog("AP Config aktif (SSID: ESP32-Keyless-Config)");

    kedipEventLED(ledAp, 2, 100, 100);
    bunyiPola(buzAp, 2, 100, 100);
  }
}

void matikanWiFiAP() {
  if (wifiApAktif && !sedangOTA) {
    WiFi.softAPdisconnect(true);
    if (!berhentiTotalScan) {
      WiFi.mode(WIFI_STA);
    } else {
      WiFi.mode(WIFI_OFF);
    }
    wifiApAktif = false;
    lastLedToggle = millis();
    updateIndikatorLED();
    catetLog("AP Config dimatikan");
  }
}

void loadConfigList() {
  preferences.begin("wifi_keys", true);
  keynumber = preferences.getInt("keynumber", 0);
  if (keynumber > 5) keynumber = 5;
  for (int i = 0; i < keynumber; ++i) {
    SSIDkey[i] = preferences.getString(("ssid" + String(i)).c_str(), "");
    BSSIDkey[i] = preferences.getString(("bssid" + String(i)).c_str(), "");
    RSSItresh[i] = preferences.getInt(("rssi" + String(i)).c_str(), -100);
  }
  rfidCount = preferences.getInt("rfid_count", 0);
  if (rfidCount > 5) rfidCount = 5;
  for (int i = 0; i < rfidCount; ++i) {
    rfidKeyUID[i] = preferences.getString(("rfid_" + String(i)).c_str(), "");
  }
  rfidAktif = (rfidCount > 0);
  totalJamKerjaMenit = preferences.getULong("oli_menit", 0);

  durasiStabilGetarMs = preferences.getULong("getar_ms", 250);
  debounceTombolMs    = preferences.getULong("btn_ms", 60);
  jumlahTapConfigAP   = preferences.getInt("btn_tap", 3);
  if (durasiStabilGetarMs < 50) durasiStabilGetarMs = 50;
  if (durasiStabilGetarMs > 2000) durasiStabilGetarMs = 2000;
  if (debounceTombolMs < 20) debounceTombolMs = 20;
  if (debounceTombolMs > 500) debounceTombolMs = 500;
  if (jumlahTapConfigAP < 2) jumlahTapConfigAP = 2;
  if (jumlahTapConfigAP > 5) jumlahTapConfigAP = 5;

  ledWakeup  = preferences.getBool("led_wu", true);
  buzWakeup  = preferences.getBool("buz_wu", true);
  ledStandby = preferences.getBool("led_sb", true);
  buzStandby = preferences.getBool("buz_sb", true);
  ledWifi    = preferences.getBool("led_wf", true);
  buzWifi    = preferences.getBool("buz_wf", true);
  ledRfid    = preferences.getBool("led_rf", true);
  buzRfid    = preferences.getBool("buz_rf", true);
  ledAp      = preferences.getBool("led_ap", true);
  buzAp      = preferences.getBool("buz_ap", true);
  ledOli     = preferences.getBool("led_ol", true);
  buzOli     = preferences.getBool("buz_ol", true);

  preferences.end();
}

void simpanJamOli() {
  preferences.begin("wifi_keys", false);
  preferences.putULong("oli_menit", totalJamKerjaMenit);
  preferences.end();
}

void simpanSensitivitas() {
  preferences.begin("wifi_keys", false);
  preferences.putULong("getar_ms", durasiStabilGetarMs);
  preferences.putULong("btn_ms", debounceTombolMs);
  preferences.putInt("btn_tap", jumlahTapConfigAP);
  preferences.end();
}

void simpanSetelanProses() {
  preferences.begin("wifi_keys", false);
  preferences.putBool("led_wu", ledWakeup);
  preferences.putBool("buz_wu", buzWakeup);
  preferences.putBool("led_sb", ledStandby);
  preferences.putBool("buz_sb", buzStandby);
  preferences.putBool("led_wf", ledWifi);
  preferences.putBool("buz_wf", buzWifi);
  preferences.putBool("led_rf", ledRfid);
  preferences.putBool("buz_rf", buzRfid);
  preferences.putBool("led_ap", ledAp);
  preferences.putBool("buz_ap", buzAp);
  preferences.putBool("led_ol", ledOli);
  preferences.putBool("buz_ol", buzOli);
  preferences.end();
}

void setupWebServer() {
  WiFi.mode(WIFI_STA);
  wifiApAktif = false;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("user", true) && request->hasParam("pass", true)) {
      String u = request->getParam("user", true)->value();
      String p = request->getParam("pass", true)->value();
      if (u == "admin" && p == "1234") {
        request->send(200, "text/plain", "OK");
      } else {
        request->send(401, "text/plain", "Gagal");
      }
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request){
    bacaTeganganAki();
    String json = "{\"total_menit\":" + String(totalJamKerjaMenit) +
                  ",\"tegangan\":" + String(teganganAki, 1) +
                  ",\"getar_ms\":" + String(durasiStabilGetarMs) +
                  ",\"btn_ms\":" + String(debounceTombolMs) +
                  ",\"btn_tap\":" + String(jumlahTapConfigAP) +
                  ",\"led_wakeup\":" + String(ledWakeup ? 1 : 0) +
                  ",\"buz_wakeup\":" + String(buzWakeup ? 1 : 0) +
                  ",\"led_standby\":" + String(ledStandby ? 1 : 0) +
                  ",\"buz_standby\":" + String(buzStandby ? 1 : 0) +
                  ",\"led_wifi\":" + String(ledWifi ? 1 : 0) +
                  ",\"buz_wifi\":" + String(buzWifi ? 1 : 0) +
                  ",\"led_rfid\":" + String(ledRfid ? 1 : 0) +
                  ",\"buz_rfid\":" + String(buzRfid ? 1 : 0) +
                  ",\"led_ap\":" + String(ledAp ? 1 : 0) +
                  ",\"buz_ap\":" + String(buzAp ? 1 : 0) +
                  ",\"led_oli\":" + String(ledOli ? 1 : 0) +
                  ",\"buz_oli\":" + String(buzOli ? 1 : 0) +
                  ",\"wifis\":[";
    for (int i = 0; i < keynumber; i++) {
      json += "{\"ssid\":\"" + escapeJsonString(SSIDkey[i]) + "\",\"bssid\":\"" + escapeJsonString(BSSIDkey[i]) + "\",\"rssi\":" + String(RSSItresh[i]) + "}";
      if (i < keynumber - 1) json += ",";
    }
    json += "],\"rfids\":[";
    for (int i = 0; i < rfidCount; i++) {
      json += "\"" + escapeJsonString(rfidKeyUID[i]) + "\"";
      if (i < rfidCount - 1) json += ",";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    uint32_t since = 0;
    if (request->hasParam("since")) {
      since = request->getParam("since")->value().toInt();
    }

    uint32_t oldestAvailable = (totalLogCount > (uint32_t)MAX_LOG_LINES)
                               ? (totalLogCount - (uint32_t)MAX_LOG_LINES)
                               : 0;
    if (since < oldestAvailable) {
      since = oldestAvailable;
    }

    String json = "{\"next\":" + String(totalLogCount) + ",\"lines\":[";
    bool first = true;
    for (uint32_t idx = since; idx < totalLogCount; idx++) {
      int slot = (int)(idx % (uint32_t)MAX_LOG_LINES);
      if (!first) json += ",";
      json += "\"" + escapeJsonString(logBuffer[slot]) + "\"";
      first = false;
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.on("/savesens", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("getar_ms", true) && request->hasParam("btn_ms", true) && request->hasParam("btn_tap", true)) {
      unsigned long g = request->getParam("getar_ms", true)->value().toInt();
      unsigned long b = request->getParam("btn_ms", true)->value().toInt();
      int t           = request->getParam("btn_tap", true)->value().toInt();

      if (g < 50) g = 50;     if (g > 2000) g = 2000;
      if (b < 20) b = 20;     if (b > 500) b = 500;
      if (t < 2) t = 2;       if (t > 5) t = 5;

      durasiStabilGetarMs = g;
      debounceTombolMs    = b;
      jumlahTapConfigAP   = t;
      simpanSensitivitas();
      catetLog("Sensitivitas disimpan: getar=" + String(g) + "ms, btn=" + String(b) + "ms, tap=" + String(t) + "x");

      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/saveproses", HTTP_POST, [](AsyncWebServerRequest *request){
    ledWakeup  = parseBoolParam(request, "led_wakeup",  ledWakeup);
    buzWakeup  = parseBoolParam(request, "buz_wakeup",  buzWakeup);
    ledStandby = parseBoolParam(request, "led_standby", ledStandby);
    buzStandby = parseBoolParam(request, "buz_standby", buzStandby);
    ledWifi    = parseBoolParam(request, "led_wifi",    ledWifi);
    buzWifi    = parseBoolParam(request, "buz_wifi",    buzWifi);
    ledRfid    = parseBoolParam(request, "led_rfid",    ledRfid);
    buzRfid    = parseBoolParam(request, "buz_rfid",    buzRfid);
    ledAp      = parseBoolParam(request, "led_ap",      ledAp);
    buzAp      = parseBoolParam(request, "buz_ap",      buzAp);
    ledOli     = parseBoolParam(request, "led_oli",     ledOli);
    buzOli     = parseBoolParam(request, "buz_oli",     buzOli);

    simpanSetelanProses();
    updateIndikatorLED();
    catetLog("Setelan LED & Buzzer per proses diperbarui");
    request->send(200, "text/plain", "OK");
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("rssi", true)) {
      if (keynumber >= 5) {
        request->send(200, "text/html", "<h3>Maksimal 5 WiFi! <a href='/'>Kembali</a></h3>");
        return;
      }
      String newSsid = request->getParam("ssid", true)->value();
      String newBssid = request->hasParam("bssid", true) ? request->getParam("bssid", true)->value() : "";
      newBssid.toUpperCase();
      int newRssi = request->getParam("rssi", true)->value().toInt();

      SSIDkey[keynumber] = newSsid;
      BSSIDkey[keynumber] = newBssid;
      RSSItresh[keynumber] = newRssi;

      preferences.begin("wifi_keys", false);
      preferences.putString(("ssid" + String(keynumber)).c_str(), newSsid);
      preferences.putString(("bssid" + String(keynumber)).c_str(), newBssid);
      preferences.putInt(("rssi" + String(keynumber)).c_str(), newRssi);
      keynumber++;
      preferences.putInt("keynumber", keynumber);
      preferences.end();

      catetLog("Wi-Fi disimpan: " + newSsid + " (min RSSI: " + String(newRssi) + " dBm)");
      request->send(200, "text/html", "<h3>WiFi Berhasil Disimpan! <a href='/'>Kembali</a></h3>");
    } else {
      request->send(400, "text/html", "<h3>Gagal menyimpan.</h3>");
    }
  });

  server.on("/scanrfid", HTTP_GET, [](AsyncWebServerRequest *request){
    if (rfidCount >= 5) {
      request->send(200, "text/html", "<h3>Maksimal 5 kartu! <a href='/'>Kembali</a></h3>");
      return;
    }
    modeSettingWeb = true;
    catetLog("Mode rekam kartu RFID baru aktif");
    request->send(200, "text/html", "<h3>Tempelkan kartu RFID baru ke modul... <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetoli", HTTP_GET, [](AsyncWebServerRequest *request){
    totalJamKerjaMenit = 0;
    peringatanOliSudahBunyi = false;
    simpanJamOli();
    catetLog("Timer oli direset ke 0");
    request->send(200, "text/html", "<h3>Timer Oli Direset! <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("wifi_keys", false);
    preferences.clear();
    preferences.end();
    catetLog("Factory reset konfigurasi, restart ESP32...");
    request->send(200, "text/html", "<h3>Data Dihapus! Restart ESP32...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    catetLog("Restart ESP32 diminta dari web");
    request->send(200, "text/html", "<h3>ESP32 Memulai ulang...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool updateSuccess = !Update.hasError() && Update.isFinished();
    catetLog(updateSuccess ? "OTA berhasil, restart..." : "OTA gagal!");
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", updateSuccess ?
      "<h3>Update Berhasil! Restart...</h3>" : "<h3>Gagal Update! <a href='/'>Kembali</a></h3>");
    response->addHeader("Connection", "close");
    request->send(response);
    sedangOTA = false;
    if (updateSuccess) {
      delay(1000);
      ESP.restart();
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if (!index) {
      sedangOTA = true;
      catetLog("OTA mulai: " + filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        catetLog("OTA Update.begin gagal");
      }
    }
    if (len && Update.isRunning()) {
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    }
    if (final) {
      if (Update.end(true)) {
        catetLog("OTA selesai & terverifikasi");
      } else {
        Update.printError(Serial);
        catetLog("OTA Update.end gagal");
      }
    }
  });

  server.begin();
}

// ==================== FUNGSI BUZZER, LED & MODE ====================
void bunyiWakeupTombol() {
  kedipEventLED(ledWakeup, 1, 60, 0);
  bunyiPulsa(buzWakeup, 60);
}

void bunyiWakeupGetar() {
  kedipEventLED(ledWakeup, 2, 60, 70)