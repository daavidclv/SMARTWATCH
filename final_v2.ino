#include "hardware.h"
#include <Arduino.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <time.h>
#include <WiFi.h>
#include <WebServer.h>

// ===================== WIFI CREDENCIALES =====================
const char* WIFI_SSID = "alvaro";
const char* WIFI_PASS = "12345678";

WebServer server(80);

// ===================== NTP / ZONA HORARIA =====================
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// ===================== JOYSTICK (ADC como botón) =====================
#define PIN_JOYSTICK_ADC 32
#define BTN_PRESSED_TH   3800     // pulsado si ADC < 3100
#define BTN_DEBOUNCE_MS  60
#define BTN_LONG_MS      5000

// ===================== BUZZER =====================
bool alarmEnabled = false;
unsigned long lastBeep = 0;
bool buzzerState = false;
#define BEEP_INTERVAL_MS 500

// ===================== SENSORES =====================
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
float currentTemperature = NAN;

volatile int stepCount = 0;

// Podómetro MPU6050
unsigned long lastStepSample = 0;
unsigned long lastStepTime = 0;
bool stepPossible = false;
#define STEP_SAMPLE_MS 50
#define STEP_DEBOUNCE_MS 300

// Temperatura MLX (solo cuando toca)
unsigned long lastTempRead = 0;
#define TEMP_UPDATE_MS 1000

// ===================== LCD =====================
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
unsigned long lastScreenUpdate = 0;
#define SCREEN_UPDATE_MS 300   // un poco más fluido para cambiar pantallas

// ===================== ESTADO WIFI =====================
bool wifiOn = false;  // se ajusta tras connectWiFi()

// ===================== PANTALLAS =====================
enum ScreenMode {
  SCREEN_MLX = 0,
  SCREEN_MPU = 1,
  SCREEN_TIME = 2
};
const int SCREEN_COUNT = 3;
int currentScreen = SCREEN_MPU;

// ===================== BOTÓN (estado interno) =====================
bool btnStable = false;
bool btnLastStable = false;
unsigned long btnLastChange = 0;
unsigned long btnPressStart = 0;
bool longFired = false;

// =========================================================
//                      UTIL RELOJ
// =========================================================
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

bool syncTimeWithNTP(uint32_t timeoutMs = 10000) {
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeinfo;
  unsigned long t0 = millis();
  while (!getLocalTime(&timeinfo) && (millis() - t0 < timeoutMs)) {
    delay(200);
  }
  return getLocalTime(&timeinfo);
}

// =========================================================
//                      WIFI (TU FUNCIÓN)
// =========================================================
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

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi OFF");
}

// =========================================================
//                      BUZZER
// =========================================================
void updateAlarm() {
  if (!alarmEnabled) {
    noTone(PIN_BUZZER);
    buzzerState = false;
    return;
  }

  if (millis() - lastBeep >= BEEP_INTERVAL_MS) {
    lastBeep = millis();
    buzzerState = !buzzerState;

    if (buzzerState) tone(PIN_BUZZER, 2000);
    else noTone(PIN_BUZZER);
  }
}

// =========================================================
//                     JOYSTICK BOTÓN
// =========================================================
bool readButtonRaw() {
  int v = analogRead(PIN_JOYSTICK_ADC);
  return (v < BTN_PRESSED_TH);
}

void nextScreen() {
  currentScreen = (currentScreen + 1) % SCREEN_COUNT;
  // refresco inmediato al cambiar
  lastScreenUpdate = 0;
}

// maneja pulsación corta/larga
void handleButton() {
  bool raw = readButtonRaw();
  unsigned long now = millis();

  // debounce
  if (raw != btnStable && (now - btnLastChange) > BTN_DEBOUNCE_MS) {
    btnLastChange = now;
    btnStable = raw;
  }

  // flancos
  if (btnStable != btnLastStable) {
    btnLastStable = btnStable;

    if (btnStable) {
      // empezó a pulsar
      btnPressStart = now;
      longFired = false;
    } else {
      // soltó
      if (!longFired) {
        // pulsación corta -> siguiente pantalla
        nextScreen();
      }
    }
  }

  // pulsación larga
  if (btnStable && !longFired && (now - btnPressStart >= BTN_LONG_MS)) {
    longFired = true;

    // toggle WiFi usando tu connectWiFi()
    if (wifiOn) {
      disconnectWiFi();
      wifiOn = false;
    } else {
      connectWiFi();
      wifiOn = (WiFi.status() == WL_CONNECTED);
      if (wifiOn) {
        syncTimeWithNTP();
      }
    }
  }
}

