/************************************************************
 *  main.ino
 *  
 *  Sistema de pantallas con LCD SPI controladas
 *  mediante joystick analógico (ADC).
 *  
 *  - Pantalla HOME: información general
 *  - Pantalla TEMP: temperatura IR (MLX90614)
 *  - Pantalla STEPS: contador de pasos (MPU6050)
 ************************************************************/

#include <Arduino.h>
#include "hardware.h"

// =====================
// LIBRERÍAS
// =====================
#include <WiFi.h>                 // Conexión WiFi
#include "time.h"                 // NTP para hora
#include <Wire.h>                 // Comunicación I2C (sensores)
#include <SPI.h>                  // Comunicación SPI (LCD)
#include <Adafruit_GFX.h>         // Primitivas gráficas
#include <Adafruit_GC9A01A.h>     // Driver LCD GC9A01 (240x240)
#include <Adafruit_MLX90614.h>    // Sensor de temperatura IR

// =====================
// Inicialización del rail de periféricos
// =====================
void initPeripheralRail() {
    pinMode(PIN_CONT_PERIF, OUTPUT);
    pinMode(PIN_CONT_PERIF_LOW, OUTPUT);

    // LOW = ON → habilita alimentación a periféricos
    digitalWrite(PIN_CONT_PERIF, HIGH);
    digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
}


// =====================
// LCD
// =====================
// Objeto principal de la pantalla
Adafruit_GC9A01A lcd(LCD_CS, LCD_DC, LCD_RST);

// =====================
// GESTIÓN DE PANTALLAS
// =====================
// Enumeración de pantallas disponibles
enum Screen {
  SCREEN_HOME,
  SCREEN_TEMP,
  SCREEN_STEPS
};

// Pantalla actualmente activa
Screen currentScreen = SCREEN_HOME;

// =====================
// SENSOR DE TEMPERATURA IR (MLX90614)
// =====================
// Objeto del sensor
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// Última temperatura leída
float currentTemperature = 0.0f;

// Control temporal de muestreo
unsigned long lastTempRead = 0;

// Periodo de actualización (ms)
#define TEMP_UPDATE_MS 1000

// =====================
// PODÓMETRO (MPU6050)
// =====================
// Control de muestreo del acelerómetro
unsigned long lastStepSample = 0;

// Tiempo del último paso detectado (antirebote)
unsigned long lastStepTime = 0;

// Frecuencia de muestreo del acelerómetro
#define STEP_SAMPLE_MS 50    // 20 Hz

// Tiempo mínimo entre pasos válidos
#define STEP_DEBOUNCE_MS 300

// Contador total de pasos
int stepCount = 0;

// Flag intermedio para detección de flanco
bool stepPossible = false;

// =====================
// ESTADOS DE CONSUMO
// =====================
enum PowerState {
  PWR_DEEP_SLEEP,
  PWR_3V3,
  PWR_PERIF_LOW,
  PWR_PERIF
};

PowerState powerState = PWR_3V3;

unsigned long powerLastActivity = 0;

#define IN_SLEEP_TIMEOUT MINUTES(2) // 2 minutos
#define OUT_SLEEP_TIMEOUT MINUTES(5) // 5 minutos

bool joystickActivity = false;
bool needHighPerif = false;
bool upPressed = false;
bool downPressed = false;

// =====================
// JOYSTICK (ADC)
// =====================
// Direcciones detectadas por el joystick
enum JoyDir {
  JOY_NONE,
  JOY_UP,
  JOY_DOWN,
  JOY_LEFT,
  JOY_RIGHT,
  JOY_CENTER
};

// =====================
// UMBRALES ADC DEL JOYSTICK
// =====================
#define ADC_CENTER_MIN   1800
#define ADC_CENTER_MAX   2300

#define ADC_UP_MIN       3500
#define ADC_UP_MAX       4095

#define ADC_DOWN_MIN     0
#define ADC_DOWN_MAX     700

#define ADC_LEFT_MIN     800
#define ADC_LEFT_MAX     1400

#define ADC_RIGHT_MIN    2600
#define ADC_RIGHT_MAX    3300

