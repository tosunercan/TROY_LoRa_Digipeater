#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h> // veya WiFiClient
#include <esp_task_wdt.h>

const int WDT_TIMEOUT = 30; // 30 saniye

// Web Sunucusu
Preferences preferences;
WebServer server(80);

//************************** Genel Ayarlar ********************
String callSign = "TR0Y-5";
String symbolTable = "L";   // DigiRepeater sembolü
String symbolCode = "#";    // DigiRepeater sembolü
String statusMessage = " TROY LoRa DigiRepeater by TA3OER"; // Preferences'tan yüklenecek
String aprsComment = "LoRa Digirepeater on 433.775MHz";     // Preferences'tan yüklenecek
String header = "<\xff\x01"; // APRS-IS başlığı
String wide = "WIDE1-1";

// APRS-IS konfigürasyonu (global değişkenler)
struct APRSISConfig {
  String server = "rotate.aprs2.net";
  int port = 14580;
  String username = "TR0Y-5";  // Sizin call sign'ınız
  String password = "6121";      // Read-only için -1
  bool enabled = false;
  bool connected = false;
  String filter = "r/40.1479/26.4324/50"; // Çanakkale merkezli 50km filtre
} aprsisConfig;

WiFiClient aprsisClient;
unsigned long lastAPRSISHeartbeat = 0;
unsigned long lastAPRSISConnect = 0;
const unsigned long APRSIX_HEARTBEAT = 30000; // 30 saniye
const unsigned long APRSIX_RECONNECT = 60000; // 1 dakika

// WiFi bağlantı ayarları - mevcut AP ayarlarına ek
String sta_ssid = "TP-Link_4C9E";
String sta_password = "K1o2z3u4l5c6a7";
bool wifiSTAEnabled = false;

// İstatistikler
struct APRSISStats {
  uint32_t sentToAPRSIS = 0;
  uint32_t receivedFromAPRSIS = 0;
  uint32_t connectionAttempts = 0;
  uint32_t connectionFailures = 0;
  String lastError = "";
} aprsisStats;

// WiFi Ayarları
const char* ap_ssid = "TROY-DigiRepeater";
const char* ap_password = "12345678"; // AP şifresi
unsigned long apStartTime = 0;
const unsigned long apDuration = 5 * 60 * 1000; // 5 dakika milisaniye cinsinden
bool apModeActive = false;

// Boot butonu için değişkenler
const int BOOT_BUTTON_PIN = 0; // GPIO0 boot butonu için
unsigned long buttonPressStart = 0;
bool buttonPressed = false;

// LED durumu için
unsigned long lastLedToggle = 0;
const unsigned long ledInterval = 500; // 500ms yanıp sönme
bool ledState = false;

// LoRa Parametreleri
float loraFrequency = 433.775E6;
long loraBandwidth = 125E3;
int loraSpreadingFactor = 12;
int loraCodingRate = 5;
int loraTxPower = 20;

// Paket Kayıt Yapısı
struct PacketRecord {
  int packetNo;
  String packet;
  int rssi;
  float snr;
  String timestamp;
  bool digipeated;
};

const int MAX_PACKETS = 20; // Azaltıldı - memory tasarrufu için
PacketRecord packetHistory[MAX_PACKETS];
int packetIndex = 0;
int totalPacketsReceived = 0;

// İstatistikler
struct {
  uint32_t totalPacketsReceived = 0;
  uint32_t totalPacketsDigipeated = 0;
  uint32_t loraTxCount = 0; // Yeni: LoRa TX paketi sayısı
  uint32_t loraTxFailCount = 0; // Yeni: LoRa TX başarısız paketi sayısı
  int lastRSSI = 0;
  float lastSNR = 0;
  String lastPacket = "";
  String latitude = "4033.48N";
  String longitude = "02644.85E";
} stats;

// Zaman Değişkenleri
unsigned long previousMillis = 0;
int currentMinute = 0;
int lastCheckedMinute = -1; // Periyodik mesajlar için

// LoRa Pinleri
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26
#define GPIO2_PIN 2

uint16_t DigiCount = 1;

// Fonksiyon Tanımları
void Txstatus();
void Txcomment();
void setupWebServer();
void addPacketToHistory(String packet, int rssi, float snr, bool digipeated);
String getUptimeString();
void restartLoRa();
void checkWiFiMode();
void startAPMode();
void stopAPMode();
void updateLedIndicator();
void checkPeriodicTransmissions();
void handleReboot();
void handleRestartLoRa();

void addPacketToHistory(String packet, int rssi, float snr, bool digipeated) {
  packetIndex = (packetIndex + 1) % MAX_PACKETS;
  packetHistory[packetIndex].packetNo = ++totalPacketsReceived;
  packetHistory[packetIndex].packet = packet.substring(0, min((int)packet.length(), 100)); // Paket boyutunu sınırla
  packetHistory[packetIndex].rssi = rssi;
  packetHistory[packetIndex].snr = snr;
  packetHistory[packetIndex].timestamp = String(millis() / 1000) + "s";
  packetHistory[packetIndex].digipeated = digipeated;
}

String getUptimeString() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  return String(days) + "g " + String(hours) + "s " + String(minutes) + "d " + String(seconds) + "sn";
}

void blinkLED(int times, int delayMs = 100) {
  if (!apModeActive) { // AP modunda LED'i karıştırma
    for (int i = 0; i < times; i++) {
      digitalWrite(GPIO2_PIN, HIGH);
      delay(delayMs);
      digitalWrite(GPIO2_PIN, LOW);
      delay(delayMs);
    }
  }
}

