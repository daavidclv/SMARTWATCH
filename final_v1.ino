#include "hardware.h"
#include <Arduino.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// ===== RELOJ =====
#include <time.h>

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String("--:--:--");
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String("----/--/--");
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}


//Se cogen dos servidores por backup
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// Madrid (CET/CEST automático)
const char* TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// ===== WIFI + WEB =====
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "alvaro";
const char* WIFI_PASS = "12345678";

WebServer server(80);

// ======== SENSORES ========
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// ======== LCD ========
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// ======== VARIABLES GLOBALES ========
float currentTemperature = NAN;   // LCD sí, WEB no
volatile int stepCount = 0;

// ======== BUZZER ========
bool alarmEnabled = false;

// control de pitidos (no bloqueante)
unsigned long lastBeep = 0;
bool buzzerState = false;
#define BEEP_INTERVAL_MS 500   // 500 ms ON / OFF


// ======== PODÓMETRO ========
unsigned long lastStepSample = 0;
unsigned long lastStepTime = 0;
bool stepPossible = false;
#define STEP_SAMPLE_MS 50
#define STEP_DEBOUNCE_MS 300

// ======== TEMPERATURA ========
unsigned long lastTempRead = 0;
#define TEMP_UPDATE_MS 1000

// ======== PANTALLA ========
unsigned long lastScreenUpdate = 0;
#define SCREEN_UPDATE_MS 1000

// =========================================================

void initPeripheralRail() {
  pinMode(PIN_CONT_PERIF, OUTPUT);
  pinMode(PIN_CONT_PERIF_LOW, OUTPUT);
  digitalWrite(PIN_CONT_PERIF, HIGH);
  digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
}

// ================= BUZZER =================
void updateAlarm() {
  if (!alarmEnabled) {
    noTone(PIN_BUZZER);
    buzzerState = false;
    return;
  }

  if (millis() - lastBeep >= BEEP_INTERVAL_MS) {
    lastBeep = millis();
    buzzerState = !buzzerState;

    if (buzzerState) {
      tone(PIN_BUZZER, 2000);  // 2 kHz
    } else {
      noTone(PIN_BUZZER);
    }
  }
}

// ================= RELOJ =================
bool syncTimeWithNTP(uint32_t timeoutMs = 10000) {
  // Esta función ya aplica la zona horaria y el DST correctamente
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeinfo;
  unsigned long t0 = millis();
  while (!getLocalTime(&timeinfo) && (millis() - t0 < timeoutMs)) {
    delay(200);
  }

  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  return true;
}


// ================= WIFI =================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Conectando a WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK! IP: ");
    Serial.println(WiFi.localIP());
  }
}

// ================= WEB =================

String makeHTMLPage() {
  String html;
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pulsera</title>";
  html += "<style>";
  html += "body{font-family:system-ui;margin:20px}";
  html += ".card{padding:16px;border:1px solid #ddd;border-radius:12px;max-width:420px}";
  html += ".big{font-size:1.3rem}";
  html += "</style></head><body>";

  html += "<h2>Pulsera - Medidas</h2>";
  html += "<div class='card'>";
  html += "<p class='big'><b>Pasos:</b> <span id='steps'>--</span></p>";
  html += "<p class='big'><b>Hora:</b> <span id='time'>--:--:--</span></p>";
  html += "<p><small>Fecha: <span id='date'>----/--/--</span></small></p>";
  html += "<p class='big'><b>Alarma:</b> <span id='alarm'>--</span></p>";
  html += "<button onclick=\"setAlarm(1)\">Activar alarma</button> ";
  html += "<button onclick=\"setAlarm(0)\">Desactivar alarma</button>";

  html += "<p><small>Uptime: <span id='uptime'>--</span> ms</small></p>";
  html += "<p><small>Estado: <span id='status'>Conectando...</span></small></p>";
  html += "</div>";

  html += "<script>";
  html += "async function refresh(){";
  html += " try{";
  html += "  const r = await fetch(window.location.origin + '/api');";
  html += "  const j = await r.json();";
  html += "  document.getElementById('steps').textContent = j.steps;";
  html += "document.getElementById('time').textContent = j.time;";
  html += "document.getElementById('date').textContent = j.date;";
  html += "document.getElementById('alarm').textContent = j.alarm ? 'ON' : 'OFF';";
  html += "  document.getElementById('uptime').textContent = j.uptime_ms;";
  html += "  document.getElementById('status').textContent = 'OK';";
  html += " }catch(e){";
  html += "  document.getElementById('status').textContent = 'Error';";
  html += " }}";
  html += "async function setAlarm(state){";
  html += "  await fetch(window.location.origin + '/alarm?state=' + state, {method:'POST'});";
  html += "}";
  html += "refresh(); setInterval(refresh, 1000);";
  html += "</script></body></html>";

  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", makeHTMLPage());
}

