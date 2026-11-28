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

// ==================== PENGAMAN ANTI-MATI DI JALAN ====================
int hitungGagalScanWiFi = 0;
const int BATAS_TOLERANSI_WIFI = 3;
const int HYSTERESIS_RSSI_JALAN = 8;                      // Kelonggaran -8 dBm saat sudah terhubung
const unsigned long MULAI_PERINGATAN_HILANG_MS = 6000;    // 6 detik hilang -> mulai bunyi peringatan
const unsigned long TIMEOUT_PUTUS_KONTAK_MS    = 15000;   // 15 detik hilang terus -> baru matikan relay
unsigned long waktuBuzzerPeringatanTerakhir = 0;

// ==================== MODE TUNE-UP (BENGKEL / SERVIS) ====================
bool modeTuneUp = false;
unsigned long waktuMulaiTuneUp = 0;
const unsigned long TIMEOUT_TUNEUP_MS = 3600000; // Auto-off 60 menit (pengaman aki)
const unsigned long TAHAN_TOMBOL_TUNEUP_MS = 5000; // Tahan tombol 5 detik untuk toggle Tune-Up
unsigned long waktuMulaiTahanTombol = 0;
bool sedangMenahanTombolAktif = false;
bool tahanTombolSudahDipicu = false;

unsigned long waktuGetarMulai = 0;
unsigned long waktuGetarTerakhir = 0;
const unsigned long JEDA_TOLERANSI_GETAR_MS = 80;
bool sedangMendeteksiGetar = false;
bool siapBangunGetar = false;

// Sensitivitas tersimpan di Preferences (dapat diubah lewat Web Config)
unsigned long durasiStabilGetarMs = 250; // 50 - 2000 ms (makin kecil makin sensitif)
unsigned long debounceTombolMs    = 60;  // 20 - 500 ms
int jumlahTapConfigAP             = 3;   // 2 - 5 kali tekan

// ==================== SETELAN LED & BUZZER PER PROSES ====================
bool ledWakeup  = true,  buzWakeup  = true;
bool ledStandby = false, buzStandby = false;
bool ledWifi    = true,  buzWifi    = true;
bool ledRfid    = true,  buzRfid    = true;
bool ledAp      = true,  buzAp      = true;
bool ledOli     = true,  buzOli     = true;

// Interval LED (ms, 100 - 2000) & Durasi Buzzer (ms, 30 - 1000) per proses
unsigned long ledMsWakeup  = 700,  buzMsWakeup  = 60;
unsigned long ledMsStandby = 1500, buzMsStandby = 50;
unsigned long ledMsWifi    = 200,  buzMsWifi    = 80;
unsigned long ledMsRfid    = 200,  buzMsRfid    = 80;
unsigned long ledMsAp      = 120,  buzMsAp      = 100;
unsigned long ledMsOli     = 250,  buzMsOli     = 150;

// ==================== PENGHITUNG JAM KERJA MESIN (2 MODE) ====================
unsigned long totalJamKerjaMenit = 0;
unsigned long waktuMulaiMesinNyala = 0;
const unsigned long INTERVAL_GANTI_OLI_MENIT = 12000;
bool peringatanOliSudahBunyi = false;

const unsigned long MIN_MENIT_VALID = 5; // Batas minimal 5 menit baru dianggap pemakaian
unsigned long menitSesiIni = 0;          // Penghitung sementara sebelum tembus 5 menit
bool sesiSudahValid = false;

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
void updateJamOli();
void bunyiPeringatanOli();
void aktifkanModeTuneUp();
void matikanModeTuneUp();

bool belumAdaKunciTerdaftar() {
  return (keynumber == 0 && rfidCount == 0);
}

unsigned long clampDurasiBuzzer(unsigned long val) {
  if (val < 30) return 30;
  if (val > 1000) return 1000;
  return val;
}

unsigned long clampIntervalLed(unsigned long val) {
  if (val < 100) return 100;
  if (val > 2000) return 2000;
  return val;
}

unsigned long parseMsParam(AsyncWebServerRequest *request, const char *name, unsigned long currentVal, bool isBuzzer) {
  if (!request->hasParam(name, true)) return currentVal;
  unsigned long v = request->getParam(name, true)->value().toInt();
  return isBuzzer ? clampDurasiBuzzer(v) : clampIntervalLed(v);
}

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
  if (!aktif || sedangOTA) return;
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durasiMs);
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiPola(bool aktif, int jumlah, unsigned long onMs, unsigned long offMs) {
  if (!aktif || sedangOTA || jumlah <= 0) return;
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(onMs);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < jumlah - 1) delay(offMs);
  }
}