// Periyodik iletim kontrolü fonksiyonu
void checkPeriodicTransmissions() {
  // Mevcut dakikayı hesapla (0-59 arası)
  unsigned long currentSeconds = millis() / 1000;
  currentMinute = (currentSeconds / 60) % 60;
  
  // Dakika değişti mi kontrol et
  if (currentMinute != lastCheckedMinute) {
    lastCheckedMinute = currentMinute;
    
    Serial.println("Dakika kontrolü: " + String(currentMinute));
    
    // Txstatus kontrolü - 30. ve 59. dakika
    if (currentMinute == 30 || currentMinute == 59) {
      Serial.println("Periyodik STATUS gönderiliyor (Dakika: " + String(currentMinute) + ")");
      Txstatus();
    }
    
    // Txcomment kontrolü - 5, 25 ve 45. dakikalar
    if (currentMinute == 5 || currentMinute == 25 || currentMinute == 45) {
      Serial.println("Periyodik COMMENT gönderiliyor (Dakika: " + String(currentMinute) + ")");
      Txcomment();
    }
  }
}

// APRS-IS fonksiyonları
bool connectToAPRSIS() {
  if (aprsisClient.connected()) {
    return true;
  }
  
  aprsisStats.connectionAttempts++;
  Serial.println("APRS-IS: " + aprsisConfig.server + ":" + String(aprsisConfig.port) + " bağlanılıyor...");
  
  if (!aprsisClient.connect(aprsisConfig.server.c_str(), aprsisConfig.port)) {
    aprsisStats.connectionFailures++;
    aprsisStats.lastError = "Bağlantı başarısız";
    Serial.println("APRS-IS: Bağlantı başarısız!");
    return false;
  }
  
  // Login paketi gönder
  String loginPacket = "user " + aprsisConfig.username + " pass " + aprsisConfig.password + 
                      " vers TROY LoRa DigiRepeater 1.0 filter " + aprsisConfig.filter + "\r\n";
  
  aprsisClient.print(loginPacket);
  Serial.println("APRS-IS: Login paketi gönderildi");
  
  // Cevap bekle (5 saniye timeout)
  unsigned long timeout = millis() + 5000;
  String response = "";
  
  while (millis() < timeout) {
    if (aprsisClient.available()) {
      response += aprsisClient.readString();
      if (response.indexOf("\n") > 0) break;
    }
    delay(10);
  }
  
  Serial.println("APRS-IS Cevap: " + response);
  
  if (response.indexOf("verified") > 0 || response.indexOf("unverified") > 0) {
    aprsisConfig.connected = true;
    Serial.println("APRS-IS: Başarıyla bağlandı!");
    return true;
  } else {
    aprsisStats.lastError = "Login başarısız: " + response;
    Serial.println("APRS-IS: Login başarısız!");
    aprsisClient.stop();
    return false;
  }
}

void disconnectFromAPRSIS() {
  if (aprsisClient.connected()) {
    aprsisClient.stop();
  }
  aprsisConfig.connected = false;
  Serial.println("APRS-IS: Bağlantı kesildi");
}

bool sendToAPRSIS(String packet) {
  if (!aprsisConfig.enabled || !aprsisConfig.connected) {
    return false;
  }
  
  if (!aprsisClient.connected()) {
    aprsisConfig.connected = false;
    return false;
  }
  
  // APRS-IS format: call>path:data
  // LoRa formatından dönüştür
  if (packet.substring(0, 3) == "\x3c\xff\x01") {
    String aprsPacket = packet.substring(3) + "\r\n";
    
    if (aprsisClient.print(aprsPacket)) {
      aprsisStats.sentToAPRSIS++;
      Serial.println("APRS-IS TX: " + aprsPacket.substring(0, aprsPacket.length()-2));
      return true;
    } else {
      aprsisStats.lastError = "Gönderme başarısız";
      Serial.println("APRS-IS: Paket gönderilemedi!");
      return false;
    }
  }
  
  return false;
}

void processAPRSISData() {
  if (!aprsisConfig.enabled || !aprsisConfig.connected) {
    return;
  }
  
  while (aprsisClient.available()) {
    String line = aprsisClient.readStringUntil('\n');
    line.trim();
    
    if (line.length() < 5) continue;
    
    // Heartbeat mesajlarını atla
    if (line.startsWith("#")) {
      Serial.println("APRS-IS Heartbeat: " + line);
      continue;
    }
    
    // APRS paketini LoRa formatına çevir
    String loraPacket = header + line;
    
    Serial.println("APRS-IS RX: " + line);
    
    aprsisStats.receivedFromAPRSIS++;
    addPacketToHistory("INET: " + line, 0, 0, false);
    
    // APRS-IS'ten gelen paketleri LoRa ağına yayınla (Çift yönlü Gateway için)
    /*
    delay(random(100, 500)); 
    LoRa.beginPacket();
    LoRa.print(loraPacket);
    if (LoRa.endPacket()) {
      Serial.println("APRS-IS paketi LoRa ağına yayınlandı.");
      stats.loraTxCount++;
    } else {
      Serial.println("APRS-IS paketi LoRa ağına yayınlanırken hata oluştu.");
      stats.loraTxFailCount++;
    }
    */
  }
}

void manageAPRSIS() {
  unsigned long currentTime = millis();
  
  if (!aprsisConfig.enabled || !wifiSTAEnabled || WiFi.status() != WL_CONNECTED) {
    if (aprsisConfig.connected) {
      disconnectFromAPRSIS();
    }
    return;
  }
  
  // Bağlantı kontrolü
  if (!aprsisConfig.connected || !aprsisClient.connected()) {
    if (currentTime - lastAPRSISConnect > APRSIX_RECONNECT) {
      lastAPRSISConnect = currentTime;
      connectToAPRSIS();
    }
    return;
  }
  
  // Heartbeat gönder
  if (currentTime - lastAPRSISHeartbeat > APRSIX_HEARTBEAT) {
    lastAPRSISHeartbeat = currentTime;
    aprsisClient.println("# TROY DigiRepeater heartbeat");
  }
  
  // Gelen data işle
  processAPRSISData();
}