// =========================================================
//                     WEB
// =========================================================
String makeHTMLPage() {
  String html;
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pulsera</title>";
  html += "<style>";
  html += "body{font-family:system-ui;margin:20px}";
  html += ".card{padding:16px;border:1px solid #ddd;border-radius:12px;max-width:420px}";
  html += ".big{font-size:1.3rem}";
  html += "button{padding:10px 14px;border-radius:10px;border:1px solid #ccc;background:#f5f5f5;margin-right:8px}";
  html += "</style></head><body>";

  html += "<h2>Pulsera - Medidas</h2>";
  html += "<div class='card'>";
  html += "<p class='big'><b>Pasos:</b> <span id='steps'>--</span></p>";
  html += "<p class='big'><b>Hora:</b> <span id='time'>--:--:--</span></p>";
  html += "<p><small>Fecha: <span id='date'>----/--/--</span></small></p>";
  html += "<p class='big'><b>Alarma:</b> <span id='alarm'>--</span></p>";
  html += "<button onclick=\"setAlarm(1)\">Activar alarma</button>";
  html += "<button onclick=\"setAlarm(0)\">Desactivar alarma</button>";
  html += "<p><small>Uptime: <span id='uptime'>--</span> ms</small></p>";
  html += "<p><small>Estado: <span id='status'>Conectando...</span></small></p>";
  html += "</div>";

  html += "<script>";
  html += "async function refresh(){";
  html += " try{";
  html += "  const r = await fetch(window.location.origin + '/api', {cache:'no-store'});";
  html += "  const j = await r.json();";
  html += "  document.getElementById('steps').textContent = j.steps;";
  html += "  document.getElementById('time').textContent = j.time;";
  html += "  document.getElementById('date').textContent = j.date;";
  html += "  document.getElementById('alarm').textContent = j.alarm ? 'ON' : 'OFF';";
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
  if (!alarmEnabled) noTone(PIN_BUZZER);
  server.send(200, "text/plain", alarmEnabled ? "ALARM ON" : "ALARM OFF");
}

void handleApi() {
  String json = "{";
  json += "\"steps\":" + String(stepCount) + ",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"time\":\"" + getTimeString() + "\",";
  json += "\"date\":\"" + getDateString() + "\",";
  json += "\"alarm\":" + String(alarmEnabled ? "true" : "false");
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

// =========================================================
//                     SENSORES
// =========================================================
void updateTemperatureIfNeeded() {
  // SOLO leer MLX si estás en su pantalla (ahorro + evitas NaNs en background)
  if (currentScreen != SCREEN_MLX) return;

  if (millis() - lastTempRead < TEMP_UPDATE_MS) return;
  lastTempRead = millis();
  currentTemperature = mlx.readObjectTempC();
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

  if (A > 1.2 && stepPossible && (millis() - lastStepTime > STEP_DEBOUNCE_MS)) {
    stepCount++;
    lastStepTime = millis();
    stepPossible = false;
  }
}

// =========================================================
//                     LCD (PANTALLAS)
// =========================================================
void drawScreen() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextWrap(false);

  // cabecera
  tft.setTextColor(GC9A01A_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 25);
  tft.print("Pulsera");

  // indicador WiFi
  tft.setTextSize(1);
  tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(20, 55);
  tft.print("WiFi: ");
  tft.print(wifiOn ? "ON" : "OFF");

  // Pantalla actual
  tft.setTextSize(2);
  tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(20, 90);

  if (currentScreen == SCREEN_MLX) {
    tft.print("MLX Temp:");
    tft.setCursor(20, 130);
    tft.setTextColor(GC9A01A_YELLOW);
    tft.print(isnan(currentTemperature) ? 0.0 : currentTemperature, 1);
    tft.print(" C");
  }
  else if (currentScreen == SCREEN_MPU) {
    tft.print("Pasos:");
    tft.setCursor(20, 130);
    tft.setTextColor(GC9A01A_GREEN);
    tft.print(stepCount);
  }
  else if (currentScreen == SCREEN_TIME) {
    tft.print("Hora:");
    tft.setCursor(20, 130);
    tft.setTextColor(GC9A01A_WHITE);
    tft.print(getTimeString());
    tft.setCursor(20, 170);
    tft.setTextSize(1);
    tft.print(getDateString());
  }

  // hint abajo
  tft.setTextSize(1);
  tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(20, 210);
  tft.print("Click: siguiente | Mantener 5s: WiFi");
}

void updateScreen() {
  if (millis() - lastScreenUpdate < SCREEN_UPDATE_MS) return;
  lastScreenUpdate = millis();
  drawScreen();
}

// =========================================================
//                     SETUP / LOOP
// =========================================================
void setup() {
  Serial.begin(115200);

  // Buzzer
  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);

  // Joystick ADC
  pinMode(PIN_JOYSTICK_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_JOYSTICK_ADC, ADC_11db);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // MPU6050 init (wake)
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission();

  // MLX init (0x5A)
  mlx.begin(0x5A, &Wire);

  // LCD
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);

  // WiFi + NTP + Web
  connectWiFi();
  wifiOn = (WiFi.status() == WL_CONNECTED);
  if (wifiOn) {
    syncTimeWithNTP();
    setupWebServer();
  }

  drawScreen();
}

void loop() {
  handleButton();

  // Sensores
  updateTemperatureIfNeeded();  // SOLO en pantalla MLX
  updateStepCounter();          // siempre (sin sleep)

  // LCD
  updateScreen();

  // Web + alarma
  server.handleClient();
  updateAlarm();
}
