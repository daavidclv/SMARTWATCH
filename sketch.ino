#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LittleFS.h>
#include <MPU6050.h>
#include <Adafruit_MLX90614.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>

#include "hardware.h"

// =====================
// DEBUG
// =====================
#define DEBUG_MODE 1
#if DEBUG_MODE
  #define DPRINTLN(x) Serial.println(x)
  #define DPRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DPRINTLN(x)
  #define DPRINTF(...)
#endif

// =====================
// LOG
// =====================
#define LOG_FILE     "/log.csv"
#define LOG_MAX_SIZE (1024 * 50)

// =====================
// OBJETOS
// =====================
TwoWire I2C_Bus = TwoWire(0);
MPU6050 imu;
Adafruit_MLX90614 mlx;
TFT_eSPI display;
WebServer server(80); // Servidor web

// =====================
// VARIABLES
// =====================
volatile int pasos = 0;
float prevMagn = 0;
bool stepDetected = false;

float tempIR = 0;
float voltBat = 0;

unsigned long lastStepSample = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastActivityMillis = 0;
unsigned long joyCenterPressedTime = 0;

bool peripheralsOn = false;
bool linternaOn = false;

// =====================
// ISR MPU
// =====================
void IRAM_ATTR MPU_ISR() {
  lastActivityMillis = millis();
}

// =====================
// PWM
// =====================
void setupPWM() {
  ledcAttach(LCD_BL, PWM_FREQ_BL, 8);
  ledcWrite(LCD_BL, 0);

  ledcAttach(PIN_LINTERN_LED, PWM_FREQ_LED, 8);
  ledcWrite(PIN_LINTERN_LED, 0);

  ledcAttach(PIN_BUZZER, PWM_FREQ_BUZZ, 8);
  ledcWrite(PIN_BUZZER, 0);
}

void beep(int ms) {
  ledcWrite(PIN_BUZZER, 150);
  delay(ms);
  ledcWrite(PIN_BUZZER, 0);
}

// =====================
// BATERÍA
// =====================
float leerBateria() {
  return analogRead(PIN_BAT_ADC) / 4095.0f * 3.3f * ADC_FACTOR;
}

// =====================
// RAIL
// =====================
void activarRails(bool on) {
  digitalWrite(PIN_RAIL_CTRL, on ? LOW : HIGH);
}

// =====================
// PERIFÉRICOS
// =====================
void configureMPUMotionInterrupt() {
  imu.setSleepEnabled(false);
  imu.setMotionDetectionThreshold(2);
  imu.setMotionDetectionDuration(10);
  imu.setIntMotionEnabled(true);
}

void setupPeripherals() {
  imu.initialize();
  configureMPUMotionInterrupt();
  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), MPU_ISR, RISING);

  mlx.begin();

  display.init();
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);

  ledcWrite(LCD_BL, 200);  // brillo LCD encendido
  lastActivityMillis = millis();
}

void shutdownPeripherals() {
  ledcWrite(LCD_BL, 0);
  ledcWrite(PIN_LINTERN_LED, 0);
  ledcWrite(PIN_BUZZER, 0);
  detachInterrupt(digitalPinToInterrupt(PIN_MPU_INT));
}

// =====================
// DISPLAY
// =====================
void updateDisplay() {
  display.fillScreen(TFT_BLACK);
  display.setCursor(10, 10);
  display.println("Wearable IoT");

  display.setTextSize(3);
  display.setCursor(10, 50);
  display.println("Pasos:");
  display.println(pasos);

  display.setTextSize(2);
  display.setCursor(10, 120);
  display.printf("Temp: %.1f C\n", tempIR);
  display.printf("Bat:  %.2f V\n", voltBat);
}

// =====================
// LOG
// =====================
void rotateLogIfNeeded() {
  if (!LittleFS.exists(LOG_FILE)) return;
  File f = LittleFS.open(LOG_FILE, "r");
  if (f.size() <= LOG_MAX_SIZE) { f.close(); return; }
  f.close();
  LittleFS.remove(LOG_FILE);
}

void logSample() {
  rotateLogIfNeeded();
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) return;
  time_t now = time(nullptr);
  f.printf("%lu,%d,%.1f,%.2f\n", now, pasos, tempIR, voltBat);
  f.close();
}

// =====================
// DEEP SLEEP
// =====================
void enterDeepSleep() {
  shutdownPeripherals();
  activarRails(false);

  // Wakeup por botón o movimiento
  uint64_t mask = (1ULL << PIN_WAKE_BUTTON) | (1ULL << PIN_MPU_INT);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);

  DPRINTLN("Entrando en deep sleep...");
  esp_deep_sleep_start();
}