// WiFi STA bağlantısı - düzeltildi
bool connectToWiFiSTA() {
  if (sta_ssid.length() == 0) return false;
  
  Serial.println("WiFi STA: " + sta_ssid + " bağlanılıyor...");
  
  // Mevcut WiFi bağlantısını kes
  WiFi.disconnect(true);
  delay(1000);
  
  WiFi.mode(WIFI_AP_STA); // Hem AP hem STA
  WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi STA bağlandı!");
    Serial.println("IP: " + WiFi.localIP().toString());
    wifiSTAEnabled = true;
    
    // Web sunucusunu başlat (eğer başlatılmamışsa)
    server.begin();
    Serial.println("Web sunucusu STA modunda başlatıldı");
    
    // APRS-IS bağlantısını başlat
    if (aprsisConfig.enabled) {
      connectToAPRSIS();
    }
    
    return true;
  } else {
    Serial.println();
    Serial.println("WiFi STA bağlantısı başarısız!");
    wifiSTAEnabled = false;
    return false;
  }
}

// Preferences'a APRS-IS ayarlarını kaydetme
void loadAPRSISSettings() {
  // Yeni: Status ve Comment mesajlarını yükle
  statusMessage = preferences.getString("status_msg", statusMessage);
  aprsComment = preferences.getString("aprs_comment", aprsComment);
  
  // Mevcut APRS-IS ve WiFi ayarlarını yükle
  aprsisConfig.server = preferences.getString("aprs_server", aprsisConfig.server);
  aprsisConfig.port = preferences.getInt("aprs_port", aprsisConfig.port);
  aprsisConfig.username = preferences.getString("aprs_user", aprsisConfig.username);
  aprsisConfig.password = preferences.getString("aprs_pass", aprsisConfig.password);
  aprsisConfig.filter = preferences.getString("aprs_filter", aprsisConfig.filter);
  aprsisConfig.enabled = preferences.getBool("aprs_enabled", aprsisConfig.enabled);
  
  sta_ssid = preferences.getString("sta_ssid", sta_ssid);
  sta_password = preferences.getString("sta_pass", sta_password);
}

void saveAPRSISSettings() {
  // Yeni: Status ve Comment mesajlarını kaydet
  preferences.putString("status_msg", statusMessage);
  preferences.putString("aprs_comment", aprsComment);

  // Mevcut APRS-IS ve WiFi ayarlarını kaydet
  preferences.putString("aprs_server", aprsisConfig.server);
  preferences.putInt("aprs_port", aprsisConfig.port);
  preferences.putString("aprs_user", aprsisConfig.username);
  preferences.putString("aprs_pass", aprsisConfig.password);
  preferences.putString("aprs_filter", aprsisConfig.filter);
  preferences.putBool("aprs_enabled", aprsisConfig.enabled);
  
  preferences.putString("sta_ssid", sta_ssid);
  preferences.putString("sta_pass", sta_password);
}

void restartLoRa() {
  Serial.println("LoRa yeniden başlatılıyor...");
  
  // Önce mevcut LoRa bağlantısını temiz bir şekilde kapat
  LoRa.end();
  delay(1000); // Daha uzun bekleme süresi
  
  // Reset pinini kullanarak hard reset
  digitalWrite(LORA_RST, LOW);
  delay(100);
  digitalWrite(LORA_RST, HIGH);
  delay(100);
  
  // SPI'yi yeniden initialize et
  SPI.end();
  delay(100);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  
  // LoRa pinlerini yeniden ayarla
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  
  // LoRa'yı başlat - birkaç deneme yap
  int attempts = 0;
  const int maxAttempts = 5;
  bool success = false;
  
  while (attempts < maxAttempts && !success) {
    attempts++;
    Serial.println("LoRa başlatma denemesi: " + String(attempts));
    
    if (LoRa.begin(loraFrequency)) {
      success = true;
      Serial.println("LoRa başarıyla başlatıldı!");
    } else {
      Serial.println("Deneme " + String(attempts) + " başarısız, tekrar deneniyor...");
      delay(1000);
    }
  }
  
  if (!success) {
    Serial.println("HATA: LoRa " + String(maxAttempts) + " denemede başlatılamadı!");
    Serial.println("Lütfen ayarları kontrol edin ve sistemi yeniden başlatın.");
    return;
  }
  
  // Ayarları uygula
  LoRa.setSpreadingFactor(loraSpreadingFactor);
  LoRa.setSignalBandwidth(loraBandwidth);
  LoRa.setCodingRate4(loraCodingRate);
  LoRa.setTxPower(loraTxPower);
  LoRa.enableCrc();
  
  Serial.println("LoRa parametreleri uygulandı:");
  Serial.println("- Frekans: " + String(loraFrequency / 1E6, 3) + " MHz");
  Serial.println("- Bant Genişliği: " + String(loraBandwidth / 1E3) + " kHz");
  Serial.println("- Spreading Factor: " + String(loraSpreadingFactor));
  Serial.println("- Coding Rate: 4/" + String(loraCodingRate));
  Serial.println("- TX Power: " + String(loraTxPower) + " dBm");
  
  Serial.println("LoRa yeniden başlatıldı!");
  
  // Başarı LED'i
  blinkLED(3, 200);
}

void checkWiFiMode() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) { // Butona basılıyor
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
    } else if (millis() - buttonPressStart >= 3000 && !apModeActive) {
      // 3 saniye basılı tutuldu
      startAPMode();
    }
  } else {
    buttonPressed = false;
  }

  // AP modu zaman aşımı kontrolü
  if (apModeActive && millis() - apStartTime >= apDuration) {
    stopAPMode();
  }
}

