/************************************************************
 *  main.ino
 *  
 *  Sistema de pantallas con LCD SPI controladas
 *  mediante joystick analógico (ADC).
 ************************************************************/

#include <Arduino.h>
#include "hardware.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// =====================
// LCD
// =====================
Adafruit_ST7789 lcd = Adafruit_ST7789(
  LCD_CS,
  LCD_DC,
  LCD_RST
);

// =====================
// PIN JOYSTICK ADC
// =====================
#define PIN_JOYSTICK_ADC 35

// =====================
// PANTALLAS
// =====================
enum Screen {
  SCREEN_HOME,
  SCREEN_TEMP,
  SCREEN_STEPS
};

Screen currentScreen = SCREEN_HOME;

// =====================
// DIRECCIONES JOYSTICK
// =====================
enum JoyDir {
  JOY_NONE,
  JOY_UP,
  JOY_DOWN,
  JOY_LEFT,
  JOY_RIGHT,
  JOY_CENTER
};

// =====================
// UMBRALES ADC (AJUSTAR)
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
// ANTIREBOTE
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
void drawStepsScreen();

// =====================
// SETUP
// =====================
void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("Sistema iniciado (Joystick ADC)");

  // Configuración ADC ESP32
  pinMode(PIN_JOYSTICK_ADC, INPUT);
  analogReadResolution(12); // 0–4095

  // LCD
  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  lcd.init(240, 240);
  lcd.setRotation(2);
  lcd.fillScreen(ST77XX_BLACK);

  drawHomeScreen();
}

// =====================
// LOOP
// =====================
void loop() {
  checkJoystick();
}

// =====================
// LEER JOYSTICK ADC
// =====================
JoyDir readJoystickADC() {

  int adc = analogRead(PIN_JOYSTICK_ADC);

  Serial.print("ADC: ");
  Serial.println(adc);

  if (adc >= ADC_CENTER_MIN && adc <= ADC_CENTER_MAX)
    return JOY_CENTER;

  if (adc >= ADC_UP_MIN)
    return JOY_UP;

  if (adc <= ADC_DOWN_MAX)
    return JOY_DOWN;

  if (adc >= ADC_LEFT_MIN && adc <= ADC_LEFT_MAX)
    return JOY_LEFT;

  if (adc >= ADC_RIGHT_MIN && adc <= ADC_RIGHT_MAX)
    return JOY_RIGHT;

  return JOY_NONE;
}

// =====================
// CHECK JOYSTICK
// =====================
void checkJoystick() {

  if (millis() - lastDebounceTime < debounceDelay) return;

  JoyDir dir = readJoystickADC();

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
// ACTUALIZAR PANTALLA
// =====================
void updateScreen() {

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

void drawTempScreen() {

  lcd.setTextColor(ST77XX_CYAN);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("TEMPERATURA");

  lcd.setTextSize(3);
  lcd.setCursor(10, 80);
  lcd.println("36.5 C");
}

void drawStepsScreen() {

  lcd.setTextColor(ST77XX_GREEN);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("PASOS");

  lcd.setTextSize(3);
  lcd.setCursor(10, 80);
  lcd.println("1234");
}
