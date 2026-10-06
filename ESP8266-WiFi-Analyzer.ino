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

// ===== КЭШ СКАНИРОВАНИЯ =====
String cachedScanJSON = "[]";
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 2000;

// ===== HTML-СТРАНИЦА =====
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Wi-Fi Analyzer</title>
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 15px; text-align: center; }
    h1 { font-size: 20px; color: #fff; margin-bottom: 15px; }
    .card { background: #1e1e1e; padding: 15px; margin-bottom: 15px; border-radius: 10px; border: 1px solid #333; }
    .info-row { display: flex; justify-content: space-between; margin: 5px 0; font-size: 14px; }
    .info-row b { color: #0f0; }
    canvas { width: 100%; height: 200px; background: #000; border-radius: 8px; border: 1px solid #444; }
    .legend { display: flex; flex-wrap: wrap; justify-content: center; gap: 10px; margin-top: 10px; font-size: 12px; }
    .legend-item { display: flex; align-items: center; gap: 5px; }
    .color-box { width: 12px; height: 12px; border-radius: 3px; }
    .btn { padding: 10px 20px; margin: 5px; background: #0f0; color: #000; border: none; border-radius: 5px; font-weight: bold; cursor: pointer; }
    
    .client-row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #333; }
    .client-row:last-child { border-bottom: none; }
    .client-info { text-align: left; flex: 1; }
    .client-mac { color: #0f0; font-family: monospace; font-size: 13px; }
    .client-ip { color: #00ffff; font-family: monospace; font-size: 12px; margin-top: 3px; }
    .client-num { color: #888; font-size: 14px; margin-right: 10px; }

    .net-row { padding: 12px 0; border-bottom: 1px solid #333; }
    .net-row:last-child { border-bottom: none; }
    .net-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
    .net-ssid { color: #fff; font-weight: bold; font-size: 15px; flex: 1; text-align: left; }
    .net-distance { color: #00ffff; font-size: 13px; font-weight: bold; }
    .net-details { display: flex; justify-content: space-between; align-items: center; font-size: 11px; color: #888; margin-bottom: 6px; }
    .net-channel { background: #333; padding: 2px 6px; border-radius: 3px; color: #ff0; }
    .net-encryption { background: #333; padding: 2px 6px; border-radius: 3px; }
    .net-encryption.open { color: #f00; }
    .net-encryption.secure { color: #0f0; }
    .signal-bar-bg { width: 100%; height: 6px; background: #333; border-radius: 3px; overflow: hidden; }
    .signal-bar-fill { height: 100%; border-radius: 3px; transition: width 0.3s ease; }
    .disclaimer { font-size: 10px; color: #666; margin-top: 10px; font-style: italic; }
  </style>
</head>
<body>
  <h1>📡 Wi-Fi Analyzer</h1>
  
  <div class="card">
    <div class="info-row"><span>My AP:</span> <b>%SSID%</b></div>
    <div class="info-row"><span>IP:</span> <b>%IP%</b></div>
    <div class="info-row"><span>Clients:</span> <b id="clientCount">%CLIENTS%</b></div>
  </div>

  <div class="card">
    <h3 style="margin: 0 0 10px 0; color: #fff;"> Connected Devices</h3>
    <div id="clientsList">
      <div style="text-align:center; color:#888; padding: 10px;">Loading...</div>
    </div>
  </div>

  <div class="card">
    <h3 style="margin: 0 0 10px 0; color: #fff;">Live Signal Graph (RSSI)</h3>
    <canvas id="signalChart"></canvas>
    <div class="legend" id="legend"></div>
  </div>

  <div class="card">
    <h3 style="margin: 0 0 10px 0; color: #fff;">📶 Surrounding Networks</h3>
    <div id="networksList" style="text-align: left;">
      <div style="text-align:center; color:#888; padding: 10px;">Scanning...</div>
    </div>
    <div class="disclaimer">⚠️ Distance is approximate (±30-50%). Walls and obstacles affect accuracy.</div>
  </div>

  <div class="card">
    <button class="btn" onclick="fetch('/ledon')">💡 LED ON</button>
    <button class="btn" onclick="fetch('/ledoff')">🌑 LED OFF</button>
  </div>

  <script>
    const canvas = document.getElementById('signalChart');
    const ctx = canvas.getContext('2d');
    const legendDiv = document.getElementById('legend');
    
    function resizeCanvas() {
      canvas.width = canvas.offsetWidth;
      canvas.height = canvas.offsetHeight;
    }
    window.addEventListener('resize', resizeCanvas);
    resizeCanvas();

    let history = {};
    const MAX_POINTS = 60; 
    const colors = ['#00ff00', '#00ffff', '#ff00ff', '#ffff00', '#ff8800', '#ff0088', '#88ff00', '#0088ff'];

    function drawGraph() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = '#333';
      ctx.lineWidth = 1;
      
      ctx.fillStyle = '#666';
      ctx.font = '10px Arial';
      for (let rssi = -100; rssi <= -30; rssi += 10) {
        let y = canvas.height - ((rssi + 100) / 70) * canvas.height;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(canvas.width, y);
        ctx.stroke();
        ctx.fillText(rssi, 5, y - 2);
      }

      let colorIdx = 0;
      for (let ssid in history) {
        let data = history[ssid];
        if (data.length < 2) continue;
        
        ctx.strokeStyle = colors[colorIdx % colors.length];
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        for (let i = 0; i < data.length; i++) {
          let x = (i / (MAX_POINTS - 1)) * canvas.width;
          let y = canvas.height - ((data[i] + 100) / 70) * canvas.height;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
        colorIdx++;
      }
    }

    function updateLegend() {
      legendDiv.innerHTML = '';
      let colorIdx = 0;
      for (let ssid in history) {
        let div = document.createElement('div');
        div.className = 'legend-item';
        div.innerHTML = '<div class="color-box" style="background:' + colors[colorIdx % colors.length] + '"></div>' + ssid;
        legendDiv.appendChild(div);
        colorIdx++;
      }
    }

    function updateClients() {
      fetch('/api/clients')
        .then(res => res.json())
        .then(data => {
          document.getElementById('clientCount').textContent = data.count;
          
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
        .catch(err => console.error('Ошибка загрузки клиентов:', err));
    }

    // Расчёт приблизительного расстояния по RSSI
    function calculateDistance(rssi) {
      const txPower = -40; // dBm на 1 метре
      const n = 2.7; // коэффициент затухания в помещении
      let distance = Math.pow(10, (txPower - rssi) / (10 * n));
      return distance.toFixed(1);
    }

    function getEncryptionName(encType) {
      switch(encType) {
        case 0: return 'Open';
        case 1: return 'WEP';
        case 2: return 'WPA';
        case 4: return 'WPA2';
        case 8: return 'WPA/WPA2';
        default: return 'Unknown';
      }
    }

    function fetchData() {
      fetch('/api/scan')
        .then(res => res.json())
        .then(data => {
          let currentSSIDs = new Set();
          data.forEach(net => {
            currentSSIDs.add(net.ssid);
            if (!history[net.ssid]) history[net.ssid] = [];
            history[net.ssid].push(net.rssi);
            if (history[net.ssid].length > MAX_POINTS) {
              history[net.ssid].shift(); 
            }
          });
          for (let ssid in history) {
            if (!currentSSIDs.has(ssid)) delete history[ssid];
          }
          drawGraph();
          updateLegend();

          // Фильтруем дубликаты
          let uniqueNets = {};
          data.forEach(net => {
            if (net.ssid === "") return;
            if (!uniqueNets[net.ssid] || net.rssi > uniqueNets[net.ssid].rssi) {
              uniqueNets[net.ssid] = net;
            }
          });

          // Сортируем по убыванию сигнала
          let sortedNets = Object.values(uniqueNets).sort((a, b) => b.rssi - a.rssi);

          const netListDiv = document.getElementById('networksList');
          let html = '';
          sortedNets.forEach(net => {
            let percent = 2 * (net.rssi + 100);
            if (percent > 100) percent = 100;
            if (percent < 0) percent = 0;

            let barColor = '#f00';
            if (percent > 70) barColor = '#0f0';
            else if (percent > 40) barColor = '#ff0';

            let distance = calculateDistance(net.rssi);
            let encName = getEncryptionName(net.enc);
            let encClass = encName === 'Open' ? 'open' : 'secure';

            html += '<div class="net-row">';
            html += '<div class="net-header">';
            html += '<div class="net-ssid">' + net.ssid + '</div>';
            html += '<div class="net-distance">≈' + distance + 'm</div>';
            html += '</div>';
            html += '<div class="net-details">';
            html += '<span class="net-channel">CH ' + net.ch + '</span>';
            html += '<span class="net-encryption ' + encClass + '">' + encName + '</span>';
            html += '<span>' + net.rssi + ' dBm</span>';
            html += '</div>';
            html += '<div class="signal-bar-bg"><div class="signal-bar-fill" style="width:' + percent + '%; background:' + barColor + ';"></div></div>';
            html += '</div>';
          });
          
          if (sortedNets.length === 0) {
            html = '<div style="text-align:center; color:#888; padding: 10px;"><i>No networks found</i></div>';
          }
          netListDiv.innerHTML = html;
        })
        .catch(err => console.error('Ошибка загрузки:', err));
    }

    setInterval(fetchData, 2000);
    setInterval(updateClients, 2000);
    fetchData();
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
  String json = "{\"count\":" + String(WiFi.softAPgetStationNum()) + ",\"clients\":[";
  
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

void performScan() {
  int n = WiFi.scanNetworks();
  if (n == 0) {
    cachedScanJSON = "[]";
    return;
  }

  String json = "[";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "\\\""); 
    int rssi = WiFi.RSSI(i);
    int channel = WiFi.channel(i);
    int enc = WiFi.encryptionType(i);
    
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(rssi);
    json += ",\"ch\":" + String(channel);
    json += ",\"enc\":" + String(enc);
    json += "}";
    if (i < n - 1) json += ",";
  }
  json += "]";
  cachedScanJSON = json;
  WiFi.scanDelete();
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Запуск Wi-Fi Analyzer ===");

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("SSD1306 не найден!"));
      for (;;);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("  Initializing...");
  display.display();
  delay(1000);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);

  dnsServer.start(53, "*", IP);

  server.on("/", []() {
    String html = INDEX_HTML;
    html.replace("%SSID%", ap_ssid);
    html.replace("%IP%", IP.toString());
    html.replace("%CLIENTS%", String(WiFi.softAPgetStationNum()));
    server.send(200, "text/html", html);
  });

  server.on("/api/scan", []() {
    server.send(200, "application/json", cachedScanJSON);
  });

  server.on("/api/clients", []() {
    String clientsJSON = getClientsJSON();
    server.send(200, "application/json", clientsJSON);
  });

  server.on("/ledon", []() {
    digitalWrite(LED_BUILTIN, LOW);
    server.send(200, "text/plain", "OK");
  });
  server.on("/ledoff", []() {
    digitalWrite(LED_BUILTIN, HIGH);
    server.send(200, "text/plain", "OK");
  });

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

  performScan(); 
  updateDisplay();
  Serial.println("Готово! Откройте 192.168.4.1");
}

// ===== LOOP =====
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (millis() - lastScanTime > SCAN_INTERVAL) {
    performScan();
    lastScanTime = millis();
  }

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
  display.println("--- Wi-Fi Analyzer ---");

  display.setCursor(0, 10);
  display.print("AP: ");
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