void startAPMode() {
  Serial.println("AP modu başlatılıyor...");
  
  WiFi.mode(WIFI_AP_STA); // Hem AP hem STA modunu koru
  
  if (!WiFi.softAP(ap_ssid, ap_password)) {
    Serial.println("AP başlatılamadı!");
    return;
  }
  
  // WebServer'ı başlat
  server.begin();
  Serial.println("Web sunucusu AP modunda başlatıldı");
  
  apStartTime = millis();
  apModeActive = true;
  
  Serial.print("AP başlatıldı. SSID: ");
  Serial.println(ap_ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  blinkLED(3, 100);
}

void stopAPMode() {
  Serial.println("AP modu kapatılıyor...");
  WiFi.softAPdisconnect(true);
  apModeActive = false;
  digitalWrite(GPIO2_PIN, LOW);
  Serial.println("AP modu kapatıldı.");
}

void updateLedIndicator() {
  if (apModeActive) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastLedToggle >= ledInterval) {
      lastLedToggle = currentMillis;
      ledState = !ledState;
      digitalWrite(GPIO2_PIN, ledState ? HIGH : LOW);
    }
  }
}

// Ana sayfa - memory efficient
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  
  server.sendContent("<!DOCTYPE html><html><head><title>TROY LoRa DigiRepeater</title>");
  server.sendContent("<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>");
  server.sendContent("<style>body{font-family:Arial;margin:20px;background:#f5f5f5}");
  server.sendContent(".container{max-width:1000px;margin:0 auto}");
  server.sendContent(".header{background:#2c3e50;color:white;padding:20px;border-radius:5px}");
  server.sendContent(".header .btn-group{margin-top:10px;}"); // Yeni: Buton grubu için boşluk
  server.sendContent(".card{background:white;padding:20px;margin:15px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}");
  server.sendContent("table{width:100%;border-collapse:collapse;margin-top:10px}");
  server.sendContent("th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd}");
  server.sendContent("tr:nth-child(even){background-color:#f2f2f2}");
  server.sendContent(".digi{background-color:#dff0d8!important}");
  server.sendContent(".btn{display:inline-block;padding:8px 15px;background:#3498db;color:white;text-decoration:none;border-radius:4px;margin:5px 5px 5px 0;}"); // Yeni: Buton sağında boşluk
  server.sendContent(".warn-btn{background:#e74c3c;} .success-btn{background:#2ecc71;}");
  server.sendContent("</style>");
  server.sendContent("<script>setTimeout(function(){ location.reload(); }, 30000);</script>"); // Yeni: 30 saniyede bir yenile
  server.sendContent("</head><body><div class='container'>");
  
  server.sendContent("<div class='header'><h1>TROY LoRa DigiRepeater</h1>");
  // Yeni: LoRa parametreleri eklendi
  server.sendContent("<p>Digi Callsign: <strong>" + callSign + "</strong> | Freq: <strong>" + String(loraFrequency / 1E6, 3) + " MHz</strong> | " +
                      "SF" + String(loraSpreadingFactor) + ", BW" + String(loraBandwidth / 1E3) + "kHz, CR4/" + String(loraCodingRate) + ", TX" + String(loraTxPower) + "dBm</p>");
  
  // Yeni: Yönetim butonları header altına taşındı
  server.sendContent("<div class='btn-group'>");
  server.sendContent("<a href='/config' class='btn success-btn'>Yapılandırma</a> ");
  server.sendContent("<a href='/status' class='btn'>Durum Gönder</a> ");
  server.sendContent("<a href='/comment' class='btn'>Yorum Gönder</a> ");
  server.sendContent("<a href='/restartlora' class='btn warn-btn'>LoRa Modülünü Yeniden Başlat</a> ");
  server.sendContent("<a href='/reboot' class='btn warn-btn'>Cihazı Yeniden Başlat</a>");
  server.sendContent("</div></div>"); // .btn-group ve .header divlerini kapat

  server.sendContent("<div class='card'><h2>İstatistikler</h2>");
  server.sendContent("<p>Uptime: <strong>" + getUptimeString() + "</strong></p>");
  server.sendContent("<p>RX: <strong>" + String(stats.totalPacketsReceived) + "</strong> | DIGI: <strong>" + String(stats.totalPacketsDigipeated) + "</strong></p>");
  server.sendContent("<p>LoRa TX: <strong>" + String(stats.loraTxCount) + "</strong> | LoRa TX Hata: <strong>" + String(stats.loraTxFailCount) + "</strong></p>");
  server.sendContent("<p>Son RSSI: <strong>" + String(stats.lastRSSI) + " dBm</strong> | Son SNR: <strong>" + String(stats.lastSNR) + " dB</strong></p>");
  server.sendContent("<p>Mevcut Dakika: <strong>" + String(currentMinute) + "</strong></p></div>");
  
  server.sendContent("<div class='card'><h2>APRS-IS Gateway</h2>");
  server.sendContent("<p>APRS-IS: <strong>" + String(aprsisConfig.enabled ? "Aktif" : "Kapalı") + "</strong> | " +
                  "Bağlantı: <strong>" + String(aprsisConfig.connected ? "Bağlı" : "Bağlı Değil") + "</strong></p>");
  if (aprsisConfig.enabled) {
    server.sendContent("<p>Sunucu: <strong>" + aprsisConfig.server + ":" + String(aprsisConfig.port) + "</strong></p>");
    server.sendContent("<p>Gönderilen: <strong>" + String(aprsisStats.sentToAPRSIS) + "</strong> | ");
    server.sendContent("Alınan: <strong>" + String(aprsisStats.receivedFromAPRSIS) + "</strong></p>");
    if (aprsisStats.lastError.length() > 0) {
      server.sendContent("<p>Son Hata: <strong>" + aprsisStats.lastError + "</strong></p>");
    }
  }
  server.sendContent("<p>WiFi STA: <strong>" + String(wifiSTAEnabled ? WiFi.localIP().toString() : "Bağlı değil") + "</strong></p>");
  server.sendContent("</div>");