// =====================
// ANTIREBOTE DEL JOYSTICK
// =====================
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250;

// =====================
// PANTALLA HOME: actualización periódica
// =====================
unsigned long lastHomeUpdate = 0;

// =====================
// ACTUADORES: LINTERNA Y ALARMA
// =====================
bool flashlightOn = false;

// Buzzer batería baja
unsigned long batteryAlertStart = 0;
bool batteryAlertActive = false;

// =====================
// PROTOTIPOS
// =====================
JoyDir readJoystickADC();
void checkJoystick();
void updateScreen();
void drawHomeScreen();
void drawTempScreen();
void drawTempValue();
void drawStepsScreen();
void drawStepsValue();
void updateTemperature();
void updateStepCounter();
float readBattery();
String getCurrentTime();
void setFlashlight(bool on);
void initBuzzer();
void checkTemperatureActuator();
void checkBatteryAlert();
void setPowerState();
void enterLightSleep();

// =====================
// SETUP
// =====================
void setup() {

  // Inicialización del puerto serie (debug)
  Serial.begin(115200);
  delay(500);

  // Encender rail de periféricos
  initPeripheralRail();

  // Configuración del ADC del ESP32
  pinMode(PIN_JOYSTICK_ADC, INPUT);
  analogReadResolution(12); // 0–4095

  // Inicialización del bus I2C compartido
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- Inicialización MPU6050 ---
  // El MPU6050 arranca en modo SLEEP y debe despertarse
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x6B);       // Registro Power Management
  Wire.write(0x00);      // Wake up
  Wire.endTransmission(true);

  // --- Inicialización MLX90614 ---
  // El sensor IR arranca activo por defecto
  mlx.begin(ADDR_MLX90614, &Wire);

  // Inicialización del bus SPI y LCD
  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  lcd.begin();
  lcd.setRotation(2);
  lcd.fillScreen(ST77XX_BLACK);

  // --------------------------
  // Conexión WiFi
  // --------------------------
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd.setCursor(10, 120);
  lcd.setTextSize(1);
  lcd.setTextColor(ST77XX_WHITE);
  lcd.println("Conectando a WiFi...");

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
      delay(100);
      if (millis() - wifiStart > 10000) { // Timeout 10s
          lcd.setCursor(10, 140);
          lcd.println("WiFi FALLA");
          break;
      }
  }
  if (WiFi.status() == WL_CONNECTED) {
      lcd.setCursor(10, 140);
      lcd.println("WiFi OK");
  }

  // Configurar NTP para obtener hora
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 0; // Ajustar según zona horaria
  const int daylightOffset_sec = 3600;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  // Dibujar pantalla inicial
  drawHomeScreen();
}

// =====================
// LOOP PRINCIPAL
// =====================
void loop() {

  // Actualizar pines de consumo
  updatePowerState();

  // Gestión de navegación por joystick
  checkJoystick();

  // Actualizar pantalla HOME cada segundo
  if (currentScreen == SCREEN_HOME && millis() - lastHomeUpdate > 1000) {
      drawHomeScreen();
      lastHomeUpdate = millis();
  }

  // Actualización de temperatura (solo en pantalla TEMP)
  updateTemperature();

  // Actualización del contador de pasos (siempre activa)
  updateStepCounter();

  // Linterna y alarma
  checkTemperatureActuator();
  checkBatteryAlert();
}
// =====================
// MODOS DE CONSUMO
// =====================
void setPowerState(PowerState newState){

  if (newState == powerState) return;//Para q no repita la gestion de pines

  powerState = newState;

  switch (powerState)  {
    case PWR_DEEP_SLEEP:
      digitalWrite(PIN_CONT_PERIF, HIGH);
      digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
      // preparar deep sleep
      break;

    case PWR_3V3:
      digitalWrite(PIN_CONT_PERIF, HIGH);
      digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
      // solo el joystick
      break;

    case PWR_PERIF_LOW:
      digitalWrite(PIN_CONT_PERIF, LOW);
      digitalWrite(PIN_CONT_PERIF_LOW, HIGH);
      // LCD y buzzer
      break;

    case PWR_PERIF:
      digitalWrite(PIN_CONT_PERIF, LOW);
      digitalWrite(PIN_CONT_PERIF_LOW, LOW);
      // WiFi ON, IR, acelerometro, linterna
      break;
  }
}
void enterLightSleep() {
  setPowerState(PWR_DEEP_SLEEP);

  lcd.fillScreen(ST77XX_BLACK);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup(OUT_SLEEP_TIMEOUT); 

  esp_light_sleep_start();

  powerLastActivity = millis();
  powerState = PWR_3V3;
  setPowerState(PWR_3V3);
}

