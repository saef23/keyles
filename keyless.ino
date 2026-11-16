#include "WiFi.h"
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "index.h" // Memuat file HTML eksternal

// ==================== KONFIGURASI PIN ====================
const int PIN_RELAY_KEYLESS  = 2;   // Pin Relay Utama / Kontak (GPIO 2) - ACTIVE HIGH
const int PIN_SIMULASI_GETAR = 4;   // Pin Sensor Getar / Tombol Bangun (GPIO 4) - Sinyal HIGH saat bergetar
const int PIN_BUTTON_WAKEUP  = 13;  // Pin Pushbutton Cadangan Wake-up (GPIO 13) - ACTIVE LOW
const int PIN_BUZZER         = 25;  // Pin Buzzer Indikator (GPIO 25) - ACTIVE HIGH
const int PIN_VOLT_SENSOR    = 34;  // Pin ADC untuk Pemantau Tegangan Aki (GPIO 34) - Input Analog

// Konfigurasi Pin RFID RC522 (SPI Default ESP32)
const int RST_PIN = 22;             
const int SS_PIN  = 5;              
MFRC522 mfrc522(SS_PIN, RST_PIN);   

// ==================== PENYIMPANAN MAKSIMAL 5 ====================
String SSIDkey[5];
String BSSIDkey[5]; 
int RSSItresh[5];
int keynumber = 0;

String rfidKeyUID[5];  
int rfidCount = 0;     
bool rfidAktif = false;
bool rfidKontakState = false; 

Preferences preferences;

// Variabel Kontrol Waktu & Toleransi Putus-Nyambung
unsigned long waktuMulaiHilang = 0;
bool sedangMencari = false;
bool berhentiTotalScan = false;
bool statusTerdeteksiSebelumnya = false;

// Variabel Timer Jeda Standby Khusus Setelah Mati via RFID (10 Detik)
unsigned long waktuMulaiStandbyRFID = 0;
bool sedangMenungguStandbyRFID = false;

// Variabel Toleransi Stabilitas WiFi (Mencegah Relay Kedip/Mati-Hidup)
int hitungGagalScanWiFi = 0;             
const int BATAS_TOLERANSI_WIFI = 3;    

// Variabel Anti-Spam / Filter Sensor Getar
unsigned long waktuGetarMulai = 0;
bool sedangMendeteksiGetar = false;
bool siapBangunGetar = false;          
const unsigned long DURASI_STABIL_GETAR = 150; 

// ==================== VARIABEL TELEMETRI & TEGANGAN ====================
unsigned long totalJamKerjaMenit = 0;  
unsigned long waktuMulaiMesinNyala = 0;
const unsigned long INTERVAL_GANTI_OLI_MENIT = 12000; // 200 Jam = 12.000 Menit
bool peringatanOliSudahBunyi = false;

float teganganAki = 0.0; 

// Mode Web Config / AP Mode & Multi-Tap Triggers
bool modeSettingWeb = false;
bool wifiApAktif = false;
unsigned long waktuMulaiAP = 0;
const unsigned long TIMEOUT_AP_AUTO_OFF = 120000; // 2 Menit timeout AP jika tidak dipakai

// Variabel Deteksi Multi-Tap RFID & Tombol untuk Masuk Mode Setting
int rfidTapCount = 0;
unsigned long waktuRfidTapPertama = 0;
int buttonTapCount = 0;
unsigned long waktuButtonTapPertama = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonDebounce = 0;

AsyncWebServer server(80);

// ==================== FUNGSI BACA TEGANGAN AKI ====================
void bacaTeganganAki() {
  int nilaiAnalog = analogRead(PIN_VOLT_SENSOR);
  teganganAki = (nilaiAnalog / 4095.0) * 3.3 * 5.0; 
}

// ==================== PENGELOLAAN WI-FI AP ON-DEMAND ====================
void aktifkanWiFiAP() {
  if (!wifiApAktif) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Keyless-Config", "12345678");
    wifiApAktif = true;
    waktuMulaiAP = millis();
    Serial.println("\n[AP ON-DEMAND] Wi-Fi AP Aktif! SSID: ESP32-Keyless-Config (Sandi: 12345678)");
    Serial.println("Akses via browser: http://192.168.4.1");
    
    // Bunyi konfirmasi AP aktif
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
    Serial.println("\n[AP ON-DEMAND] Wi-Fi AP Dimatikan kembali untuk hemat daya.");
  }
}

// ==================== MEMUAT DATA DARI MEMORI ====================
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