server.sendContent("<div class='card'><h2>Son Paketler</h2><table>");
server.sendContent("<tr><th>#</th><th>Zaman</th><th>RSSI</th><th>SNR</th><th>Durum</th><th>Paket</th></tr>");

for (int i = 0; i < MAX_PACKETS; i++) {
  int idx = (packetIndex - i + MAX_PACKETS) % MAX_PACKETS;
  if (packetHistory[idx].packetNo > 0) {
    String statusText = packetHistory[idx].digipeated ? "DİGİ" : "RX";
    String rowClass = packetHistory[idx].digipeated ? " class='digi'" : "";
    
    server.sendContent("<tr" + rowClass + ">");
    server.sendContent("<td>" + String(packetHistory[idx].packetNo) + "</td>");
    server.sendContent("<td>" + packetHistory[idx].timestamp + "</td>");
    server.sendContent("<td>" + String(packetHistory[idx].rssi) + "</td>");
    server.sendContent("<td>" + String(packetHistory[idx].snr) + "</td>");
    server.sendContent("<td><strong>" + statusText + "</strong></td>");
    server.sendContent("<td>" + packetHistory[idx].packet + "</td>");
    server.sendContent("</tr>");
  }
}
server.sendContent("</table></div>");

  // Eski yönetim butonu kartı kaldırıldı.
  // server.sendContent("<div class='card'><h2>Yönetim</h2>");
  // server.sendContent("<a href='/config' class='btn'>Yapılandırma</a> ");
  // server.sendContent("<a href='/status' class='btn'>Durum Gönder</a> ");
  // server.sendContent("<a href='/comment' class='btn'>Yorum Gönder</a><br>");
  // server.sendContent("<a href='/restartlora' class='btn warn-btn'>LoRa Modülünü Yeniden Başlat</a> ");
  // server.sendContent("<a href='/reboot' class='btn warn-btn'>Cihazı Yeniden Başlat</a></div>");
  server.sendContent("</div></body></html>");
  server.sendContent("");
}

// Yapılandırma sayfası - düzeltildi
void handleConfig() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  
  server.sendContent("<!DOCTYPE html><html><head><title>Yapılandırma</title>");
  server.sendContent("<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>");
  server.sendContent("<style>body{font-family:Arial;margin:20px;background:#f5f5f5}");
  server.sendContent(".container{max-width:600px;margin:0 auto}");
  server.sendContent(".card{background:white;padding:20px;margin:15px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}");
  server.sendContent(".form-group{margin-bottom:15px}");
  server.sendContent("label{display:block;margin-bottom:5px;font-weight:bold}");
  server.sendContent("input,textarea,select{width:100%;padding:8px;box-sizing:border-box}");
  server.sendContent(".btn{display:inline-block;padding:8px 15px;background:#27ae60;color:white;text-decoration:none;border-radius:4px;border:none;cursor:pointer}");
  server.sendContent("</style></head><body><div class='container'>");

  server.sendContent("<div class='card'><h1>Yapılandırma</h1><form method='POST' action='/saveconfig'>");

  server.sendContent("<h3>WiFi İnternet Bağlantısı</h3>");
  server.sendContent("<div class='form-group'><label>WiFi Ağ Adı (SSID):</label>");
  server.sendContent("<input type='text' name='sta_ssid' value='" + sta_ssid + "'></div>");
  server.sendContent("<div class='form-group'><label>WiFi Şifresi:</label>");
  server.sendContent("<input type='password' name='sta_password' value='" + sta_password + "'></div>");

  server.sendContent("<h3>Digipeater Genel Ayarları</h3>");
  server.sendContent("<div class='form-group'><label>Çağrı İşareti (Call Sign):</label>");
  server.sendContent("<input type='text' name='callsign' value='" + callSign + "'></div>");
  
  server.sendContent("<div class='form-group'><label>Durum Mesajı (Status Message):</label>");
  server.sendContent("<input type='text' name='status_message' value='" + statusMessage + "'></div>");
  
  server.sendContent("<div class='form-group'><label>Yorum Mesajı (Comment Message):</label>");
  server.sendContent("<input type='text' name='aprs_comment' value='" + aprsComment + "'></div>");

  server.sendContent("<h3>APRS-IS Gateway</h3>");
  server.sendContent("<div class='form-group'><label>APRS-IS Aktif:</label>");
  server.sendContent("<input type='checkbox' name='aprs_enabled' value='1'" + String(aprsisConfig.enabled ? " checked" : "") + "></div>");

  server.sendContent("<div class='form-group'><label>APRS-IS Sunucu:</label>");
  server.sendContent("<input type='text' name='aprs_server' value='" + aprsisConfig.server + "'></div>");

  server.sendContent("<div class='form-group'><label>APRS-IS Port:</label>");
  server.sendContent("<input type='number' name='aprs_port' value='" + String(aprsisConfig.port) + "'></div>");

  server.sendContent("<div class='form-group'><label>APRS-IS Kullanıcı Adı:</label>");
  server.sendContent("<input type='text' name='aprs_username' value='" + aprsisConfig.username + "'></div>");

  server.sendContent("<div class='form-group'><label>APRS-IS Şifre:</label>");
  server.sendContent("<input type='text' name='aprs_password' value='" + aprsisConfig.password + "'></div>");

  server.sendContent("<div class='form-group'><label>APRS-IS Filtre:</label>");
  server.sendContent("<input type='text' name='aprs_filter' value='" + aprsisConfig.filter + "'></div>");

  server.sendContent("<h3>LoRa Parametreleri</h3>");
  server.sendContent("<div class='form-group'><label>Frekans (MHz):</label>");
  server.sendContent("<input type='number' step='0.001' name='lora_frequency' value='" + String(loraFrequency / 1E6, 3) + "'></div>");

  server.sendContent("<div class='form-group'><label>Spreading Factor:</label>");
  server.sendContent("<select name='lora_sf'>");
  for (int sf = 7; sf <= 12; sf++) {
    server.sendContent("<option value='" + String(sf) + "'" + String(sf == loraSpreadingFactor ? " selected" : "") + ">SF" + String(sf) + "</option>");
  }
  server.sendContent("</select></div>");

  server.sendContent("<div class='form-group'><label>Bant Genişliği (kHz):</label>");
  server.sendContent("<select name='lora_bw'>");
  int bwValues[] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};
  String bwLabels[] = {"7.8", "10.4", "15.6", "20.8", "31.25", "41.7", "62.5", "125", "250", "500"};
  for (int i = 0; i < 10; i++) {
    server.sendContent("<option value='" + String(bwValues[i]) + "'" + String(bwValues[i] == loraBandwidth ? " selected" : "") + ">" + bwLabels[i] + " kHz</option>");
  }
  server.sendContent("</select></div>");

  server.sendContent("<div class='form-group'><label>Coding Rate:</label>");
  server.sendContent("<select name='lora_cr'>");
  for (int cr = 5; cr <= 8; cr++) {
    server.sendContent("<option value='" + String(cr) + "'" + String(cr == loraCodingRate ? " selected" : "") + ">4/" + String(cr) + "</option>");
  }
  server.sendContent("</select></div>");

  server.sendContent("<div class='form-group'><label>TX Power (dBm):</label>");
  server.sendContent("<input type='number' min='2' max='20' name='lora_power' value='" + String(loraTxPower) + "'></div>");

  server.sendContent("<button type='submit' class='btn'>Kaydet ve Yeniden Başlat</button>");
  server.sendContent("</form></div>");

  server.sendContent("<div class='card'><a href='/' class='btn'>Ana Sayfa</a></div>");
  server.sendContent("</div></body></html>");
  server.sendContent("");
}