void kedipEventLED(bool aktif, int jumlah, unsigned long onMs, unsigned long offMs) {
  if (!aktif || jumlah <= 0) return;
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_LED_STATUS, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED_STATUS, LOW);
    if (i < jumlah - 1) delay(offMs);
  }
}

void updateIndikatorLED() {
  unsigned long sekarang = millis();

  // Mode Tune-Up aktif -> kedip santai tiap 1000 ms sebagai tanda kontak dikunci ON
  if (modeTuneUp) {
    if (sekarang - lastLedToggle >= 1000) {
      lastLedToggle = sekarang;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
    return;
  }

  // 4. Mode AP aktif -> kedip sesuai ledMsAp bila ledAp aktif
  if (wifiApAktif) {
    if (!ledAp) {
      ledState = LOW;
      digitalWrite(PIN_LED_STATUS, LOW);
      return;
    }
    unsigned long apStep = clampIntervalLed(ledMsAp);
    if (sekarang - lastLedToggle >= apStep) {
      lastLedToggle = sekarang;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
    return;
  }

  // 1. Saat standby -> mati (LOW) atau kedip sesuai ledMsStandby bila ledStandby aktif
  if (berhentiTotalScan) {
    if (!ledStandby) {
      ledState = LOW;
      digitalWrite(PIN_LED_STATUS, LOW);
      return;
    }
    unsigned long sbStep = clampIntervalLed(ledMsStandby);
    if (sekarang - lastLedToggle >= sbStep) {
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

  // 2. Saat wake up / belum terhubung (cari Wi-Fi) -> kedip sesuai ledMsWakeup bila ledWakeup aktif
  if (!ledWakeup) {
    ledState = LOW;
    digitalWrite(PIN_LED_STATUS, LOW);
    return;
  }
  unsigned long wuStep = clampIntervalLed(ledMsWakeup);
  if (sekarang - lastLedToggle >= wuStep) {
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

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    bool apOk = WiFi.softAP("ESP32-Keyless-Config", "12345678");
    delay(100);

    wifiApAktif = true;
    waktuMulaiAP = millis();
    lastLedToggle = millis();
    ledState = ledAp ? HIGH : LOW;
    digitalWrite(PIN_LED_STATUS, ledState);
    catetLog(apOk ? "AP Config aktif (SSID: ESP32-Keyless-Config)" : "AP Config gagal aktif");

    unsigned long dBuz = clampDurasiBuzzer(buzMsAp);
    unsigned long dLed = clampIntervalLed(ledMsAp);
    kedipEventLED(ledAp, 2, dLed, dLed);
    bunyiPola(buzAp, 2, dBuz, dBuz);
  }
}

void matikanWiFiAP() {
  if (belumAdaKunciTerdaftar() || modeTuneUp) return;
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

void aktifkanModeTuneUp() {
  if (modeTuneUp) return;
  modeTuneUp = true;
  waktuMulaiTuneUp = millis();
  berhentiTotalScan = false;
  sedangDiModeWakeup = false;
  sedangMencari = false;
  waktuMulaiHilang = 0;
  hitungGagalScanWiFi = 0;

  digitalWrite(PIN_RELAY_KEYLESS, HIGH);
  statusTerdeteksiSebelumnya = true;
  lastLedToggle = millis();
  ledState = HIGH;
  digitalWrite(PIN_LED_STATUS, HIGH);

  catetLog("MODE TUNE-UP AKTIF (Kontak dikunci ON, timer oli dijeda)");
  bunyiPola(true, 3, 70, 70);
}

void matikanModeTuneUp() {
  if (!modeTuneUp) return;
  modeTuneUp = false;
  waktuMulaiTuneUp = 0;
  waktuMulaiMesinNyala = millis();
  lastLedToggle = millis();
  updateIndikatorLED();
  catetLog("MODE TUNE-UP NONAKTIF (Kembali ke mode normal)");
  bunyiPulsa(true, 200);
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
  ledStandby = preferences.getBool("led_sb", false);
  buzStandby = preferences.getBool("buz_sb", false);
  ledWifi    = preferences.getBool("led_wf", true);
  buzWifi    = preferences.getBool("buz_wf", true);
  ledRfid    = preferences.getBool("led_rf", true);
  buzRfid    = preferences.getBool("buz_rf", true);
  ledAp      = preferences.getBool("led_ap", true);
  buzAp      = preferences.getBool("buz_ap", true);
  ledOli     = preferences.getBool("led_ol", true);
  buzOli     = preferences.getBool("buz_ol", true);

  ledMsWakeup  = clampIntervalLed(preferences.getULong("lms_wu", 700));
  buzMsWakeup  = clampDurasiBuzzer(preferences.getULong("bms_wu", 60));
  ledMsStandby = clampIntervalLed(preferences.getULong("lms_sb", 1500));
  buzMsStandby = clampDurasiBuzzer(preferences.getULong("bms_sb", 50));
  ledMsWifi    = clampIntervalLed(preferences.getULong("lms_wf", 200));
  buzMsWifi    = clampDurasiBuzzer(preferences.getULong("bms_wf", 80));
  ledMsRfid    = clampIntervalLed(preferences.getULong("lms_rf", 200));
  buzMsRfid    = clampDurasiBuzzer(preferences.getULong("bms_rf", 80));
  ledMsAp      = clampIntervalLed(preferences.getULong("lms_ap", 120));
  buzMsAp      = clampDurasiBuzzer(preferences.getULong("bms_ap", 100));
  ledMsOli     = clampIntervalLed(preferences.getULong("lms_ol", 250));
  buzMsOli     = clampDurasiBuzzer(preferences.getULong("bms_ol", 150));

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
  ledMsWakeup  = clampIntervalLed(ledMsWakeup);
  buzMsWakeup  = clampDurasiBuzzer(buzMsWakeup);
  ledMsStandby = clampIntervalLed(ledMsStandby);
  buzMsStandby = clampDurasiBuzzer(buzMsStandby);
  ledMsWifi    = clampIntervalLed(ledMsWifi);
  buzMsWifi    = clampDurasiBuzzer(buzMsWifi);
  ledMsRfid    = clampIntervalLed(ledMsRfid);
  buzMsRfid    = clampDurasiBuzzer(buzMsRfid);
  ledMsAp      = clampIntervalLed(ledMsAp);
  buzMsAp      = clampDurasiBuzzer(buzMsAp);
  ledMsOli     = clampIntervalLed(ledMsOli);
  buzMsOli     = clampDurasiBuzzer(buzMsOli);

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

  preferences.putULong("lms_wu", ledMsWakeup);
  preferences.putULong("bms_wu", buzMsWakeup);
  preferences.putULong("lms_sb", ledMsStandby);
  preferences.putULong("bms_sb", buzMsStandby);
  preferences.putULong("lms_wf", ledMsWifi);
  preferences.putULong("bms_wf", buzMsWifi);
  preferences.putULong("lms_rf", ledMsRfid);
  preferences.putULong("bms_rf", buzMsRfid);
  preferences.putULong("lms_ap", ledMsAp);
  preferences.putULong("bms_ap", buzMsAp);
  preferences.putULong("lms_ol", ledMsOli);
  preferences.putULong("bms_ol", buzMsOli);
  preferences.end();
}

void updateJamOli() {
  // Jeda penghitungan jam oli selama Mode Tune-Up aktif
  if (modeTuneUp) {
    waktuMulaiMesinNyala = millis();
    return;
  }

  bool kontakNyala = (digitalRead(PIN_RELAY_KEYLESS) == HIGH);

  if (kontakNyala) {
    // Cek peringatan ganti oli saat kontak ON
    if (totalJamKerjaMenit >= INTERVAL_GANTI_OLI_MENIT && !peringatanOliSudahBunyi) {
      catetLog("Peringatan ganti oli (>= 200 jam)");
      bunyiPeringatanOli();
      peringatanOliSudahBunyi = true;
    }

    // Hitung tiap 60.000 ms (1 menit) di RAM
    if (millis() - waktuMulaiMesinNyala >= 60000) {
      unsigned long selisihMenit = (millis() - waktuMulaiMesinNyala) / 60000;
      waktuMulaiMesinNyala += selisihMenit * 60000;

      if (!sesiSudahValid) {
        // MODE 1: Belum tembus 5 menit (tampung sementara di RAM)
        menitSesiIni += selisihMenit;
        if (menitSesiIni >= MIN_MENIT_VALID) {
          sesiSudahValid = true;
          totalJamKerjaMenit += menitSesiIni;
          menitSesiIni = 0;
          catetLog("Sesi >= 5 menit: jam oli mulai dihitung");
        }
      } else {
        // MODE 2: Sudah lewat 5 menit (langsung masuk hitungan di RAM)
        totalJamKerjaMenit += selisihMenit;
      }

      // Cek ulang jika batas 200 jam tercapai di tengah jalan
      if (totalJamKerjaMenit >= INTERVAL_GANTI_OLI_MENIT && !peringatanOliSudahBunyi) {
        catetLog("Peringatan ganti oli (>= 200 jam)");
        bunyiPeringatanOli();
        peringatanOliSudahBunyi = true;
      }
    }
  } else {
    // Saat kontak mati: simpan ke flash hanya jika sesi sempat tembus >= 5 menit
    if (sesiSudahValid) {
      simpanJamOli();
      catetLog("Jam oli disimpan ke flash: " + String(totalJamKerjaMenit) + " menit");
    }
    // Reset variabel sesi (kalau < 5 menit otomatis terbuang & tidak ditulis ke flash)
    menitSesiIni = 0;
    sesiSudahValid = false;
    waktuMulaiMesinNyala = millis();
  }
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
      if (u == "sikat miring" && p == "135790") {
        request->send(200, "text/plain", "OK");
      } else {
        request->send(401, "text/plain", "Gagal");
      }
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/tuneup", HTTP_POST, [](AsyncWebServerRequest *request){
    bool aktif = parseBoolParam(request, "aktif", !modeTuneUp);
    if (aktif) {
      aktifkanModeTuneUp();
    } else {
      matikanModeTuneUp();
    }
    String json = "{\"tuneup\":" + String(modeTuneUp ? 1 : 0) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request){
    bacaTeganganAki();
    String json = "{\"total_menit\":" + String(totalJamKerjaMenit) +
                  ",\"tegangan\":" + String(teganganAki, 1) +
                  ",\"tuneup\":" + String(modeTuneUp ? 1 : 0) +
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
                  ",\"lms_wakeup\":" + String(ledMsWakeup) +
                  ",\"bms_wakeup\":" + String(buzMsWakeup) +
                  ",\"lms_standby\":" + String(ledMsStandby) +
                  ",\"bms_standby\":" + String(buzMsStandby) +
                  ",\"lms_wifi\":" + String(ledMsWifi) +
                  ",\"bms_wifi\":" + String(buzMsWifi) +
                  ",\"lms_rfid\":" + String(ledMsRfid) +
                  ",\"bms_rfid\":" + String(buzMsRfid) +
                  ",\"lms_ap\":" + String(ledMsAp) +
                  ",\"bms_ap\":" + String(buzMsAp) +
                  ",\"lms_oli\":" + String(ledMsOli) +
                  ",\"bms_oli\":" + String(buzMsOli) +
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

    ledMsWakeup  = parseMsParam(request, "lms_wakeup",  ledMsWakeup,  false);
    buzMsWakeup  = parseMsParam(request, "bms_wakeup",  buzMsWakeup,  true);
    ledMsStandby = parseMsParam(request, "lms_standby", ledMsStandby, false);
    buzMsStandby = parseMsParam(request, "bms_standby", buzMsStandby, true);
    ledMsWifi    = parseMsParam(request, "lms_wifi",    ledMsWifi,    false);
    buzMsWifi    = parseMsParam(request, "bms_wifi",    buzMsWifi,    true);
    ledMsRfid    = parseMsParam(request, "lms_rfid",    ledMsRfid,    false);
    buzMsRfid    = parseMsParam(request, "bms_rfid",    buzMsRfid,    true);
    ledMsAp      = parseMsParam(request, "lms_ap",      ledMsAp,      false);
    buzMsAp      = parseMsParam(request, "bms_ap",      buzMsAp,      true);
    ledMsOli     = parseMsParam(request, "lms_oli",     ledMsOli,     false);
    buzMsOli     = parseMsParam(request, "bms_oli",     buzMsOli,     true);

    simpanSetelanProses();
    lastLedToggle = millis();
    updateIndikatorLED();
    catetLog("Setelan LED & Buzzer per proses disimpan");
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
    menitSesiIni = 0;
    sesiSudahValid = false;
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
  unsigned long dBuz = clampDurasiBuzzer(buzMsWakeup);
  unsigned long dLed = clampIntervalLed(ledMsWakeup);
  kedipEventLED(ledWakeup, 1, dLed, 0);
  bunyiPulsa(buzWakeup, dBuz);
}

void bunyiWakeupGetar() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsWakeup);
  unsigned long dLed = clampIntervalLed(ledMsWakeup);
  kedipEventLED(ledWakeup, 2, dLed, dLed);
  bunyiPola(buzWakeup, 2, dBuz, dBuz);
}

void bunyiStandby() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsStandby);
  unsigned long dLed = clampIntervalLed(ledMsStandby);
  kedipEventLED(ledStandby, 2, dLed, dLed);
  bunyiPola(buzStandby, 3, dBuz, dBuz);
}

void bunyiWifiTerhubung() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsWifi);
  unsigned long dLed = clampIntervalLed(ledMsWifi);
  kedipEventLED(ledWifi, 2, dLed, dLed);
  bunyiPola(buzWifi, 2, dBuz, dBuz);
}