void updatePowerState() {
 switch (powerState) {

    case PWR_DEEP_SLEEP:
      powerState = PWR_3V3; 
      enterLightSleep();
      break;

    case PWR_3V3:
      setPowerState(PWR_3V3);

      if (upPressed) {
        upPressed = false;
        powerLastActivity = millis();
        powerState = PWR_PERIF_LOW;
      }

      if (millis() - powerLastActivity > IN_SLEEP_TIMEOUT) {
        powerState = PWR_DEEP_SLEEP;
      }
      break;

    case PWR_PERIF_LOW:
    setPowerState(PWR_PERIF_LOW);
    if (downPressed) {
        downPressed = false;
        powerLastActivity = millis();
        powerState = PWR_3V3;
      }

    if (needHighPerif) {
        needHighPerif = false;
        powerState = PWR_PERIF;
    }
      break;

    case PWR_PERIF:
    setPowerState(PWR_PERIF);
    if (!needHighPerif) {
        powerLastActivity = millis();
        powerState = PWR_PERIF_LOW;
      }
      break;
  }
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

// =====================
// BATERÍA Y HORA
// =====================
float readBattery() {
  int adc = analogRead(PIN_BAT_ADC);
  float voltage = adc * BAT_ADC_FACTOR * (3.3 / 4095.0);
  return voltage;
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";
  char buffer[6];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
  return String(buffer);
}

// =====================
// PODÓMETRO: LECTURA Y DETECCIÓN DE PASOS
// =====================
void updateStepCounter() {

  // Control de frecuencia de muestreo
  if (millis() - lastStepSample < STEP_SAMPLE_MS) return;
  lastStepSample = millis();

  int16_t AcX, AcY, AcZ;

  // Lectura de registros de aceleración
  Wire.beginTransmission(ADDR_MPU6050);
  Wire.write(0x3B); // Registro base de aceleración
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR_MPU6050, 6, true);

  if (Wire.available() < 6) return;

  // Conversión de bytes
  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();

  // Conversión a g
  float Ax = AcX / 16384.0;
  float Ay = AcY / 16384.0;
  float Az = AcZ / 16384.0;

  // Magnitud total del vector aceleración
  float A_total = sqrt(Ax * Ax + Ay * Ay + Az * Az);

  const float upperThreshold = 1.2;
  const float lowerThreshold = 0.9;

  // Detección de valle previo al paso
  if (A_total < lowerThreshold) {
    stepPossible = true;
  }

  // Detección de flanco ascendente (paso)
  if (A_total > upperThreshold && stepPossible) {
    if (millis() - lastStepTime > STEP_DEBOUNCE_MS) {
      stepCount++;
      lastStepTime = millis();

      // Actualizar pantalla solo si está visible
      if (currentScreen == SCREEN_STEPS) {
        drawStepsValue();
      }
    }
    stepPossible = false;
  }
}

// =====================
// ACTUALIZACIÓN DE TEMPERATURA IR
// =====================
void updateTemperature() {

  // Solo se actualiza si la pantalla activa es la de temperatura
  if (currentScreen != SCREEN_TEMP) return;

  // Control de periodo de muestreo
  if (millis() - lastTempRead < TEMP_UPDATE_MS) return;

  lastTempRead = millis();

  // Lectura de temperatura del objeto
  currentTemperature = mlx.readObjectTempC();

  // Actualización parcial de la pantalla
  drawTempValue();
}

