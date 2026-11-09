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

String SSIDkey[10];
int RSSItresh[10];
int keynumber = 0;

String rfidKeyUID = ""; 
bool rfidAktif = false;

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
    .card { background: white; padding: 20px; margin: 10px auto; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); max-width: 400px; }
    input, select { width: 90%; padding: 10px; margin: 8px 0; border: 1px solid #ccc; border-radius: 4px; }
    button { background: #007BFF; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
    button:hover { background: #0056b3; }
    .btn-danger { background: #dc3545; }
    .btn-danger:hover { background: #bd2130; }
  </style>
</head>
<body>
  <h2>Pengaturan Keyless ESP32</h2>
  
  <div class="card">
    <h3>Daftarkan WiFi Hotspot</h3>
    <form action="/savewifi" method="POST">
      <input type="text" name="ssid" placeholder="Nama SSID Hotspot HP" required><br>
      <input type="number" name="rssi" placeholder="RSSI Threshold (Contoh: -75)" value="-75" required><br>
      <button type="submit">Simpan WiFi</button>
    </form>
  </div>

  <div class="card">
    <h3>Status Kartu RFID</h3>
    <p id="rfidStatus">Memuat...</p>
    <a href="/scanrfid"><button>Mode Rekam Kartu RFID Baru</button></a>
  </div>

  <div class="card">
    <h3>Kontrol Sistem</h3>
    <a href="/restart"><button class="btn-danger">Restart ESP32</button></a>
  </div>

<script>
  fetch('/getdata').then(response => response.json()).then(data => {
    document.getElementById('rfidStatus').innerHTML = data.rfid ? "Terdaftar: " + data.rfid : "Belum ada kartu terdaftar";
  });
</script>
</body>
</html>
)rawliteral";

// ==================== MEMUAT DATA DARI MEMORI ====================
void loadConfigList() {
  preferences.begin("wifi_keys", true);
  keynumber = preferences.getInt("keynumber", 0);
  for (int i = 0; i < keynumber; ++i) {
    SSIDkey[i] = preferences.getString(("ssid" + String(i)).c_str(), "");
    RSSItresh[i] = preferences.getInt(("rssi" + String(i)).c_str(), -100);
  }
  rfidKeyUID = preferences.getString("rfid_uid", "");
  rfidAktif = (rfidKeyUID != "");
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
    String json = "{\"rfid\":\"" + rfidKeyUID + "\"}";
    request->send(200, "application/json", json);
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("rssi", true)) {
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
    modeSettingWeb = true; 
    request->send(200, "text/html", "<h3>Silakan tempelkan kartu RFID ke modul sekarang... Cek Serial Monitor untuk konfirmasi. <a href='/'>Kembali</a></h3>");
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<h3>ESP32 Memulai ulang... Silakan sambungkan ulang.</h3>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

// ==================== FUNGSI BANTUAN BUZZER (ACTIVE LOW) ====================
void bunyiBuzzerBuka() {
  digitalWrite(PIN_BUZZER, LOW);  delay(80); // Nyala (LOW)
  digitalWrite(PIN_BUZZER, HIGH); delay(80); // Mati (HIGH)
  digitalWrite(PIN_BUZZER, LOW);  delay(80); // Nyala (LOW)
  digitalWrite(PIN_BUZZER, HIGH);            // Mati (HIGH)
}

void bunyiBuzzerKunci() {
  digitalWrite(PIN_BUZZER, LOW);  delay(250); // Nyala (LOW)
  digitalWrite(PIN_BUZZER, HIGH);             // Mati (HIGH)
}

void runDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, LOW);  
  hitungGagalScan = 0;                   
  if (!statusTerdeteksiSebelumnya) {
    bunyiBuzzerBuka();
    statusTerdeteksiSebelumnya = true;
    Serial.println("STATUS: Motor Terbuka (Kontak ON).");
  }
}

void runNotDetected() {
  digitalWrite(PIN_RELAY_KEYLESS, HIGH); 
  if (statusTerdeteksiSebelumnya) {
    bunyiBuzzerKunci();
    statusTerdeteksiSebelumnya = false;
    Serial.println("STATUS: Motor Terkunci (Kontak OFF).");
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
    rfidKeyUID = scannedUID;
    rfidAktif = true;
    preferences.begin("wifi_keys", false);
    preferences.putString("rfid_uid", rfidKeyUID);
    preferences.end();
    Serial.printf("[WEB RFID] Kartu Baru Berhasil Direkam! UID: %s\n", rfidKeyUID.c_str());
    digitalWrite(PIN_BUZZER, LOW); delay(200); digitalWrite(PIN_BUZZER, HIGH);
    modeSettingWeb = false;
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return true;
  }

  if (rfidAktif && scannedUID == rfidKeyUID) {
    Serial.printf("[RFID] Kartu Dikenali! UID: %s\n", scannedUID.c_str());
    runDetected();
    berhentiTotalScan = false; 
    waktuMulaiHilang = millis();
    hitungGagalScan = 0;
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
  digitalWrite(PIN_BUZZER, HIGH);        // Kondisi Mati untuk Active LOW di awal

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
      digitalWrite(PIN_BUZZER, LOW); delay(60); digitalWrite(PIN_BUZZER, HIGH);
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