// ==================== SETUP WEB SERVER ====================
void setupWebServer() {
  // Mulai dalam mode STA (AP mati secara default saat booting)
  WiFi.mode(WIFI_STA);
  wifiApAktif = false;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->requestAuthentication();
    request->send_P(200, "text/html", index_html);
  });

  server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->send(401);
    
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
    if(!request->authenticate("admin", "1234")) return request->send(401);

    if (request->hasParam("ssid", true) && request->hasParam("bssid", true) && request->hasParam("rssi", true)) {
      if (keynumber >= 5) {
        request->send(400, "text/html", "<h3>Maksimal 5 WiFi telah tercapai! <a href='/'>Kembali</a></h3>");
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

      request->send(200, "text/html", "<h3>WiFi & MAC Address Berhasil Disimpan! <a href='/'>Kembali</a></h3>");
    } else {
      request->send(400, "text/html", "<h3>Gagal menyimpan data.</h3>");
    }
  });

  server.on("/scanrfid", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->requestAuthentication();

    if (rfidCount >= 5) {
      request->send(200, "text/html", "<h3>Maksimal 5 kartu RFID telah tercapai! <a href='/'>Kembali</a></h3>");
      return;
    }
    modeSettingWeb = true; 
    request->send(200, "text/html", "<h3>Silakan tempelkan kartu RFID baru ke modul sekarang... <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetoli", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->requestAuthentication();

    totalJamKerjaMenit = 0;
    peringatanOliSudahBunyi = false;
    simpanJamOli();
    request->send(200, "text/html", "<h3>Jam Kerja Oli Berhasil Direset ke 0! <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->requestAuthentication();

    preferences.begin("wifi_keys", false);
    preferences.clear();
    preferences.end();
    request->send(200, "text/html", "<h3>Semua data telah dihapus! Restart ESP32... <a href='/'>Kembali</a></h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->requestAuthentication();

    request->send(200, "text/html", "<h3>ESP32 Memulai ulang...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "1234")) return request->send(401);

    bool updateSuccess = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", updateSuccess ? 
      "<h3>Update Firmware Berhasil! ESP32 akan merestart... <a href='/'>Kembali</a></h3>" : 
      "<h3>Gagal Melakukan Update! <a href='/'>Kembali</a></h3>");
    response->addHeader("Connection", "close");
    request->send(response);
    delay(1000);
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!request->authenticate("admin", "1234")) return;

    if(!index){
      Serial.printf("[OTA] Memulai proses update firmware: %s\n", filename.c_str());
      if(!Update.begin(UPDATE_SIZE_UNKNOWN)){
        Update.printError(Serial);
      }
    }
    if(len){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("[OTA] Update Berhasil: %u byte\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

// ==================== FUNGSI BUZZER ====================
void bunyiWakeup() {
  digitalWrite(PIN_BUZZER, HIGH); delay(60); 
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiStandby() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(50);
    digitalWrite(PIN_BUZZER, LOW);  delay(50);
  }
}

void bunyiWifiTerhubung() {
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiWifiTerputus() {
  digitalWrite(PIN_BUZZER, HIGH); delay(400); 
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiBuzzerBuka() {
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiBuzzerKunci() {
  digitalWrite(PIN_BUZZER, HIGH); delay(250); 
  digitalWrite(PIN_BUZZER, LOW);
}

void bunyiPeringatanOli() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(150);
    digitalWrite(PIN_BUZZER, LOW);  delay(150);
  }
}

// ==================== AKSI RELAY ====================
void runDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, HIGH);  
  if (!statusTerdeteksiSebelumnya) {
    bunyiWifiTerhubung();
    statusTerdeteksiSebelumnya = true;
    waktuMulaiMesinNyala = millis(); 
    
    if (totalJamKerjaMenit >= INTERVAL_GANTI_OLI_MENIT && !peringatanOliSudahBunyi) {
      bunyiPeringatanOli();
      peringatanOliSudahBunyi = true;
      Serial.println("[TELEMETRI] PERINGATAN: Waktunya Ganti Oli Mesin!");
    }
    
    Serial.println("STATUS: Motor Terbuka (Kontak ON).");
  } else {
    if (millis() - waktuMulaiMesinNyala >= 60000) {
      totalJamKerjaMenit++;
      waktuMulaiMesinNyala = millis();
      Serial.printf("[TELEMETRI] Total Jam Kerja Mesin: %lu Menit (~%lu Jam)\n", totalJamKerjaMenit, totalJamKerjaMenit / 60);
    }
  }
}

void runNotDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, LOW); 
  if (statusTerdeteksiSebelumnya) {
    bunyiWifiTerputus(); 
    statusTerdeteksiSebelumnya = false;
    simpanJamOli(); // Disimpan saat motor dikunci/mati agar flash awet
    matikanWiFiAP(); // Otomatis matikan AP jika motor mati
    Serial.println("STATUS: Motor Terkunci (Kontak OFF).");
  }
}

// ==================== CEK RFID ====================
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
      
      Serial.printf("[WEB RFID] Kartu ke-%d Berhasil Direkam! UID: %s\n", rfidCount, scannedUID.c_str());
      digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);
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
    Serial.printf("[RFID] Kartu Dikenali! UID: %s\n", scannedUID.c_str());
    
    // Fitur Tambahan: Deteksi Multi-Tap RFID (3x tap dalam 2.5 detik saat motor ON -> Buka Setting AP)
    if (statusTerdeteksiSebelumnya) {
      unsigned long sekarang = millis();
      if (sekarang - waktuRfidTapPertama > 2500) {
        rfidTapCount = 0;
      }
      if (rfidTapCount == 0) {
        waktuRfidTapPertama = sekarang;
      }
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
      bunyiBuzzerBuka();
      waktuMulaiMesinNyala = millis(); 
      
      berhentiTotalScan = false; 
      sedangMenungguStandbyRFID = false;
      siapBangunGetar = false;
      hitungGagalScanWiFi = 0;

      waktuMulaiHilang = millis();
      statusTerdeteksiSebelumnya = true;
      Serial.println("[RFID ACTION] Kontak dinyalakan via Kartu.");
    } else {
      digitalWrite(PIN_RELAY_KEYLESS, LOW);  
      bunyiBuzzerKunci();
      
      sedangMenungguStandbyRFID = true;
      waktuMulaiStandbyRFID = millis();
      statusTerdeteksiSebelumnya = false;
      matikanWiFiAP();
      
      Serial.println("[RFID ACTION] Kontak dimatikan via Kartu. Menunggu 10 detik sebelum standby...");
    }

    mfrc522.PICC_HaltA();
    return true;
  } else {
    Serial.printf("[RFID] Kartu Tidak Dikenali. UID: %s\n", scannedUID.c_str());
    digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW); delay(50);
    digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW);
  }

  mfrc522.PICC_HaltA();
  return false;
}

