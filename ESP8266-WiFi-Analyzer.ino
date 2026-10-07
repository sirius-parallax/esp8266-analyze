#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =====
IPAddress IP;
const char* ap_ssid = "HP-Print-54";      
const char* ap_password = "14725836";     

// ===== НАСТРОЙКИ ЭКРАНА =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== СЕРВЕРЫ =====
DNSServer dnsServer;
ESP8266WebServer server(80);

// ===== HTML-СТРАНИЦА =====
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Device Control</title>
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 15px; text-align: center; }
    h1 { font-size: 20px; color: #fff; margin-bottom: 15px; }
    .card { background: #1e1e1e; padding: 15px; margin-bottom: 15px; border-radius: 10px; border: 1px solid #333; }
    .info-row { display: flex; justify-content: space-between; margin: 5px 0; font-size: 14px; }
    .info-row b { color: #0f0; }
    .btn { padding: 12px 25px; margin: 5px; background: #0f0; color: #000; border: none; border-radius: 5px; font-weight: bold; cursor: pointer; font-size: 16px; }
    .btn:active { background: #0c0; transform: scale(0.95); }
    .client-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #333; }
    .client-row:last-child { border-bottom: none; }
    .client-info { text-align: left; flex: 1; }
    .client-mac { color: #0f0; font-family: monospace; font-size: 13px; }
    .client-ip { color: #00ffff; font-family: monospace; font-size: 12px; margin-top: 3px; }
    .client-num { color: #888; font-size: 14px; margin-right: 10px; }
    .status { color: #0f0; font-weight: bold; }
  </style>
</head>
<body>
  <h1> Device Control Panel</h1>
  
  <div class="card">
    <div class="info-row"><span>Network:</span> <b>%SSID%</b></div>
    <div class="info-row"><span>IP:</span> <b>%IP%</b></div>
    <div class="info-row"><span>Clients:</span> <b id="clientCount">%CLIENTS%</b></div>
    <div class="info-row"><span>Uptime:</span> <b id="uptime">0</b> sec</div>
  </div>

  <div class="card">
    <h3 style="margin: 0 0 10px 0; color: #fff;"> Connected Devices</h3>
    <div id="clientsList">
      <div style="text-align:center; color:#888; padding: 10px;">Loading...</div>
    </div>
  </div>

  <div class="card">
    <button class="btn" onclick="fetch('/ledon')">💡 LED ON</button>
    <button class="btn" onclick="fetch('/ledoff')">🌑 LED OFF</button>
  </div>

  <script>
    function updateClients() {
      fetch('/api/clients')
        .then(res => res.json())
        .then(data => {
          document.getElementById('clientCount').textContent = data.count;
          document.getElementById('uptime').textContent = data.uptime;
          
          const clientsDiv = document.getElementById('clientsList');
          if (data.clients.length === 0) {
            clientsDiv.innerHTML = '<div style="text-align:center; color:#888; padding: 10px;"><i>No devices connected</i></div>';
          } else {
            let html = '';
            data.clients.forEach((client, idx) => {
              html += '<div class="client-row">';
              html += '<span class="client-num">#' + (idx + 1) + '</span>';
              html += '<div class="client-info">';
              html += '<div class="client-mac">' + client.mac + '</div>';
              html += '<div class="client-ip">IP: ' + client.ip + '</div>';
              html += '</div>';
              html += '</div>';
            });
            clientsDiv.innerHTML = html;
          }
        })
        .catch(err => console.error('Error:', err));
    }

    setInterval(updateClients, 3000);
    updateClients();
  </script>
</body>
</html>
)rawliteral";

// ===== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =====
String formatMAC(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

String getClientIP(int clientIndex) {
  return "192.168.4." + String(clientIndex + 2);
}

String getClientsJSON() {
  String json = "{\"count\":" + String(WiFi.softAPgetStationNum());
  json += ",\"uptime\":" + String(millis() / 1000);
  json += ",\"clients\":[";
  
  struct station_info* station = wifi_softap_get_station_info();
  int count = 0;
  
  while (station) {
    if (count > 0) json += ",";
    String mac = formatMAC(station->bssid);
    String ip = getClientIP(count);
    json += "{\"mac\":\"" + mac + "\",\"ip\":\"" + ip + "\"}";
    station = STAILQ_NEXT(station, next);
    count++;
  }
  wifi_softap_free_station_info();
  
  json += "]}";
  return json;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Запуск стабильной точки доступа ===");

  // Инициализация экрана
  Wire.begin(D2, D1);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("SSD1306 не найден!"));
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("  Initializing...");
  display.display();
  delay(1000);

  // Создаем точку доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  IP = WiFi.softAPIP();
  
  // ВАЖНО: отключаем сон Wi-Fi для стабильности
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  
  Serial.print("AP IP: ");
  Serial.println(IP);

  // DNS (Captive Portal)
  dnsServer.start(53, "*", IP);

  // Главная страница
  server.on("/", []() {
    String html = INDEX_HTML;
    html.replace("%SSID%", ap_ssid);
    html.replace("%IP%", IP.toString());
    html.replace("%CLIENTS%", String(WiFi.softAPgetStationNum()));
    server.send(200, "text/html", html);
  });

  // API клиентов
  server.on("/api/clients", []() {
    server.send(200, "application/json", getClientsJSON());
  });

  // Управление LED
  server.on("/ledon", []() {
    digitalWrite(LED_BUILTIN, LOW);
    server.send(200, "text/plain", "OK");
  });
  server.on("/ledoff", []() {
    digitalWrite(LED_BUILTIN, HIGH);
    server.send(200, "text/plain", "OK");
  });

  // Captive Portal для Android/iOS/Windows
  server.on("/generate_204", []() {
    server.sendHeader("Location", "http://" + IP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", []() {
    server.sendHeader("Location", "http://" + IP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/connecttest.txt", []() {
    server.sendHeader("Location", "http://" + IP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" + IP.toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  updateDisplay();
  Serial.println("Готово! Точка доступа стабильна.");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

// ===== LOOP =====
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  yield(); // ВАЖНО: отдаём управление системе

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    updateDisplay();
    lastUpdate = millis();
  }
}

// ===== ВЫВОД НА ЭКРАН =====
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("--- Wi-Fi AP ---");

  display.setCursor(0, 10);
  display.print("SSID: ");
  display.println(ap_ssid);

  display.setCursor(0, 20);
  display.print("Pass: ");
  display.println(ap_password);

  display.setCursor(0, 30);
  display.print("IP: ");
  display.println(IP.toString());

  display.setCursor(0, 40);
  int clients = WiFi.softAPgetStationNum();
  display.print("Clients: ");
  display.println(clients);

  if (clients > 0) {
    struct station_info* station = wifi_softap_get_station_info();
    int count = 0;
    while (station && count < 2) {
      display.setCursor(0, 50 + (count * 7));
      String mac = formatMAC(station->bssid);
      display.print(mac.substring(mac.length() - 8));
      station = STAILQ_NEXT(station, next);
      count++;
    }
    wifi_softap_free_station_info();
  }

  display.display();
}
