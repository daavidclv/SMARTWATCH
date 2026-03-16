/************************************************************
 *  WEARABLE ESP32 COMPLETO
 *  LCD + JOYSTICK + SENSORES + WEB + SPIFFS
 ************************************************************/

#include <Arduino.h>
#include "hardware.h"

/* ======== LIBRERÍAS ======== */
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_MLX90614.h>

/* ======== WIFI ======== */
#define WIFI_SSID "alvaro"
#define WIFI_PASS "12345678"

/* ======== WEB ======== */
WebServer server(80);


/* ======== SENSORES ======== */
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

/* ======== VARIABLES GLOBALES ======== */
float currentTemperature = 0.0;
volatile int stepCount = 0;
bool linternaOn = false;

/* ======== SPIFFS ======== */
unsigned long ultimoGuardado = 0;
#define PERIODO_GUARDADO 60000UL


/* ======== PODÓMETRO ======== */
unsigned long lastStepSample = 0;
unsigned long lastStepTime = 0;
bool stepPossible = false;
#define STEP_SAMPLE_MS 50
#define STEP_DEBOUNCE_MS 300

/* ======== TEMPERATURA ======== */
unsigned long lastTempRead = 0;
#define TEMP_UPDATE_MS 1000

/* =========================================================
 *                       FUNCIONES
 * ========================================================= */

void initPeripheralRail() {
  pinMode(PIN_CONT_PERIF, OUTPUT);
  pinMode(PIN_CONT_PERIF_LOW, OUTPUT);
  digitalWrite(PIN_CONT_PERIF, HIGH);
  digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
}

/* ---------- BATERÍA ---------- */
float readBattery() {
  int adc = analogRead(PIN_BAT_ADC);
  return adc * BAT_ADC_FACTOR * (3.3 / 4095.0);
}

/* ---------- HORA ---------- */
String getTimestamp() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  if (!localtime_r(&now, &timeinfo)) return "1970-01-01 00:00:00";
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  return String(buf);
}

/* ---------- FLASH ---------- */
void guardarDatos() {
  int batPct = (int)(readBattery() / 4.2 * 100);
  String linea = getTimestamp() + "," +
                 String(stepCount) + "," +
                 String(currentTemperature,1) + "," +
                 String(batPct) + "\n";
  File f = SPIFFS.open("/datos.txt", FILE_APPEND);
  if (f) { f.print(linea); f.close(); }
}

// =====================
// ACTUADORES: LINTERNA Y ALARMA
// =====================

// Función para encender o apagar la linterna (LEDs discretos)
// Parámetro 'on': true -> encender linterna, false -> apagar
void setFlashlight(bool on) {
    flashlightOn = on;                         // Guardar estado actual
    digitalWrite(PIN_LEDS, on ? HIGH : LOW);  // Escribir en el pin correspondiente
    // HIGH = linterna encendida según tu esquema
    // LOW  = linterna apagada
}

// Inicialización del buzzer
void initBuzzer() {
    pinMode(PIN_BUZZER, OUTPUT);             // Configurar pin como salida
    ledcSetup(0, PWM_FREQ_BUZZ, 8);          // Configurar canal PWM 0, frecuencia definida, resolución 8 bits
    ledcAttachPin(PIN_BUZZER, 0);            // Asociar canal PWM al pin del buzzer
}

// Función que revisa la temperatura actual y enciende/apaga la linterna
// Se ejecuta periódicamente después de leer la temperatura
void checkTemperatureActuator() {
    if (currentTemperature > 30.0) {         // Si temperatura > 30°C
        setFlashlight(true);                 // Encender linterna
    } else {
        setFlashlight(false);                // Apagar linterna si temperatura <= 30°C
    }
}