// ==================== DETEKSI WIFI ====================
void detectwifi() {
  if (berhentiTotalScan) return; 

  if (rfidKontakState || sedangMenungguStandbyRFID) return;

  int n = WiFi.scanNetworks();
  bool found = false;

  for (int i = 0; i < n; ++i) {
    String scannedSSID = WiFi.SSID(i);
    String scannedBSSID = WiFi.BSSIDstr(i); 
    scannedBSSID.toUpperCase();
    int scannedRSSI = WiFi.RSSI(i);

    for (int j = 0; j < keynumber; ++j) {
      if (scannedSSID == SSIDkey[j] && scannedBSSID == BSSIDkey[j]) {
        if (scannedRSSI >= RSSItresh[j]) {
          found = true;
          break;
        }
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
      
      if (!sedangMencari) {
        waktuMulaiHilang = millis();
        sedangMencari = true;
      }
      
      if (millis() - waktuMulaiHilang >= 10000) {
        Serial.println("STATUS: Sinyal hilang selama 10 detik. Masuk mode standby.");
        bunyiStandby(); 
        
        berhentiTotalScan = true; 
        siapBangunGetar = false;
        sedangMendeteksiGetar = false;
        sedangMencari = false;
        hitungGagalScanWiFi = 0;

        matikanWiFiAP();
        WiFi.mode(WIFI_OFF);
        mfrc522.PCD_SoftPowerDown();
        Serial.println("[POWER] WiFi & Modul RFID dimatikan total.");
      }
    } else {
      Serial.printf("[WIFI] Sinyal terputus sementara (%d/3)...\n", hitungGagalScanWiFi);
    }
  }
  
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(PIN_RELAY_KEYLESS, OUTPUT);
  digitalWrite(PIN_RELAY_KEYLESS, LOW);  

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);         

  pinMode(PIN_SIMULASI_GETAR, INPUT_PULLDOWN); 
  pinMode(PIN_BUTTON_WAKEUP, INPUT_PULLUP); 
  pinMode(PIN_VOLT_SENSOR, INPUT); 

  loadConfigList();
  setupWebServer();
}

