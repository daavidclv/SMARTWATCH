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
    digitalWrite(PIN_CONT_PERIF, LOW);
    digitalWrite(PIN_CONT_PERIF_LOW, LOW);

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

  // Dibujar pantalla inicial
  drawHomeScreen();
}

// =====================
// LOOP PRINCIPAL
// =====================
void loop() {

  // Gestión de navegación por joystick
  checkJoystick();

  // Actualización de temperatura (solo en pantalla TEMP)
  updateTemperature();

  // Actualización del contador de pasos (siempre activa)
  updateStepCounter();
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

  // Cambio de pantalla según dirección
  switch (dir) {

    case JOY_LEFT:
      currentScreen = SCREEN_STEPS;
      updateScreen();
      lastDebounceTime = millis();
      break;

    case JOY_RIGHT:
      currentScreen = SCREEN_TEMP;
      updateScreen();
      lastDebounceTime = millis();
      break;

    case JOY_CENTER:
      currentScreen = SCREEN_HOME;
      updateScreen();
      lastDebounceTime = millis();
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
void drawHomeScreen() {

  lcd.setTextColor(ST77XX_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("HOME");

  lcd.setTextSize(1);
  lcd.setCursor(10, 60);
  lcd.println("Bateria: 85%");
  lcd.setCursor(10, 80);
  lcd.println("Hora: 12:45");
  lcd.setCursor(10, 100);
  lcd.println("WiFi: CONECTADO");
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