// =====================
// SERVIDOR WEB
// =====================
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Wearable ESP32</title>
  <style>
    body { font-family: sans-serif; text-align: center; background: #f6f6f6; }
    button { padding: 10px 20px; margin: 10px; border-radius: 10px; border: none; background: #0078D7; color: white; font-size: 16px; }
    #status { margin-top: 20px; }
  </style>
</head>
<body>
  <h1>Panel del Wearable</h1>
  <div id="status">
    <p><b>Batería:</b> <span id="battery">--%</span></p>
    <p><b>Pasos:</b> <span id="steps">--</span></p>
    <p><b>Temperatura:</b> <span id="temp">-- °C</span></p>
  </div>

  <button onclick="getTemp()">Medir temperatura</button>
  <button onclick="toggleLight()">Linterna ON/OFF</button>
  <button onclick="syncTime()">Sincronizar hora</button>

  <script>
    function getTemp() {
      fetch('/temp').then(r => r.json()).then(data => {
        document.getElementById('temp').innerText = data.temp + ' °C';
      });
    }
    function toggleLight() {
      fetch('/light/toggle').then(r => r.text()).then(data => {
        alert(data);
      });
    }
    function syncTime() {
      fetch('/sync');
      alert('Hora sincronizada (simulada)');
    }
    setInterval(() => {
      fetch('/status').then(r => r.json()).then(data => {
        document.getElementById('battery').innerText = data.battery + '%';
        document.getElementById('steps').innerText = data.steps;
      });
    }, 3000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", MAIN_page);
}

void handleStatus() {
  String json = "{\"battery\":" + String((int)(voltBat/3.7*100)) + ",\"steps\":" + String(pasos) + "}";
  server.send(200, "application/json", json);
}

void handleTemp() {
  String json = "{\"temp\":" + String(tempIR, 1) + "}";
  server.send(200, "application/json", json);
}

void handleLightToggle() {
  linternaOn = !linternaOn;
  ledcWrite(PIN_LINTERN_LED, linternaOn ? 255 : 0);
  String msg = linternaOn ? "Linterna encendida" : "Linterna apagada";
  server.send(200, "text/plain", msg);
}

void handleSync() {
  configTime(0, 0, "pool.ntp.org"); // sincronización real NTP
  server.send(200, "text/plain", "Hora sincronizada");
}

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RAIL_CTRL, OUTPUT);
  activarRails(false);

  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_MPU_INT, INPUT_PULLUP);

  setupPWM();
  I2C_Bus.begin(I2C_SDA, I2C_SCL);

  LittleFS.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi, IP: " + WiFi.localIP().toString());

  // Configurar rutas web
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/temp", handleTemp);
  server.on("/light/toggle", handleLightToggle);
  server.on("/sync", handleSync);
  server.begin();

  DPRINTLN("Servidor web iniciado.");
}

// =====================
// LOOP
// =====================
void loop() {
  server.handleClient();
  unsigned long now = millis();

  int btn = digitalRead(PIN_WAKE_BUTTON);

  if (btn == LOW && joyCenterPressedTime == 0)
    joyCenterPressedTime = now;

  if (btn == HIGH && joyCenterPressedTime != 0) {
    unsigned long t = now - joyCenterPressedTime;
    joyCenterPressedTime = 0;

    if (t >= 2000 && !peripheralsOn) {
      activarRails(true);
      peripheralsOn = true;
      setupPeripherals();
    } else if (t >= 5000 && peripheralsOn) {
      enterDeepSleep();
    }
  }

  if (peripheralsOn) {
    // Muestreo de pasos
    if (now - lastStepSample >= STEP_SAMPLE_MS) {
      int16_t ax, ay, az;
      imu.getAcceleration(&ax, &ay, &az);
      float m = sqrt(
        sq(ax / 16384.0f) +
        sq(ay / 16384.0f) +
        sq(az / 16384.0f)
      );

      if ((m - prevMagn) > STEP_THRESHOLD && !stepDetected) {
        pasos++;
        stepDetected = true;
        lastActivityMillis = now;
      }
      if (fabs(m - prevMagn) < NOISE_THRESHOLD) stepDetected = false;
      prevMagn = m;
      lastStepSample = now;
    }

    // Actualización de pantalla y log
    if (now - lastScreenUpdate >= 1000) {
      tempIR = mlx.readObjectTempC();
      voltBat = leerBateria();
      updateDisplay();
      logSample();
      lastScreenUpdate = now;
    }

    // Deep sleep si no hay actividad
    if ((now - lastActivityMillis) > INACTIVITY_MS) {
      enterDeepSleep();
    }
  } else {
    // Sleep ligero para ahorrar energía
    esp_light_sleep_start();
  }
}