void loop() {
  // Cek timeout otomatis untuk Wi-Fi AP jika sedang aktif
  if (wifiApAktif && (millis() - waktuMulaiAP >= TIMEOUT_AP_AUTO_OFF)) {
    Serial.println("[AP TIMEOUT] Wi-Fi AP dimatikan otomatis setelah 2 menit tidak diakses.");
    matikanWiFiAP();
  }

  if (berhentiTotalScan) {
    
    if (digitalRead(PIN_BUTTON_WAKEUP) == LOW) { 
      Serial.println("\n[WAKE UP] Tombol Pushbutton Cadangan ditekan! ESP32 bangun...");
      bunyiWakeup(); 
      berhentiTotalScan = false;
      siapBangunGetar = false;
      sedangMencari = false;
      statusTerdeteksiSebelumnya = false; 
      waktuMulaiHilang = millis();
      sedangMendeteksiGetar = false;
      hitungGagalScanWiFi = 0;

      mfrc522.PCD_SoftPowerUp();
      delay(50);
      mfrc522.PCD_Init(); 
      WiFi.mode(WIFI_STA); // Normal STA mode saat bangun
      Serial.println("[POWER] Modul RFID & WiFi diaktifkan kembali.");

      delay(1000);
      return;
    }

    if (!siapBangunGetar) {
      if (digitalRead(PIN_SIMULASI_GETAR) == LOW) {
        siapBangunGetar = true;
        Serial.println("[STANDBY] Sensor getar siap siaga (LOW).");
      }
      delay(100);
      return;
    }

    if (digitalRead(PIN_SIMULASI_GETAR) == HIGH) {
      if (!sedangMendeteksiGetar) {
        waktuGetarMulai = millis();
        sedangMendeteksiGetar = true;
      } else {
        if (millis() - waktuGetarMulai >= DURASI_STABIL_GETAR) {
          Serial.println("\n[WAKE UP] Guncangan valid terdeteksi! ESP32 bangun...");
          bunyiWakeup(); 
          berhentiTotalScan = false;
          siapBangunGetar = false;
          sedangMencari = false;
          statusTerdeteksiSebelumnya = false; 
          waktuMulaiHilang = millis();
          sedangMendeteksiGetar = false;
          hitungGagalScanWiFi = 0;

          mfrc522.PCD_SoftPowerUp();
          delay(50);
          mfrc522.PCD_Init(); 
          WiFi.mode(WIFI_STA); // Normal STA mode saat bangun
          Serial.println("[POWER] Modul RFID & WiFi diaktifkan kembali.");

          delay(1000); 
        }
      }
    } else {
      sedangMendeteksiGetar = false;
    }

    delay(50); 
    return; 
  }

  // Cek tombol fisik (PIN_BUTTON_WAKEUP) saat motor menyala (Multi-tap 3x untuk buka AP setting)
  bool currentButtonState = digitalRead(PIN_BUTTON_WAKEUP);
  if (currentButtonState == LOW && lastButtonState == HIGH && (millis() - lastButtonDebounce > 300)) {
    lastButtonDebounce = millis();
    if (statusTerdeteksiSebelumnya) {
      unsigned long sekarang = millis();
      if (sekarang - waktuButtonTapPertama > 2500) {
        buttonTapCount = 0;
      }
      if (buttonTapCount == 0) {
        waktuButtonTapPertama = sekarang;
      }
      buttonTapCount++;

      if (buttonTapCount >= 3) {
        buttonTapCount = 0;
        aktifkanWiFiAP();
      }
    }
  }
  lastButtonState = currentButtonState;

  if (cekRFID()) {
    delay(1500); 
  }

  if (sedangMenungguStandbyRFID) {
    if (millis() - waktuMulaiStandbyRFID >= 10000) {
      Serial.println("STATUS: Jeda 10 detik selesai. ESP32 masuk mode standby.");
      bunyiStandby(); 
      
      sedangMenungguStandbyRFID = false;
      berhentiTotalScan = true; 
      siapBangunGetar = false;
      sedangMendeteksiGetar = false;
      hitungGagalScanWiFi = 0;

      matikanWiFiAP();
      WiFi.mode(WIFI_OFF);
      mfrc522.PCD_SoftPowerDown();
      Serial.println("[POWER] WiFi & Modul RFID dimatikan total untuk hemat daya.");
    }
    delay(50);
    return;
  }

  if (keynumber > 0 || rfidAktif) {
    detectwifi();
  }
}