void bunyiWifiTerputus() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsWifi);
  unsigned long dLed = clampIntervalLed(ledMsWifi);
  kedipEventLED(ledWifi, 1, dLed, 0);
  bunyiPulsa(buzWifi, dBuz);
}

void bunyiBuzzerBuka() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsRfid);
  unsigned long dLed = clampIntervalLed(ledMsRfid);
  kedipEventLED(ledRfid, 2, dLed, dLed);
  bunyiPola(buzRfid, 2, dBuz, dBuz);
}

void bunyiBuzzerKunci() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsRfid);
  unsigned long dLed = clampIntervalLed(ledMsRfid);
  kedipEventLED(ledRfid, 1, dLed, 0);
  bunyiPulsa(buzRfid, dBuz);
}

void bunyiPeringatanOli() {
  unsigned long dBuz = clampDurasiBuzzer(buzMsOli);
  unsigned long dLed = clampIntervalLed(ledMsOli);
  kedipEventLED(ledOli, 4, dLed, dLed);
  bunyiPola(buzOli, 4, dBuz, dBuz);
}

void masukModeStandby() {
  if (sedangOTA || modeTuneUp) return;

  if (belumAdaKunciTerdaftar()) {
    aktifkanWiFiAP();
    return;
  }

  sedangDiModeWakeup = false;
  sedangMenungguStandbyRFID = false;
  berhentiTotalScan = true;
  siapBangunGetar = false;
  sedangMendeteksiGetar = false;
  waktuGetarTerakhir = 0;
  sedangMenekanWakeup = false;
  sedangMencari = false;
  statusTerdeteksiSebelumnya = false;
  rfidKontakState = false;
  hitungGagalScanWiFi = 0;
  hitungScanWakeup = 0;
  waktuMulaiHilang = 0;

  digitalWrite(PIN_RELAY_KEYLESS, LOW);
  updateJamOli();
  matikanWiFiAP();
  WiFi.mode(WIFI_OFF);
  mfrc522.PCD_SoftPowerDown();

  ledState = LOW;
  digitalWrite(PIN_LED_STATUS, LOW);
  catetLog("Masuk STANDBY (Wi-Fi scan & RFID OFF)");
  bunyiStandby();
}