// Yapılandırma kaydetme
void handleSaveConfig() {
  // WiFi ayarları
  if (server.hasArg("sta_ssid")) {
    sta_ssid = server.arg("sta_ssid");
  }
  if (server.hasArg("sta_password")) {
    sta_password = server.arg("sta_password");
  }

  // Çağrı İşareti
  if (server.hasArg("callsign")) {
    callSign = server.arg("callsign");
  }

  // Durum ve Yorum mesajlarını oku
  if (server.hasArg("status_message")) {
    statusMessage = server.arg("status_message");
  }
  if (server.hasArg("aprs_comment")) {
    aprsComment = server.arg("aprs_comment");
  }

  // APRS-IS ayarları
  aprsisConfig.enabled = server.hasArg("aprs_enabled");
  if (server.hasArg("aprs_server")) {
    aprsisConfig.server = server.arg("aprs_server");
  }
  if (server.hasArg("aprs_port")) {
    aprsisConfig.port = server.arg("aprs_port").toInt();
  }
  if (server.hasArg("aprs_username")) {
    aprsisConfig.username = server.arg("aprs_username");
  }
  if (server.hasArg("aprs_password")) {
    aprsisConfig.password = server.arg("aprs_password");
  }
  if (server.hasArg("aprs_filter")) {
    aprsisConfig.filter = server.arg("aprs_filter");
  }

  // LoRa ayarları
  if (server.hasArg("lora_frequency")) {
    loraFrequency = server.arg("lora_frequency").toFloat() * 1E6;
  }
  if (server.hasArg("lora_sf")) {
    loraSpreadingFactor = server.arg("lora_sf").toInt();
  }
  if (server.hasArg("lora_bw")) {
    loraBandwidth = server.arg("lora_bw").toInt();
  }
  if (server.hasArg("lora_cr")) {
    loraCodingRate = server.arg("lora_cr").toInt();
  }
  if (server.hasArg("lora_power")) {
    loraTxPower = server.arg("lora_power").toInt();
  }

  // Ayarları kaydet
  saveAPRSISSettings();
  preferences.putString("callsign", callSign);
  preferences.putFloat("lora_freq", loraFrequency);
  preferences.putInt("lora_sf", loraSpreadingFactor);
  preferences.putLong("lora_bw", loraBandwidth);
  preferences.putInt("lora_cr", loraCodingRate);
  preferences.putInt("lora_power", loraTxPower);

  server.send(200, "text/html", 
    "<!DOCTYPE html><html><head><title>Ayarlar Kaydedildi</title><meta charset='UTF-8'></head>"
    "<body><h1>Ayarlar Kaydedildi</h1>"
    "<p>Ayarlar başarıyla kaydedildi. Sistem yeniden başlatılıyor...</p>"
    "<p><a href='/'>Ana Sayfa</a></p></body></html>");

  delay(2000);
  ESP.restart();
}

// Status ve Comment sayfaları
void handleStatus() {
  server.send(200, "text/html", 
    "<!DOCTYPE html><html><head><title>Status Gönderildi</title><meta charset='UTF-8'></head>"
    "<body><h1>Status Paketi Gönderildi</h1><p><a href='/'>Ana Sayfa</a></p></body></html>");
  Txstatus();
}

void handleComment() {
  server.send(200, "text/html", 
    "<!DOCTYPE html><html><head><title>Yorum Gönderildi</title><meta charset='UTF-8'></head>"
    "<body><h1>Yorum Paketi Gönderildi</h1><p><a href='/'>Ana Sayfa</a></p></body></html>");
  Txcomment();
}

// Yeni: Cihazı yeniden başlatma handler'ı
void handleReboot() {
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><title>Yeniden Başlatılıyor</title><meta charset='UTF-8'></head>"
    "<body><h1>Cihaz Yeniden Başlatılıyor...</h1><p>Lütfen bekleyiniz.</p></body></html>");
  delay(1000);
  ESP.restart();
}