// =====================
// LECTURA DEL JOYSTICK
// =====================
JoyDir readJoystickADC() {

  int adc = analogRead(PIN_JOYSTICK_ADC);

  if (adc >= ADC_CENTER_MIN && adc <= ADC_CENTER_MAX) return JOY_CENTER;
  if (adc >= ADC_UP_MIN)                          return JOY_UP;
  if (adc <= ADC_DOWN_MAX)                        return JOY_DOWN;
  if (adc >= ADC_LEFT_MIN && adc <= ADC_LEFT_MAX) return JOY_LEFT;
  if (adc >= ADC_RIGHT_MIN && adc <= ADC_RIGHT_MAX) return JOY_RIGHT;

  return JOY_NONE;
}

// =====================
// GESTIÓN DEL JOYSTICK
// =====================
void checkJoystick() {

  // Antirebote temporal
  if (millis() - lastDebounceTime < debounceDelay) return;

  JoyDir dir = readJoystickADC();
  if (dir == JOY_NONE) return;

  // Cambio de pantalla según dirección
  switch (dir) {

    case JOY_LEFT:
      currentScreen = SCREEN_STEPS;
      needHighPerif = true;
      updateScreen();
      lastDebounceTime = millis();
      break;

    case JOY_RIGHT:
      currentScreen = SCREEN_TEMP;
      needHighPerif = true;
      updateScreen();
      lastDebounceTime = millis();
      break;

    case JOY_CENTER:
      currentScreen = SCREEN_HOME;
      updateScreen();
      lastDebounceTime = millis();
      break;

    case JOY_UP:
      upPressed = true;
      break;

    case JOY_DOWN:
      downPressed = true;
      break;
    default:
      break;
  }
}

// =====================
// ACTUALIZACIÓN COMPLETA DE PANTALLA
// =====================
void updateScreen() {

  // Limpieza completa antes de dibujar nueva pantalla
  lcd.fillScreen(ST77XX_BLACK);

  switch (currentScreen) {
    case SCREEN_HOME:  drawHomeScreen();  break;
    case SCREEN_TEMP:  drawTempScreen();  break;
    case SCREEN_STEPS: drawStepsScreen(); break;
  }
}

// =====================
// PANTALLAS
// =====================

// --- Pantalla de inicio ---
void drawHomeScreen() {

  lcd.setTextColor(ST77XX_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("HOME");

  lcd.setTextSize(1);

  // Hora
  lcd.fillRect(10, 60, 100, 20, ST77XX_BLACK);
  lcd.setCursor(10, 60);
  lcd.print("Hora: ");
  lcd.println(getCurrentTime());

  // Batería
  lcd.fillRect(10, 80, 100, 20, ST77XX_BLACK);
  lcd.setCursor(10, 80);
  lcd.print("Bateria: ");
  lcd.print((int)(readBattery() / 4.2 * 100));
  lcd.println("%");

  // WiFi
  lcd.fillRect(10, 100, 150, 20, ST77XX_BLACK);
  lcd.setCursor(10, 100);
  lcd.print("WiFi: ");
  lcd.println((WiFi.status() == WL_CONNECTED) ? "CONECTADO" : "DESCONECTADO");
}


// --- Pantalla de temperatura ---
void drawTempScreen() {

  lcd.setTextColor(ST77XX_CYAN);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("TEMPERATURA");

  drawTempValue();
}

void drawTempValue() {

  // Borrado parcial para evitar parpadeo
  lcd.fillRect(10, 80, 200, 40, ST77XX_BLACK);

  lcd.setTextColor(ST77XX_CYAN);
  lcd.setTextSize(3);
  lcd.setCursor(10, 80);

  lcd.print(currentTemperature, 1);
  lcd.print(" C");
}

// --- Pantalla de pasos ---
void drawStepsScreen() {

  lcd.setTextColor(ST77XX_GREEN);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("PASOS");

  drawStepsValue();
}

void drawStepsValue() {

  // Borrado parcial del valor
  lcd.fillRect(10, 80, 200, 40, ST77XX_BLACK);

  lcd.setTextColor(ST77XX_GREEN);
  lcd.setTextSize(3);
  lcd.setCursor(10, 80);

  lcd.print(stepCount);
}

