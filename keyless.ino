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
String BSSIDkey[5]; 
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

// Mode wake up sementara setelah terputus Wi-Fi / dimatikan RFID sebelum ke standby
unsigned long waktuMasukModeWakeup = 0;
bool sedangDiModeWakeup = false;
const unsigned long TIMEOUT_WAKEUP_KE_STANDBY = 30000; // 30 detik sebelum ke standby

int hitungGagalScanWiFi = 0;             
const int BATAS_TOLERANSI_WIFI = 3;    

unsigned long waktuGetarMulai = 0;
bool sedangMendeteksiGetar = false;
bool siapBangunGetar = false;          

// Sensitivitas tersimpan di Preferences (dapat diubah lewat Web Config)
unsigned long durasiStabilGetarMs = 250; // ms (makin kecil makin responsif/sensitif)
unsigned long debounceTombolMs    = 60;  // ms
int jumlahTapConfigAP             = 3;   // 2 - 5 kali tekan

unsigned long totalJamKerjaMenit = 0;  
unsigned long waktuMulaiMesinNyala = 0;
const unsigned long INTERVAL_GANTI_OLI_MENIT = 12000; 
bool peringatanOliSudahBunyi = false;

float teganganAki = 0.0; 

bool modeSettingWeb = false;
bool wifiApAktif = false;
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

AsyncWebServer server(80);

void updateIndikatorLED() {
  unsigned long sekarang = millis();

  // 4. Mode AP aktif -> kedip cepat (120 ms)
  if (wifiApAktif) {
    if (sekarang - lastLedToggle >= 120) {
      lastLedToggle = sekarang;
      ledState = !ledState;
      digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
    return;
  }

  // 1. Saat standby -> mati (LOW)
  if (berhentiTotalScan) {
    ledState = LOW;
    digitalWrite(PIN_LED_STATUS, LOW);
    return;
  }

  // 3. Saat terhubung RFID / Wi-Fi / kontak ON -> mati (LOW)
  if (statusTerdeteksiSebelumnya || rfidKontakState) {
    ledState = LOW;
    digitalWrite(PIN_LED_STATUS, LOW);
    return;
  }

  // 2. Saat wake up / belum terhubung (cari Wi-Fi) -> kedip pelan (700 ms)
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
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Keyless-Config", "12345678");
    wifiApAktif = true;
    waktuMulaiAP = millis();
    lastLedToggle = millis();
    ledState = HIGH;
    digitalWrite(PIN_LED_STATUS, HIGH);
    Serial.println("\n[AP ON-DEMAND] Wi-Fi AP Aktif!");

    digitalWrite(PIN_BUZZER, HIGH); delay(100);
    digitalWrite(PIN_BUZZER, LOW);  delay(100);
    digitalWrite(PIN_BUZZER, HIGH); delay(100);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void matikanWiFiAP() {
  if (wifiApAktif) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiApAktif = false;
    lastLedToggle = millis();
    updateIndikatorLED();
    Serial.println("\n[AP ON-DEMAND] Wi-Fi AP Dimatikan.");
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
                  ",\"wifis\":[";
    for(int i=0; i<keynumber; i++){
      json += "{\"ssid\":\"" + SSIDkey[i] + "\",\"bssid\":\"" + BSSIDkey[i] + "\",\"rssi\":" + String(RSSItresh[i]) + "}";
      if(i < keynumber - 1) json += ",";
    }
    json += "],\"rfids\":[";
    for(int i=0; i<rfidCount; i++){
      json += "\"" + rfidKeyUID[i] + "\"";
      if(i < rfidCount - 1) json += ",";
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

      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("bssid", true) && request->hasParam("rssi", true)) {
      if (keynumber >= 5) {
        request->send(200, "text/html", "<h3>Maksimal 5 WiFi! <a href='/'>Kembali</a></h3>");
        return;
      }
      String newSsid = request->getParam("ssid", true)->value();
      String newBssid = request->getParam("bssid", true)->value();
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
    request->send(200, "text/html", "<h3>Tempelkan kartu RFID baru ke modul... <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetoli", HTTP_GET, [](AsyncWebServerRequest *request){
    totalJamKerjaMenit = 0;
    peringatanOliSudahBunyi = false;
    simpanJamOli();
    request->send(200, "text/html", "<h3>Timer Oli Direset! <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("wifi_keys", false);
    preferences.clear();
    preferences.end();
    request->send(200, "text/html", "<h3>Data Dihapus! Restart ESP32...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<h3>ESP32 Memulai ulang...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool updateSuccess = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", updateSuccess ? 
      "<h3>Update Berhasil! Restart...</h3>" : "<h3>Gagal Update! <a href='/'>Kembali</a></h3>");
    response->addHeader("Connection", "close");
    request->send(response);
    delay(1000);
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      if(!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    }
    if(len){
      if(Update.write(data, len) != len) Update.printError(Serial);
    }
    if(final){
      if(Update.end(true)) Serial.println("[OTA] Selesai");
    }
  });

  server.begin();
}

// ==================== FUNGSI BUZZER & STANDALONE ====================
void bunyiWakeupTombol() {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(60);
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiWakeupGetar() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(60);
    digitalWrite(PIN_BUZZER, LOW);
    if (i == 0) delay(70);
  }
}

void bunyiStandby() { for (int i = 0; i < 3; i++) { digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW); delay(50); } }
void bunyiWifiTerhubung() { digitalWrite(PIN_BUZZER, HIGH); delay(80); digitalWrite(PIN_BUZZER, LOW); delay(80); digitalWrite(PIN_BUZZER, HIGH); delay(80); digitalWrite(PIN_BUZZER, LOW); }
void bunyiWifiTerputus() { digitalWrite(PIN_BUZZER, HIGH); delay(400); digitalWrite(PIN_BUZZER, LOW); }
void bunyiBuzzerBuka() { digitalWrite(PIN_BUZZER, HIGH); delay(80); digitalWrite(PIN_BUZZER, LOW); delay(80); digitalWrite(PIN_BUZZER, HIGH); delay(80); digitalWrite(PIN_BUZZER, LOW); }
void bunyiBuzzerKunci() { digitalWrite(PIN_BUZZER, HIGH); delay(250); digitalWrite(PIN_BUZZER, LOW); }
void bunyiPeringatanOli() { for (int i = 0; i < 4; i++) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); delay(150); } }