// Yeni: LoRa modülünü yeniden başlatma handler'ı
void handleRestartLoRa() {
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><title>LoRa Yeniden Başlatılıyor</title><meta charset='UTF-8'></head>"
    "<body><h1>LoRa Modülü Yeniden Başlatılıyor...</h1><p>Lütfen bekleyiniz.</p><p><a href='/'>Ana Sayfa</a></p></body></html>");
  delay(100); // Cevabın gönderildiğinden emin olmak için
  restartLoRa(); // LoRa'yı yeniden başlatma fonksiyonunu çağır
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/saveconfig", handleSaveConfig);
  server.on("/status", handleStatus);
  server.on("/comment", handleComment);
  server.on("/reboot", handleReboot);
  server.on("/restartlora", handleRestartLoRa);
  
  server.onNotFound([]() {
    server.send(404, "text/plain", "Sayfa bulunamadi");
  });
}

// APRS fonksiyonları
void Txstatus() {
  String statusPacket = callSign + ">APLERT," + wide + ":=" + stats.latitude + symbolTable + stats.longitude + symbolCode + statusMessage;
  String packet = header + statusPacket;
  
  Serial.println("TX STATUS: " + statusPacket);
  
  LoRa.beginPacket();
  LoRa.print(packet);
  if (LoRa.endPacket()) {
    stats.loraTxCount++;
    blinkLED(2, 100);
  } else {
    stats.loraTxFailCount++;
    Serial.println("LoRa STATUS TX FAILED!");
  }
  
  // APRS-IS'e gönder
  sendToAPRSIS(packet);
}

void Txcomment() {
  String commentPacket = callSign + ">APLERT," + wide + ":>" + aprsComment;
  String packet = header + commentPacket;
  
  Serial.println("TX COMMENT: " + commentPacket);
  
  LoRa.beginPacket();
  LoRa.print(packet);
  if (LoRa.endPacket()) {
    stats.loraTxCount++;
    blinkLED(2, 100);
  } else {
    stats.loraTxFailCount++;
    Serial.println("LoRa COMMENT TX FAILED!");
  }
  
  // APRS-IS'e gönder
  sendToAPRSIS(packet);
}

// Paket işleme fonksiyonu
bool shouldDigipeat(String packet) {
    if (packet.length() < 3 || packet.substring(0, 3) != header) {
        return false;
    }
    
    if (packet.indexOf("TCPIP") != -1 || packet.indexOf("NOGATE") != -1) {
        return false;
    }
    
    String aprsContent = packet.substring(3);
    int greaterPos = aprsContent.indexOf('>');
    int colonPos = aprsContent.indexOf(':');
    
    if (greaterPos == -1 || colonPos == -1) {
        return false;
    }
    
    String sender = aprsContent.substring(0, greaterPos);
    if (sender == callSign) {
        return false;
    }
    
    String pathPart = aprsContent.substring(greaterPos + 1, colonPos);
    
    // Basit mantık:
    // 1. Path'de bizim callsign'ımız "*" ile varsa -> Zaten işlemişiz
    if (pathPart.indexOf(callSign + "*") != -1) {
        return false;
    }
    
    // 2. WIDE1-1 varsa ve WIDE1* yoksa -> Digipeat et
    if (pathPart.indexOf("WIDE1-1") != -1 && pathPart.indexOf("WIDE1*") == -1) {
        return true;
    }
    
    return false;
}

//TA3ABC>APLERT,WIDE1-1:>Test → Digipeat edilir
//TA3ABC>APLERT,WIDE1*:>Test → Edilmez (başkası etmiş)
//TA3ABC>APLERT,TR0Y-5*,WIDE1-1:>Test → Edilmez (biz etmişiz)
//TA3ABC>APLERT,TR0Y-5*:>Test → Edilmez (WIDE1-1 yok)
//TR0Y-5>APLERT,WIDE1-1:>Test → Edilmez (kendi paketimiz)
//TA3ABC>APLERT,WIDE1-1,WIDE2-2:>Test → Digipeat edilir

String processDigipeat(String packet) {
  String processedPacket = packet;
  String aprsContent = packet.substring(3); // Header'ı çıkar
  
  // WIDE1-1'i callSign + "*" ile değiştir
  // Sadece path kısmında değiştirildiğinden emin olalım
  int greaterPos = aprsContent.indexOf('>');
  int colonPos = aprsContent.indexOf(':', greaterPos); // > sonrası ilk kolon
  
  if (greaterPos != -1 && colonPos != -1) {
    String path = aprsContent.substring(greaterPos + 1, colonPos);
    if (path.indexOf("WIDE1-1") != -1) {
      path.replace("WIDE1-1", callSign + "*");
      processedPacket = header + aprsContent.substring(0, greaterPos + 1) + path + aprsContent.substring(colonPos);
    }
  }
  
  return processedPacket;
}

// Gelen paketlerdeki digipeater'ları tespit etme fonksiyonu
bool isDigipeater(String callsign) {
  return callsign.endsWith("*");
}

// Paket path'ini analiz etme fonksiyonu
String analyzePath(String packet) {
  if (packet.length() < 3 || packet.substring(0, 3) != header) {
    return "Invalid packet format";
  }
  
  String aprsContent = packet.substring(3);
  int colonPos = aprsContent.indexOf(':');
  if (colonPos == -1) {
    return "No payload found";
  }
  
  String header_part = aprsContent.substring(0, colonPos);
  int greaterPos = header_part.indexOf('>');
  if (greaterPos == -1) {
    return "Invalid header format";
  }
  
  String sender = header_part.substring(0, greaterPos);
  String path = header_part.substring(greaterPos + 1);
  
  String analysis = "Sender: " + sender + " | Path: " + path;
  
  // Path'teki digipeater'ları analiz et
  if (path.indexOf("*") != -1) {
    analysis += " | Digipeated by: ";
    int start = 0;
    int comma = path.indexOf(',', start);
    
    while (comma != -1 || start < path.length()) {
      String hop = (comma != -1) ? path.substring(start, comma) : path.substring(start);
      hop.trim();
      
      if (isDigipeater(hop)) {
        String digipeater = hop.substring(0, hop.length() - 1); // * işaretini çıkar
        analysis += digipeater + " ";
      }
      
      if (comma == -1) break;
      start = comma + 1;
      comma = path.indexOf(',', start);
    }
  }
  
  return analysis;
}