// Función que revisa el estado de la batería y activa la alarma si baja del 50%
// La alarma se mantiene activa durante 5 segundos
void checkBatteryAlert() {
    float batteryPercent = readBattery() / 4.2 * 100; // Convertir voltaje a porcentaje

    // Si batería < 50% y alerta no estaba activa, iniciar temporizador
    if (batteryPercent < 50 && !batteryAlertActive) {
        batteryAlertStart = millis();       // Guardar tiempo de inicio
        batteryAlertActive = true;          // Marcar alerta como activa
    }

    // Si la alerta está activa, controlar duración del buzzer
    if (batteryAlertActive) {
        if (millis() - batteryAlertStart <= 5000) {  // Durante 5 segundos
            ledcWriteTone(0, PWM_FREQ_BUZZ);         // Activar buzzer
        } else {                                     // Después de 5 segundos
            ledcWriteTone(0, 0);                     // Apagar buzzer
            batteryAlertActive = false;              // Resetear bandera de alerta
        }
    }
}

/* =========================================================
 *                     WEB SERVER
 * ========================================================= */

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<title>Wearable ESP32</title></head><body>
<h2>Wearable</h2>
<p>Hora: <span id='t'></span></p>
<p>Batería: <span id='b'></span>%</p>
<p>Pasos: <span id='s'></span></p>
<p>Temp: <span id='tp'></span> °C</p>
<button onclick="fetch('/light/on')">Linterna ON</button>
<button onclick="fetch('/light/off')">Linterna OFF</button>
<a href='/datos'>Ver histórico</a>
<script>
setInterval(()=>{
fetch('/status').then(r=>r.json()).then(d=>{
t.innerText=d.time; b.innerText=d.battery;
s.innerText=d.steps; tp.innerText=d.temp;
});
},2000);
</script></body></html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", MAIN_page);
}

void handleStatus() {
  int batPct = (int)(readBattery() / 4.2 * 100);
  String json = "{";
  json += "\"time\":\"" + getCurrentTime() + "\",";
  json += "\"battery\":" + String(batPct) + ",";
  json += "\"steps\":" + String(stepCount) + ",";
  json += "\"temp\":" + String(currentTemperature,1) + "}";
  server.send(200, "application/json", json);
}

void handleLightOn(){ setFlashlight(true); server.send(200); }
void handleLightOff(){ setFlashlight(false); server.send(200); }

void handleDatos() {
  String h="<pre>";
  File f = SPIFFS.open("/datos.txt", FILE_READ);
  while(f && f.available()) h+=(char)f.read();
  if(f)f.close();
  h+="</pre><a href='/'>Volver</a>";
  server.send(200,"text/html",h);
}

/* =========================================================
 *                     SENSORES
 * ========================================================= */

void updateTemperature() {
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

  ax = Wire.read()<<8 | Wire.read();
  ay = Wire.read()<<8 | Wire.read();
  az = Wire.read()<<8 | Wire.read();

  float A = sqrt(pow(ax/16384.0,2)+pow(ay/16384.0,2)+pow(az/16384.0,2));

  if (A < 0.9) stepPossible = true;
  if (A > 1.2 && stepPossible && millis()-lastStepTime>STEP_DEBOUNCE_MS) {
    stepCount++;
    lastStepTime = millis();
    stepPossible = false;
  }
}


/* =========================================================
 *                     SETUP
 * ========================================================= */

void setup() {
  Serial.begin(115200);
  initPeripheralRail();

  pinMode(PIN_LEDS, OUTPUT);
  analogReadResolution(12);

  SPIFFS.begin(true);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x6B); Wire.write(0);
  Wire.endTransmission();

  mlx.begin(ADDR_MLX90614, &Wire);


  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED) delay(200);

  configTime(0,0,"pool.ntp.org");
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();

  server.on("/",handleRoot);
  server.on("/status",handleStatus);
  server.on("/light/on",handleLightOn);
  server.on("/light/off",handleLightOff);
  server.on("/datos",handleDatos);
  server.begin();

}

/* =========================================================
 *                     LOOP
 * ========================================================= */

void loop() {
  server.handleClient();
  updateTemperature();
  updateStepCounter();

  // Linterna y alarma
  checkTemperatureActuator();
  checkBatteryAlert();

  if (millis()-ultimoGuardado > PERIODO_GUARDADO) {
    guardarDatos();
    ultimoGuardado = millis();
  }
}