void masukModeStandby() {
  sedangDiModeWakeup = false;
  sedangMenungguStandbyRFID = false;
  berhentiTotalScan = true;
  siapBangunGetar = false;
  sedangMendeteksiGetar = false;
  sedangMenekanWakeup = false;
  sedangMencari = false;
  hitungGagalScanWiFi = 0;
  matikanWiFiAP();
  WiFi.mode(WIFI_OFF);
  mfrc522.PCD_SoftPowerDown();
  ledState = LOW;
  digitalWrite(PIN_LED_STATUS, LOW);
  bunyiStandby();
}

void masukModeWakeup() {
  berhentiTotalScan = false;
  sedangMenungguStandbyRFID = false;
  siapBangunGetar = false;
  sedangMendeteksiGetar = false;
  sedangMenekanWakeup = false;
  statusTerdeteksiSebelumnya = false;
  rfidKontakState = false;
  hitungGagalScanWiFi = 0;
  sedangMencari = false;
  waktuMulaiHilang = 0;

  sedangDiModeWakeup = true;
  waktuMasukModeWakeup = millis();

  lastLedToggle = millis();
  ledState = HIGH;
  digitalWrite(PIN_LED_STATUS, HIGH);

  mfrc522.PCD_SoftPowerUp();
  delay(30);
  mfrc522.PCD_Init();
  WiFi.mode(WIFI_STA);
}

void keluarDariStandby() {
  masukModeWakeup();
  delay(1000);
}

void runDetected() {
  sedangDiModeWakeup = false;
  digitalWrite(PIN_RELAY_KEYLESS, HIGH);  
  digitalWrite(PIN_LED_STATUS, LOW); 
  if (!statusTerdeteksiSebelumnya) {
    bunyiWifiTerhubung();
    statusTerdeteksiSebelumnya = true;
    waktuMulaiMesinNyala = millis(); 
    if (totalJamKerjaMenit >= INTERVAL_GANTI_OLI_MENIT && !peringatanOliSudahBunyi) {
      bunyiPeringatanOli();
      peringatanOliSudahBunyi = true;
    }
  } else {
    if (millis() - waktuMulaiMesinNyala >= 60000) {
      totalJamKerjaMenit++;
      waktuMulaiMesinNyala = millis();
    }
  }
}

void runNotDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, LOW); 
  if (statusTerdeteksiSebelumnya) {
    bunyiWifiTerputus(); 
    statusTerdeteksiSebelumnya = false;
    simpanJamOli(); 
    matikanWiFiAP();
    // Tidak langsung ke standby, masuk mode wake up dulu
    masukModeWakeup();
  }
}

bool cekRFID() {
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
      digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);
    }
    modeSettingWeb = false;
    mfrc522.PICC_HaltA();
    return true;
  }

  bool kartuCocok = false;
  for (int i = 0; i < rfidCount; i++) {
    if (scannedUID == rfidKeyUID[i]) { kartuCocok = true; break; }
  }

  if (rfidAktif && kartuCocok) {
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

    rfidKontakState = !rfidKontakState; 
    if (rfidKontakState) {
      sedangDiModeWakeup = false;
      digitalWrite(PIN_RELAY_KEYLESS, HIGH); 
      digitalWrite(PIN_LED_STATUS, LOW); 
      bunyiBuzzerBuka();
      waktuMulaiMesinNyala = millis(); 
      berhentiTotalScan = false; 
      sedangMenungguStandbyRFID = false;
      siapBangunGetar = false;
      hitungGagalScanWiFi = 0;
      waktuMulaiHilang = millis();
      statusTerdeteksiSebelumnya = true;
    } else {
      digitalWrite(PIN_RELAY_KEYLESS, LOW);  
      bunyiBuzzerKunci();
      matikanWiFiAP();
      // Tidak langsung ke standby, masuk mode wake up dulu
      masukModeWakeup();
    }
    mfrc522.PICC_HaltA();
    return true;
  } else {
    digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW); delay(50);
    digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW);
  }
  mfrc522.PICC_HaltA();
  return false;
}

