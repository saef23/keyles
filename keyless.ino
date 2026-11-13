#include "WiFi.h"
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ==================== KONFIGURASI PIN ====================
const int PIN_RELAY_KEYLESS  = 2;   // Pin Relay Utama / Kontak (GPIO 2) - Active LOW
const int PIN_SIMULASI_GETAR = 4;   // Pin Sensor Getar / Tombol Bangun (GPIO 4)
const int PIN_BUZZER         = 25;  // Pin Buzzer Indikator (GPIO 25) - Active LOW

// Konfigurasi Pin RFID RC522 (SPI Default ESP32)
const int RST_PIN = 22;             
const int SS_PIN  = 5;              
MFRC522 mfrc522(SS_PIN, RST_PIN);   

// ==================== PENYIMPANAN MAKSIMAL 5 ====================
String SSIDkey[5];
int RSSItresh[5];
int keynumber = 0;

String rfidKeyUID[5];  // Mendukung hingga 5 kartu RFID
int rfidCount = 0;     // Jumlah kartu yang tersimpan
bool rfidAktif = false;
bool rfidKontakState = false; // Status Toggle ON/OFF via RFID

Preferences preferences;

// Variabel Kontrol Waktu & Toleransi Putus-Nyambung
unsigned long waktuMulaiHilang = 0;
bool sedangMencari = false;
bool berhentiTotalScan = false;
int hitungGagalScan = 0;             
const int BATAS_TOLERANSI_GAGAL = 3; 
bool statusTerdeteksiSebelumnya = false;

// Mode Web Config / AP Mode
bool modeSettingWeb = false;
AsyncWebServer server(80);