void masukModeWakeup() {
  if (belumAdaKunciTerdaftar()) {
    aktifkanWiFiAP();
    return;
  }

  berhentiTotalScan = false;
  sedangMenungguStandbyRFID = false;
  siapBangunGetar = false;
  sedangMendeteksiGetar = false;
  waktuGetarTerakhir = 0;
  sedangMenekanWakeup = false;
  statusTerdeteksiSebelumnya = false;
  rfidKontakState = false;
  hitungGagalScanWiFi = 0;
  hitungScanWakeup = 0;
  sedangMencari = false;
  waktuMulaiHilang = 0;

  sedangDiModeWakeup = true;
  waktuMasukModeWakeup = millis();

  lastLedToggle = millis();
  ledState = ledWakeup ? HIGH : LOW;
  digitalWrite(PIN_LED_STATUS, ledState);

  mfrc522.PCD_SoftPowerUp();
  delay(30);
  mfrc522.PCD_Init();
  WiFi.mode(WIFI_STA);
  catetLog("Masuk WAKE UP (maks 3x scan Wi-Fi)");
}

void keluarDariStandby() {
  masukModeWakeup();
  delay(500);
}

void runDetected() {
  sedangDiModeWakeup = false;
  hitungScanWakeup = 0;
  waktuMulaiHilang = 0;
  sedangMencari = false;
  digitalWrite(PIN_RELAY_KEYLESS, HIGH);
  digitalWrite(PIN_LED_STATUS, LOW);

  if (!statusTerdeteksiSebelumnya) {
    catetLog("Wi-Fi cocok -> Relay ON");
    bunyiWifiTerhubung();
    statusTerdeteksiSebelumnya = true;
    waktuMulaiMesinNyala = millis();
  }
}