void detectwifi() {
  if (berhentiTotalScan || rfidKontakState || sedangMenungguStandbyRFID) return;
  updateIndikatorLED();
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; ++i) {
    String scannedSSID = WiFi.SSID(i);
    String scannedBSSID = WiFi.BSSIDstr(i); 
    scannedBSSID.toUpperCase();
    int scannedRSSI = WiFi.RSSI(i);
    for (int j = 0; j < keynumber; ++j) {
      if (scannedSSID == SSIDkey[j] && scannedBSSID == BSSIDkey[j] && scannedRSSI >= RSSItresh[j]) {
        found = true; break;
      }
    }
    if (found) break;
  }
  WiFi.scanDelete();

  if (found) {
    runDetected(); 
    hitungGagalScanWiFi = 0; 
    waktuMulaiHilang = 0;
    sedangMencari = false;
  } else {
    hitungGagalScanWiFi++; 
    if (hitungGagalScanWiFi >= BATAS_TOLERANSI_WIFI) {
      runNotDetected(); 
    }
  }
  updateIndikatorLED();
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  SPI.begin();
  mfrc522.PCD_Init();
  pinMode(PIN_RELAY_KEYLESS, OUTPUT); digitalWrite(PIN_RELAY_KEYLESS, LOW);  
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);         
  pinMode(PIN_SIMULASI_GETAR, INPUT_PULLDOWN); 
  pinMode(PIN_BUTTON_WAKEUP, INPUT_PULLUP); 
  pinMode(PIN_VOLT_SENSOR, INPUT); 
  pinMode(PIN_LED_STATUS, OUTPUT); digitalWrite(PIN_LED_STATUS, LOW); 
  loadConfigList();
  setupWebServer();

  // Jika belum ada Wi-Fi maupun RFID tersimpan, nyalakan Config AP otomatis
  if (keynumber == 0 && rfidCount == 0) {
    aktifkanWiFiAP();
  }
}

void loop() {
  if (wifiApAktif && (millis() - waktuMulaiAP >= TIMEOUT_AP_AUTO_OFF)) {
    matikanWiFiAP();
  }

  updateIndikatorLED();

  // Debounce tombol saat mode aktif (buka Config AP sesuai jumlahTapConfigAP)
  bool reading = digitalRead(PIN_BUTTON_WAKEUP);
  if (reading != lastButtonReading) {
    lastButtonChangeTime = millis();
  }
  if ((millis() - lastButtonChangeTime) > debounceTombolMs) {
    if (reading != debouncedButtonState) {
      debouncedButtonState = reading;
      if (!berhentiTotalScan && debouncedButtonState == LOW && statusTerdeteksiSebelumnya) {
        unsigned long sekarang = millis();
        if (sekarang - waktuButtonTapPertama > 2500) buttonTapCount = 0;
        if (buttonTapCount == 0) waktuButtonTapPertama = sekarang;
        buttonTapCount++;
        if (buttonTapCount >= jumlahTapConfigAP) {
          buttonTapCount = 0;
          waktuButtonTapPertama = 0;
          aktifkanWiFiAP();
        }
      }
    }
  }
  lastButtonReading = reading;

  if (berhentiTotalScan) {
    // 1. Wake up dari tombol (1 bip pendek)
    if (digitalRead(PIN_BUTTON_WAKEUP) == LOW) {
      if (!sedangMenekanWakeup) {
        waktuMulaiTekanWakeup = millis();
        sedangMenekanWakeup = true;
      } else if (millis() - waktuMulaiTekanWakeup >= debounceTombolMs) {
        bunyiWakeupTombol();
        keluarDariStandby();
        return;
      }
    } else {
      sedangMenekanWakeup = false;
    }

    // 2. Wake up dari getar (2 bip pendek)
    if (!siapBangunGetar) {
      if (digitalRead(PIN_SIMULASI_GETAR) == LOW) siapBangunGetar = true;
    } else {
      if (digitalRead(PIN_SIMULASI_GETAR) == HIGH) {
        if (!sedangMendeteksiGetar) {
          waktuGetarMulai = millis();
          sedangMendeteksiGetar = true;
        } else if (millis() - waktuGetarMulai >= durasiStabilGetarMs) {
          bunyiWakeupGetar();
          keluarDariStandby();
          return;
        }
      } else {
        sedangMendeteksiGetar = false;
      }
    }

    delay(20);
    return;
  }

  if (cekRFID()) delay(1500);

  if (sedangDiModeWakeup && !statusTerdeteksiSebelumnya && !rfidKontakState) {
    if (millis() - waktuMasukModeWakeup >= TIMEOUT_WAKEUP_KE_STANDBY) {
      masukModeStandby();
      return;
    }
  }

  if (sedangMenungguStandbyRFID) {
    if (millis() - waktuMulaiStandbyRFID >= 10000) {
      masukModeStandby();
      return;
    }
    delay(50);
    return;
  }

  if (keynumber > 0 || rfidAktif) detectwifi();
}
