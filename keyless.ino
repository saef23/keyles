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

int hitungGagalScanWiFi = 0;             
const int BATAS_TOLERANSI_WIFI = 3;    

unsigned long waktuGetarMulai = 0;
bool sedangMendeteksiGetar = false;
bool siapBangunGetar = false;          
const unsigned long DURASI_STABIL_GETAR = 450; 

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

// Variabel debounce tombol 3x tekan (saat aktif)
int buttonTapCount = 0;
unsigned long waktuButtonTapPertama = 0;
bool lastButtonReading = HIGH;
bool debouncedButtonState = HIGH;
unsigned long lastButtonChangeTime = 0;
const unsigned long DEBOUNCE_DELAY_BTN = 50;

// Variabel debounce wake up tombol (saat standby)
unsigned long waktuMulaiTekanWakeup = 0;
bool sedangMenekanWakeup = false;
const unsigned long DEBOUNCE_WAKEUP_BTN = 60;

AsyncWebServer server(80);

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
    if (!statusTerdeteksiSebelumnya) {
      digitalWrite(PIN_LED_STATUS, LOW);
    }
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
  preferences.end();
}

void simpanJamOli() {
  preferences.begin("wifi_keys", false);
  preferences.putULong("oli_menit", totalJamKerjaMenit);
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

void keluarDariStandby() {
  digitalWrite(PIN_LED_STATUS, HIGH);
  berhentiTotalScan = false;
  siapBangunGetar = false;
  sedangMendeteksiGetar = false;
  sedangMenekanWakeup = false;
  sedangMencari = false;
  statusTerdeteksiSebelumnya = false;
  waktuMulaiHilang = millis();
  hitungGagalScanWiFi = 0;
  mfrc522.PCD_SoftPowerUp();
  delay(50);
  mfrc522.PCD_Init();
  WiFi.mode(WIFI_STA);
  delay(1000);
}

void runDetected() {
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
      digitalWrite(PIN_LED_STATUS, HIGH); 
      bunyiBuzzerKunci();
      sedangMenungguStandbyRFID = true;
      waktuMulaiStandbyRFID = millis();
      statusTerdeteksiSebelumnya = false;
      matikanWiFiAP();
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
      if (!sedangMencari) { waktuMulaiHilang = millis(); sedangMencari = true; }
      if (millis() - waktuMulaiHilang >= 10000) {
        bunyiStandby(); 
        berhentiTotalScan = true; 
        siapBangunGetar = false;
        sedangMendeteksiGetar = false;
        sedangMenekanWakeup = false;
        sedangMencari = false;
        hitungGagalScanWiFi = 0;
        matikanWiFiAP();
        WiFi.mode(WIFI_OFF);
        mfrc522.PCD_SoftPowerDown();
        digitalWrite(PIN_LED_STATUS, LOW); 
      }
    }
  }
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
}

void loop() {
  if (wifiApAktif && (millis() - waktuMulaiAP >= TIMEOUT_AP_AUTO_OFF)) {
    matikanWiFiAP();
  }

  // Debounce tombol saat mode aktif (untuk 3x tekan -> aktifkan Config AP)
  bool reading = digitalRead(PIN_BUTTON_WAKEUP);
  if (reading != lastButtonReading) {
    lastButtonChangeTime = millis();
  }
  if ((millis() - lastButtonChangeTime) > DEBOUNCE_DELAY_BTN) {
    if (reading != debouncedButtonState) {
      debouncedButtonState = reading;
      if (!berhentiTotalScan && debouncedButtonState == LOW && statusTerdeteksiSebelumnya) {
        unsigned long sekarang = millis();
        if (sekarang - waktuButtonTapPertama > 2500) buttonTapCount = 0;
        if (buttonTapCount == 0) waktuButtonTapPertama = sekarang;
        buttonTapCount++;
        if (buttonTapCount >= 3) {
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
      } else if (millis() - waktuMulaiTekanWakeup >= DEBOUNCE_WAKEUP_BTN) {
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
        } else if (millis() - waktuGetarMulai >= DURASI_STABIL_GETAR) {
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

  if (sedangMenungguStandbyRFID) {
    if (millis() - waktuMulaiStandbyRFID >= 10000) {
      bunyiStandby(); 
      sedangMenungguStandbyRFID = false;
      berhentiTotalScan = true; 
      siapBangunGetar = false;
      sedangMendeteksiGetar = false;
      sedangMenekanWakeup = false;
      hitungGagalScanWiFi = 0;
      matikanWiFiAP();
      WiFi.mode(WIFI_OFF);
      mfrc522.PCD_SoftPowerDown();
      digitalWrite(PIN_LED_STATUS, LOW); 
    }
    delay(50);
    return;
  }

  if (keynumber > 0 || rfidAktif) detectwifi();
}