void runNotDetected() {
  if (modeTuneUp) return;
  digitalWrite(PIN_RELAY_KEYLESS, LOW);
  if (statusTerdeteksiSebelumnya) {
    catetLog("Wi-Fi hilang >15 detik -> Relay OFF, masuk WAKE UP");
    bunyiWifiTerputus();
    statusTerdeteksiSebelumnya = false;
    waktuMulaiHilang = 0;
    sedangMencari = false;
    updateJamOli();
    matikanWiFiAP();
    masukModeWakeup();
  }
}

bool cekRFID() {
  if (berhentiTotalScan) return false;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return false;

  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  content.toUpperCase();
  String scannedUID = content.substring(1);

  if (modeSettingWeb) {
    if (rfidCount < 5) {
      rfidKeyUID[rfidCount] = scannedUID;
      rfidCount++;
      rfidAktif = true;
      preferences.begin("wifi_keys", false);
      preferences.putString(("rfid_" + String(rfidCount - 1)).c_str(), scannedUID);
      preferences.putInt("rfid_count", rfidCount);
      preferences.end();
      catetLog("Kartu RFID baru tersimpan: " + scannedUID);
      unsigned long dBuz = clampDurasiBuzzer(buzMsRfid);
      unsigned long dLed = clampIntervalLed(ledMsRfid);
      kedipEventLED(ledRfid, 1, dLed, 0);
      bunyiPulsa(buzRfid, dBuz);
    }
    modeSettingWeb = false;
    mfrc522.PICC_HaltA();
    return true;
  }

  bool kartuCocok = false;
  for (int i = 0; i < rfidCount; i++) {
    if (scannedUID == rfidKeyUID[i]) {
      kartuCocok = true;
      break;
    }
  }

  if (rfidAktif && kartuCocok) {
    catetLog("RFID UID " + scannedUID + " cocok");
    if (rfidKontakState || modeTuneUp) {
      if (modeTuneUp) matikanModeTuneUp();
      rfidKontakState = false;
      digitalWrite(PIN_RELAY_KEYLESS, LOW);
      updateJamOli();
      catetLog("Kontak RFID dimatikan");
      bunyiBuzzerKunci();
      matikanWiFiAP();
      masukModeWakeup();
      mfrc522.PICC_HaltA();
      return true;
    }

    if (statusTerdeteksiSebelumnya) {
      unsigned long sekarang = millis();
      if (sekarang - waktuRfidTapPertama > 2500) rfidTapCount = 0;
      if (rfidTapCount == 0) waktuRfidTapPertama = sekarang;
      rfidTapCount++;
      if (rfidTapCount >= 3) {
        rfidTapCount = 0;
        aktifkanWiFiAP();
        mfrc522.PICC_HaltA();
        return true;
      }
    }

    rfidKontakState = true;
    sedangDiModeWakeup = false;
    hitungScanWakeup = 0;
    digitalWrite(PIN_RELAY_KEYLESS, HIGH);
    digitalWrite(PIN_LED_STATUS, LOW);
    catetLog("Kontak RFID aktif -> Relay ON");
    bunyiBuzzerBuka();
    waktuMulaiMesinNyala = millis();
    berhentiTotalScan = false;
    sedangMenungguStandbyRFID = false;
    siapBangunGetar = false;
    hitungGagalScanWiFi = 0;
    waktuMulaiHilang = 0;
    sedangMencari = false;
    statusTerdeteksiSebelumnya = true;

    mfrc522.PICC_HaltA();
    return true;
  } else {
    catetLog("RFID UID " + scannedUID + " tidak dikenal");
    unsigned long dBuz = clampDurasiBuzzer(buzMsRfid);
    unsigned long dLed = clampIntervalLed(ledMsRfid);
    kedipEventLED(ledRfid, 2, dLed, dLed);
    bunyiPola(buzRfid, 2, dBuz, dBuz);
  }

  mfrc522.PICC_HaltA();
  return false;
}