// ==================== HALAMAN HTML WEB CONFIG ====================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Keyless ESP32 Config</title>
  <style>
    body { font-family: Arial; text-align: center; background: #f4f4f9; margin: 0; padding: 20px; }
    h2 { color: #333; }
    .card { background: white; padding: 20px; margin: 10px auto; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); max-width: 400px; text-align: left; }
    input, select { width: 93%; padding: 10px; margin: 8px 0; border: 1px solid #ccc; border-radius: 4px; }
    button { background: #007BFF; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
    button:hover { background: #0056b3; }
    .btn-danger { background: #dc3545; }
    .btn-danger:hover { background: #bd2130; }
    ul { padding-left: 20px; }
  </style>
</head>
<body>
  <h2 style="text-align:center;">Pengaturan Keyless ESP32</h2>
  
  <div class="card">
    <h3>Daftar WiFi Hotspot (Max 5)</h3>
    <div id="wifiList">Memuat data WiFi...</div>
    <form action="/savewifi" method="POST">
      <input type="text" name="ssid" placeholder="Nama SSID Hotspot HP" required><br>
      <input type="number" name="rssi" placeholder="RSSI Threshold (Contoh: -75)" value="-75" required><br>
      <button type="submit">Simpan WiFi</button>
    </form>
  </div>

  <div class="card">
    <h3>Status Kartu RFID (Max 5)</h3>
    <div id="rfidList">Memuat data RFID...</div>
    <a href="/scanrfid"><button style="margin-top:10px;">Mode Rekam Kartu RFID Baru</button></a>
  </div>

  <div class="card">
    <h3>Kontrol Sistem</h3>
    <a href="/resetwifi"><button class="btn-danger" style="margin-bottom:8px;">Hapus Semua WiFi & RFID</button></a>
    <a href="/restart"><button style="background:#6c757d;">Restart ESP32</button></a>
  </div>

<script>
  fetch('/getdata').then(response => response.json()).then(data => {
    // Tampilkan WiFi
    let wifiHTML = "<ul>";
    if(data.wifis.length === 0) wifiHTML = "<p>Belum ada WiFi terdaftar.</p>";
    data.wifis.forEach(w => {
      wifiHTML += `<li><b>${w.ssid}</b> (RSSI: ${w.rssi})</li>`;
    });
    wifiHTML += "</ul>";
    document.getElementById('wifiList').innerHTML = wifiHTML;

    // Tampilkan RFID
    let rfidHTML = "<ul>";
    if(data.rfids.length === 0) rfidHTML = "<p>Belum ada kartu terdaftar.</p>";
    data.rfids.forEach((r, idx) => {
      rfidHTML += `<li>Kartu ${idx+1}: <code>${r}</code></li>`;
    });
    rfidHTML += "</ul>";
    document.getElementById('rfidList').innerHTML = rfidHTML;
  });
</script>
</body>
</html>
)rawliteral";

// ==================== MEMUAT DATA DARI MEMORI ====================
void loadConfigList() {
  preferences.begin("wifi_keys", true);
  
  // Load WiFi
  keynumber = preferences.getInt("keynumber", 0);
  if (keynumber > 5) keynumber = 5;
  for (int i = 0; i < keynumber; ++i) {
    SSIDkey[i] = preferences.getString(("ssid" + String(i)).c_str(), "");
    RSSItresh[i] = preferences.getInt(("rssi" + String(i)).c_str(), -100);
  }

  // Load RFID
  rfidCount = preferences.getInt("rfid_count", 0);
  if (rfidCount > 5) rfidCount = 5;
  for (int i = 0; i < rfidCount; ++i) {
    rfidKeyUID[i] = preferences.getString(("rfid_" + String(i)).c_str(), "");
  }
  rfidAktif = (rfidCount > 0);

  preferences.end();
}

// ==================== SETUP WEB SERVER ====================
void setupWebServer() {
  WiFi.softAP("ESP32-Keyless-Config", "12345678");
  Serial.println("\n[WEB SERVER] Mode AP Aktif!");
  Serial.println("Sambungkan HP ke WiFi: ESP32-Keyless-Config (Sandi: 12345678)");
  Serial.println("Buka browser dan ketik alamat: http://192.168.4.1");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{\"wifis\":[";
    for(int i=0; i<keynumber; i++){
      json += "{\"ssid\":\"" + SSIDkey[i] + "\",\"rssi\":" + String(RSSItresh[i]) + "}";
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
    if (request->hasParam("ssid", true) && request->hasParam("rssi", true)) {
      if (keynumber >= 5) {
        request->send(400, "text/html", "<h3>Maksimal 5 WiFi telah tercapai! <a href='/'>Kembali</a></h3>");
        return;
      }
      String newSsid = request->getParam("ssid", true)->value();
      int newRssi = request->getParam("rssi", true)->value().toInt();

      SSIDkey[keynumber] = newSsid;
      RSSItresh[keynumber] = newRssi;

      preferences.begin("wifi_keys", false);
      preferences.putString(("ssid" + String(keynumber)).c_str(), newSsid);
      preferences.putInt(("rssi" + String(keynumber)).c_str(), newRssi);
      keynumber++;
      preferences.putInt("keynumber", keynumber);
      preferences.end();

      request->send(200, "text/html", "<h3>WiFi Berhasil Disimpan! <a href='/'>Kembali</a></h3>");
    } else {
      request->send(400, "text/html", "<h3>Gagal menyimpan data.</h3>");
    }
  });

  server.on("/scanrfid", HTTP_GET, [](AsyncWebServerRequest *request){
    if (rfidCount >= 5) {
      request->send(200, "text/html", "<h3>Maksimal 5 kartu RFID telah tercapai! <a href='/'>Kembali</a></h3>");
      return;
    }
    modeSettingWeb = true; 
    request->send(200, "text/html", "<h3>Silakan tempelkan kartu RFID baru ke modul sekarang... Cek Serial Monitor. <a href='/'>Kembali</a></h3>");
  });

  server.on("/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request){
    preferences.begin("wifi_keys", false);
    preferences.clear();
    preferences.end();
    request->send(200, "text/html", "<h3>Semua data WiFi & RFID telah dihapus! Restart ESP32... <a href='/'>Kembali</a></h3>");
    delay(1000);
    ESP.restart();
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<h3>ESP32 Memulai ulang...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

// ==================== FUNGSI BUZZER DENGAN NADA BERBEDA ====================
void bunyiWakeup() {
  digitalWrite(PIN_BUZZER, LOW);  delay(60); 
  digitalWrite(PIN_BUZZER, HIGH);
}

void bunyiWifiTerhubung() {
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH);
}

void bunyiWifiTerputus() {
  digitalWrite(PIN_BUZZER, LOW);  delay(400); 
  digitalWrite(PIN_BUZZER, HIGH);
}

void bunyiBuzzerBuka() {
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH); delay(80); 
  digitalWrite(PIN_BUZZER, LOW);  delay(80); 
  digitalWrite(PIN_BUZZER, HIGH);
}

void bunyiBuzzerKunci() {
  digitalWrite(PIN_BUZZER, LOW);  delay(250); 
  digitalWrite(PIN_BUZZER, HIGH);
}

// ==================== AKSI RELAY ====================
void runDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, LOW);  
  hitungGagalScan = 0;                   
  if (!statusTerdeteksiSebelumnya) {
    bunyiWifiTerhubung();
    statusTerdeteksiSebelumnya = true;
    Serial.println("STATUS: Motor Terbuka (Kontak ON via WiFi).");
  }
}

void runNotDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, HIGH); 
  if (statusTerdeteksiSebelumnya) {
    bunyiWifiTerputus(); // Suara beda saat putus / deep sleep
    statusTerdeteksiSebelumnya = false;
    Serial.println("STATUS: Motor Terkunci (Kontak OFF / Masuk Deep Sleep).");
  }
}

// ==================== CEK RFID (MULTI KARTU & TOGGLE) ====================
bool cekRFID() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return false;

  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  content.toUpperCase();
  String scannedUID = content.substring(1); 

  // Mode Perekaman Kartu Baru via Web
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
      digitalWrite(PIN_BUZZER, LOW); delay(200); digitalWrite(PIN_BUZZER, HIGH);
    }
    modeSettingWeb = false;
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return true;
  }

  // Cek apakah kartu terdaftar di antara 5 kartu
  bool kartuCocok = false;
  for (int i = 0; i < rfidCount; i++) {
    if (scannedUID == rfidKeyUID[i]) {
      kartuCocok = true;
      break;
    }
  }

  if (rfidAktif && kartuCocok) {
    Serial.printf("[RFID] Kartu Dikenali! UID: %s\n", scannedUID.c_str());
    
    // Sistem Toggle: Tap 1x ON, Tap 1x lagi OFF
    rfidKontakState = !rfidKontakState; 

    if (rfidKontakState) {
      digitalWrite(PIN_RELAY_KEYLESS, LOW);  
      bunyiBuzzerBuka();
      berhentiTotalScan = false; 
      waktuMulaiHilang = millis();
      hitungGagalScan = 0;
      statusTerdeteksiSebelumnya = true;
      Serial.println("[RFID ACTION] Kontak dinyalakan via Kartu.");
    } else {
      digitalWrite(PIN_RELAY_KEYLESS, HIGH); 
      bunyiBuzzerKunci();
      berhentiTotalScan = true; // Istirahat total setelah dimatikan manual
      statusTerdeteksiSebelumnya = false;
      Serial.println("[RFID ACTION] Kontak dimatikan via Kartu.");
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return true;
  } else {
    Serial.printf("[RFID] Kartu Tidak Dikenali. UID: %s\n", scannedUID.c_str());
    digitalWrite(PIN_BUZZER, LOW); delay(50); digitalWrite(PIN_BUZZER, HIGH); delay(50);
    digitalWrite(PIN_BUZZER, LOW); delay(50); digitalWrite(PIN_BUZZER, HIGH);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  return false;
}

// ==================== DETEKSI WIFI ====================
void detectwifi() {
  if (berhentiTotalScan) return; 

  int n = WiFi.scanNetworks();
  bool found = false;

  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < keynumber; ++j) {
      if (WiFi.SSID(i) == SSIDkey[j]) {
        if (WiFi.RSSI(i) >= RSSItresh[j]) {
          found = true;
          WiFi.scanDelete();
          runDetected(); 
          waktuMulaiHilang = 0;
          sedangMencari = false;
          hitungGagalScan = 0;
          delay(500);
          break;
        }
      }
    }
    if (found) break;
  }

  if (!found) {
    WiFi.scanDelete();
    hitungGagalScan++; 

    if (hitungGagalScan >= BATAS_TOLERANSI_GAGAL) {
      runNotDetected(); 
      if (!sedangMencari) {
        waktuMulaiHilang = millis();
        sedangMencari = true;
      }
      if (millis() - waktuMulaiHilang >= 60000) {
        Serial.println("STATUS: Motor ditinggal 1 menit. ESP32 BERHENTI SCAN WiFi total.");
        berhentiTotalScan = true; 
      }
    }
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(PIN_RELAY_KEYLESS, OUTPUT);
  digitalWrite(PIN_RELAY_KEYLESS, HIGH); 

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH);        

  pinMode(PIN_SIMULASI_GETAR, INPUT_PULLUP); 

  loadConfigList();
  setupWebServer();
}

void loop() {
  if (cekRFID()) {
    delay(500);
  }

  if (berhentiTotalScan) {
    if (digitalRead(PIN_SIMULASI_GETAR) == LOW) { 
      Serial.println("\n[WAKE UP] Guncangan terdeteksi! ESP32 bangun...");
      bunyiWakeup(); // Suara khusus saat bangun
      berhentiTotalScan = false;
      sedangMencari = false;
      hitungGagalScan = 0;
      statusTerdeteksiSebelumnya = false; 
      waktuMulaiHilang = millis();
      delay(1000); 
    } else {
      delay(100); 
      return; 
    }
  }

  if (keynumber > 0 || rfidAktif) {
    detectwifi();
  }
}
