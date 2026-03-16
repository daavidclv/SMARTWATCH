/*****************************************************
 *  joystick_adc_test.ino
 *
 *  Lee el valor ADC del joystick y lo imprime
 *  por el monitor serie.
 *
 *  ESP32
 *****************************************************/

#include <Arduino.h>

// =====================
// DEFINICIÓN DE PINES
// =====================

// Pin ADC donde está conectado el joystick
// (en tu esquema es GPIO 35)
const int PIN_JOYSTICK_ADC = 35;

// Tiempo entre lecturas (ms)
const unsigned long READ_INTERVAL_MS = 100;

// Control de tiempo
unsigned long lastReadTime = 0;

void setup() {

  // Iniciar comunicación serie
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Test ADC Joystick ===");

  // Configurar el pin ADC como entrada
  pinMode(PIN_JOYSTICK_ADC, INPUT);

  // Configurar resolución ADC del ESP32
  // 12 bits -> valores de 0 a 4095
  analogReadResolution(12);

  Serial.println("Moviendo el joystick...");
}

void loop() {

  // Leer cada cierto tiempo
  if (millis() - lastReadTime >= READ_INTERVAL_MS) {

    lastReadTime = millis();

    // Leer valor ADC
    int adcValue = analogRead(PIN_JOYSTICK_ADC);

    // Imprimir valor
    Serial.print("ADC Joystick = ");
    Serial.println(adcValue);
  }
}