void detectwifi() {
  if (berhentiTotalScan || rfidKontakState || sedangMenungguStandbyRFID || sedangOTA || modeTuneUp) return;

  if (belumAdaKunciTerdaftar()) {
    aktifkanWiFiAP();
    return;
  }

  updateIndikatorLED();
  int n = WiFi.scanNetworks();
  bool found = false;

  // Saat sudah terhubung, beri kelonggaran hysteresis -8 dBm agar tidak mudah putus saat HP di saku
  int kelonggaranRssi = statusTerdeteksiSebelumnya ? HYSTERESIS_RSSI_JALAN : 0;

  for (int i = 0; i < n; ++i) {
    String scannedSSID = WiFi.SSID(i);
    int scannedRSSI = WiFi.RSSI(i);

    for (int j = 0; j < keynumber; ++j) {
      if (scannedSSID == SSIDkey[j] && scannedRSSI >= (RSSItresh[j] - kelonggaranRssi)) {
        found = true;
        break;
      }
    }
    if (found) break;
  }
  WiFi.scanDelete();

  if (found) {
    if (sedangDiModeWakeup) {
      catetLog("Scan Wi-Fi #" + String(hitungScanWakeup + 1) + " -> COCOK (SSID & RSSI)");
    } else if (sedangMencari) {
      catetLog("Sinyal Wi-Fi kembali stabil, kontak tetap ON");
    }
    sedangDiModeWakeup = false;
    hitungScanWakeup = 0;
    hitungGagalScanWiFi = 0;
    waktuMulaiHilang = 0;
    sedangMencari = false;
    runDetected();
  } else {
    if (sedangDiModeWakeup) {
      hitungScanWakeup++;
      catetLog("Scan Wi-Fi wake-up #" + String(hitungScanWakeup) + "/" + String(MAKS_SCAN_WAKEUP) + " -> tidak ada");
      if (hitungScanWakeup >= MAKS_SCAN_WAKEUP && !rfidKontakState && !wifiApAktif && !modeSettingWeb && !sedangOTA) {
        catetLog("3x scan wake-up selesai tanpa hasil -> STANDBY");
        masukModeStandby();
        return;
      }
    } else if (statusTerdeteksiSebelumnya) {
      unsigned long sekarang = millis();
      hitungGagalScanWiFi++;

      // Mulai hitung waktu hilang setelah 3x gagal scan berturut-turut (abaikan drop 1-2 detik)
      if (hitungGagalScanWiFi >= BATAS_TOLERANSI_WIFI) {
        if (!sedangMencari) {
          sedangMencari = true;
          waktuMulaiHilang = sekarang;
          waktuBuzzerPeringatanTerakhir = 0;
          catetLog("Sinyal Wi-Fi melemah/hilang, tunggu 15 detik sebelum mematikan kontak...");
        }

        unsigned long durasiHilang = sekarang - waktuMulaiHilang;

        // Setelah 6 detik hilang, beri beep peringatan tiap 2 detik
        if (durasiHilang >= MULAI_PERINGATAN_HILANG_MS && durasiHilang < TIMEOUT_PUTUS_KONTAK_MS) {
          if (sekarang - waktuBuzzerPeringatanTerakhir >= 2000) {
            waktuBuzzerPeringatanTerakhir = sekarang;
            bunyiPulsa(buzWifi, 50);
          }
        }

        // Baru matikan kontak setelah hilang terus-menerus selama 15 detik
        if (durasiHilang >= TIMEOUT_PUTUS_KONTAK_MS) {
          runNotDetected();
        }
      }
    }
  }

  updateIndikatorLED();
  delay(200);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(PIN_RELAY_KEYLESS, OUTPUT); digitalWrite(PIN_RELAY_KEYLESS, LOW);
  pinMode(PIN_BUZZER, OUTPUT);        digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_SIMULASI_GETAR, INPUT_PULLDOWN);
  pinMode(PIN_BUTTON_WAKEUP, INPUT_PULLUP);
  pinMode(PIN_VOLT_SENSOR, INPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);    digitalWrite(PIN_LED_STATUS, LOW);

  loadConfigList();
  setupWebServer();
  catetLog("Sistem boot selesai");

  if (belumAdaKunciTerdaftar()) {
    aktifkanWiFiAP();
  } else {
    masukModeWakeup();
  }
}