void handleAlarm() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "Missing state");
    return;
  }

  int s = server.arg("state").toInt();
  alarmEnabled = (s != 0);

  if (!alarmEnabled) {
    noTone(PIN_BUZZER);
  }

  server.send(200, "text/plain", alarmEnabled ? "ALARM ON" : "ALARM OFF");
}


void handleApi() {
  String json = "{";
  json += "\"steps\":" + String(stepCount) + ",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"time\":\"" + getTimeString() + "\",";
  json += "\"date\":\"" + getDateString() + "\"";
  json += ",\"alarm\":" + String(alarmEnabled ? "true" : "false");
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}


void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/alarm", HTTP_POST, handleAlarm);
  server.on("/api", HTTP_GET, handleApi);
  server.begin();
}

// ================= SENSORES =================

void updateTemperature() {
  if (millis() - lastTempRead < TEMP_UPDATE_MS) return;
  lastTempRead = millis();
  currentTemperature = mlx.readObjectTempC();   // SOLO LCD
}

void updateStepCounter() {
  if (millis() - lastStepSample < STEP_SAMPLE_MS) return;
  lastStepSample = millis();

  int16_t ax, ay, az;
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR_MPU6050, 6, true);
  if (Wire.available() < 6) return;

  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();

  float A = sqrt(pow(ax / 16384.0, 2) +
                 pow(ay / 16384.0, 2) +
                 pow(az / 16384.0, 2));

  if (A < 0.9) stepPossible = true;
  if (A > 1.2 && stepPossible && millis() - lastStepTime > STEP_DEBOUNCE_MS) {
    stepCount++;
    lastStepTime = millis();
    stepPossible = false;
  }
}

// ================= LCD =================

void drawScreen() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextWrap(false);

  tft.setTextColor(GC9A01A_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 30);
  tft.print("Monitor");

  tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(20, 90);
  tft.print("Temp: ");
  tft.setTextColor(GC9A01A_YELLOW);
  tft.print(isnan(currentTemperature) ? 0.0 : currentTemperature, 1);
  tft.print(" C");

  tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(20, 140);
  tft.print("Pasos: ");
  tft.setTextColor(GC9A01A_GREEN);
  tft.print(stepCount);

  struct tm timeinfo;
  tft.setTextColor(GC9A01A_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 190);
  
  if (getLocalTime(&timeinfo)) {
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    tft.print("Hora: ");
    tft.print(buf);
  } else {
    tft.print("Hora: --:--:--");
  }

}

void updateScreen() {
  if (millis() - lastScreenUpdate < SCREEN_UPDATE_MS) return;
  lastScreenUpdate = millis();
  drawScreen();
}

// ================= SETUP / LOOP =================

void setup() {
  Serial.begin(115200);
  initPeripheralRail();

  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);   // aseguramos apagado


  Wire.begin(I2C_SDA, I2C_SCL);

  // MPU6050
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission();

  mlx.begin(ADDR_MLX90614, &Wire);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  drawScreen();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncTimeWithNTP();   // 1) coge hora real de Internet
    setupWebServer();    // si quieres que el servidor web siga funcionando, NO apagues WiFi
  }
}

void loop() {
  updateTemperature();   // LCD
  updateStepCounter();
  updateScreen();
  server.handleClient();
  updateAlarm();

}