// LoRa paket işleme fonksiyonu
void handleLoRaPacket() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;
  
  String receivedPacket = "";
  while (LoRa.available()) {
    receivedPacket += (char)LoRa.read();
  }
  
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  
  stats.lastRSSI = rssi;
  stats.lastSNR = snr;
  stats.lastPacket = receivedPacket;
  stats.totalPacketsReceived++;
  
  // Paket analizini yazdır
  String pathAnalysis = analyzePath(receivedPacket);
  Serial.println("RX: " + receivedPacket);
  Serial.println("Analysis: " + pathAnalysis);
  Serial.println("RSSI:" + String(rssi) + " SNR:" + String(snr));
  
  // APRS-IS'e gönder
  if (sendToAPRSIS(receivedPacket)) {
    Serial.println("Paket APRS-IS'e gönderildi");
  }
  
  // Digipeat kontrolü
  bool digipeated = false;
  if (shouldDigipeat(receivedPacket)) {
    String digiPacket = processDigipeat(receivedPacket);
    
    delay(random(500, 2000)); // Collision avoidance
    
    LoRa.beginPacket();
    LoRa.print(digiPacket);
    if (LoRa.endPacket()) {
      stats.totalPacketsDigipeated++;
      stats.loraTxCount++;
      digipeated = true;
      Serial.println("DIGI TX: " + digiPacket);
      Serial.println("Digipeated for: " + analyzePath(receivedPacket));
      blinkLED(1, 50);
    } else {
      stats.loraTxFailCount++;
      Serial.println("LoRa DIGI TX FAILED!");
    }
  } else {
    String reason = "";
    if (receivedPacket.indexOf(callSign) >= 0) {
      reason = "Own packet";
    } else if (receivedPacket.indexOf("WIDE1*") != -1) {
      reason = "Already digipeated";
    } else if (receivedPacket.indexOf("WIDE1-1") == -1) {
      reason = "No WIDE1-1 found";
    } else {
      reason = "Other reason";
    }
    Serial.println("Not digipeated - " + reason);
  }
  
  addPacketToHistory(receivedPacket, rssi, snr, digipeated);
}
 
// Ana setup fonksiyonu
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Watchdog Timer'ı başlat
  esp_task_wdt_init(WDT_TIMEOUT, true);  // true = panic on timeout (reboot)
  esp_task_wdt_add(NULL);                // Mevcut task'ı ekle
  Serial.println("Watchdog Timer aktif - " + String(WDT_TIMEOUT) + " saniye timeout");
   
  Serial.println("TROY LoRa DigiRepeater v1.0 başlatılıyor...");
  Serial.println("by TA3OER");
  
  // Preferences başlat
  preferences.begin("digirepeater", false);
  
  // GPIO ayarları
  pinMode(GPIO2_PIN, OUTPUT);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(GPIO2_PIN, LOW);
  
  // Kaydedilmiş ayarları yükle
  loadAPRSISSettings();
  callSign = preferences.getString("callsign", callSign);
  loraFrequency = preferences.getFloat("lora_freq", loraFrequency);
  loraSpreadingFactor = preferences.getInt("lora_sf", loraSpreadingFactor);
  loraBandwidth = preferences.getLong("lora_bw", loraBandwidth);
  loraCodingRate = preferences.getInt("lora_cr", loraCodingRate);
  loraTxPower = preferences.getInt("lora_power", loraTxPower);
  
  // SPI ayarları
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  
  // LoRa ayarları
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  
  if (!LoRa.begin(loraFrequency)) {
    Serial.println("LoRa başlatılamadı!");
    while (1) {
      blinkLED(5, 200);
      delay(2000);
    }
  }
  
  // LoRa parametrelerini ayarla
  LoRa.setSpreadingFactor(loraSpreadingFactor);
  LoRa.setSignalBandwidth(loraBandwidth);
  LoRa.setCodingRate4(loraCodingRate);
  LoRa.setTxPower(loraTxPower);
  LoRa.enableCrc();
  
  Serial.println("LoRa başlatıldı:");
  Serial.println("- Frekans: " + String(loraFrequency / 1E6, 3) + " MHz");
  Serial.println("- Bant Genişliği: " + String(loraBandwidth / 1E3) + " kHz");
  Serial.println("- Spreading Factor: " + String(loraSpreadingFactor));
  Serial.println("- Coding Rate: 4/" + String(loraCodingRate));
  Serial.println("- TX Power: " + String(loraTxPower) + " dBm");
  
  // Web sunucusunu ayarla
  setupWebServer();
  
  // WiFi STA bağlantısını dene
  if (sta_ssid.length() > 0) {
    connectToWiFiSTA();
  }
  
  // AP modunu başlat (her zaman aktif)
  startAPMode();
  
  // Başlangıç paketleri
  delay(2000);
  Txstatus();
  delay(2000);
  Txcomment();
  
  Serial.println("Sistem hazır!");
  blinkLED(3, 100);
}

// Ana loop fonksiyonu
void loop() {
  // Her döngü başında "Ben çalışıyorum" sinyali gönder
  esp_task_wdt_reset();

  // Web sunucusu
  server.handleClient();
  
  // LoRa paket kontrolü
  handleLoRaPacket();
  
  // WiFi mod kontrolü
  checkWiFiMode();
  
  // LED göstergesi
  updateLedIndicator();
  
  // APRS-IS yönetimi
  manageAPRSIS();
  
  // Periyodik iletim kontrolü
  checkPeriodicTransmissions();
  
  // Küçük gecikme
  delay(10);
}