void loop() {
  unsigned long sekarang = millis();

  if (sedangOTA) {
    updateIndikatorLED();
    return;
  }

  // Cek batas waktu pengaman Mode Tune-Up (60 menit)
  if (modeTuneUp) {
    digitalWrite(PIN_RELAY_KEYLESS, HIGH);
    if (sekarang - waktuMulaiTuneUp >= TIMEOUT_TUNEUP_MS) {
      catetLog("Timeout Mode Tune-Up (60 menit) -> otomatis nonaktif");
      matikanModeTuneUp();
    }
  }

  if (belumAdaKunciTerdaftar()) {
    aktifkanWiFiAP();
  } else if (wifiApAktif && !modeTuneUp && (sekarang - waktuMulaiAP >= TIMEOUT_AP_AUTO_OFF)) {
    matikanWiFiAP();
  }

  updateIndikatorLED();
  updateJamOli();

  // ==================== MODE STANDBY (SCAN WIFI & RFID OFF) ====================
  if (berhentiTotalScan && !modeTuneUp) {
    if (WiFi.getMode() != WIFI_OFF && !wifiApAktif) {
      WiFi.mode(WIFI_OFF);
    }

    if (digitalRead(PIN_BUTTON_WAKEUP) == LOW) {
      if (!sedangMenekanWakeup) {
        waktuMulaiTekanWakeup = sekarang;
        sedangMenekanWakeup = true;
      } else if (sekarang - waktuMulaiTekanWakeup >= debounceTombolMs) {
        sedangMenekanWakeup = false;
        catetLog("Wake-up dari tombol");
        bunyiWakeupTombol();
        keluarDariStandby();
        return;
      }
    } else {
      sedangMenekanWakeup = false;
    }

    if (digitalRead(PIN_SIMULASI_GETAR) == HIGH) {
      if (!sedangMendeteksiGetar) {
        waktuGetarMulai = sekarang;
        sedangMendeteksiGetar = true;
      }
      waktuGetarTerakhir = sekarang;

      if (sekarang - waktuGetarMulai >= durasiStabilGetarMs) {
        sedangMendeteksiGetar = false;
        waktuGetarTerakhir = 0;
        catetLog("Wake-up dari sensor getar");
        bunyiWakeupGetar();
        keluarDariStandby();
        return;
      }
    } else if (sedangMendeteksiGetar && (sekarang - waktuGetarTerakhir > JEDA_TOLERANSI_GETAR_MS)) {
      sedangMendeteksiGetar = false;
      waktuGetarTerakhir = 0;
    }

    return;
  }

  // ==================== MODE AKTIF / WAKE UP / TUNE-UP ====================
  if (!modeTuneUp && sedangDiModeWakeup && !statusTerdeteksiSebelumnya && !rfidKontakState && !wifiApAktif && !modeSettingWeb) {
    if (sekarang - waktuMasukModeWakeup >= TIMEOUT_WAKEUP_KE_STANDBY) {
      catetLog("Timeout mode wake-up -> STANDBY");
      masukModeStandby();
      return;
    }
  }

  bool reading = digitalRead(PIN_BUTTON_WAKEUP);
  if (reading != lastButtonReading) {
    lastButtonChangeTime = sekarang;
  }
  if ((sekarang - lastButtonChangeTime) > debounceTombolMs) {
    if (reading != debouncedButtonState) {
      debouncedButtonState = reading;
      if (debouncedButtonState == LOW) {
        waktuMulaiTahanTombol = sekarang;
        sedangMenahanTombolAktif = true;
        tahanTombolSudahDipicu = false;

        if (sekarang - waktuButtonTapPertama > 2500) buttonTapCount = 0;
        if (buttonTapCount == 0) waktuButtonTapPertama = sekarang;
        buttonTapCount++;
        if (buttonTapCount >= jumlahTapConfigAP) {
          buttonTapCount = 0;
          waktuButtonTapPertama = 0;
          aktifkanWiFiAP();
        }
      } else {
        sedangMenahanTombolAktif = false;
        tahanTombolSudahDipicu = false;
      }
    }

    // Cek tahan tombol 5 detik untuk toggle Mode Tune-Up
    if (debouncedButtonState == LOW && sedangMenahanTombolAktif && !tahanTombolSudahDipicu) {
      if (sekarang - waktuMulaiTahanTombol >= TAHAN_TOMBOL_TUNEUP_MS) {
        tahanTombolSudahDipicu = true;
        buttonTapCount = 0;
        if (modeTuneUp) {
          matikanModeTuneUp();
        } else if (statusTerdeteksiSebelumnya || rfidKontakState || wifiApAktif) {
          aktifkanModeTuneUp();
        }
      }
    }
  }
  lastButtonReading = reading;

  bool rfidTerbaca = cekRFID();
  if (rfidTerbaca && rfidKontakState) {
    sedangDiModeWakeup = false;
    return;
  }

  if (wifiApAktif && belumAdaKunciTerdaftar()) {
    return;
  }

  if (!rfidKontakState && !modeSettingWeb && !modeTuneUp) {
    detectwifi();
  }
}
