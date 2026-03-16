#include <Arduino.h>
#include "hardware.h"

void encenderLinterna(bool estado);

void setup() {
  // Inicializar UART (USB)
  Serial.begin(115200);
  delay(1000);

  // Configurar pines de la linterna
  pinMode(PIN_LED_A, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_LED_C, OUTPUT);

  // Apagar linterna al inicio
  encenderLinterna(false);

  Serial.println("=== Prueba UART ESP32 ===");
  Serial.println("Enviar:");
  Serial.println("  ON  -> Enciende linterna");
  Serial.println("  OFF -> Apaga linterna");
}

void loop() {
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();   // Elimina espacios y saltos de línea

    if (comando.equalsIgnoreCase("ON")) {
      encenderLinterna(true);
      Serial.println("Linterna ENCENDIDA");
    }
    else if (comando.equalsIgnoreCase("OFF")) {
      encenderLinterna(false);
      Serial.println("Linterna APAGADA");
    }
    else {
      Serial.print("Comando desconocido: ");
      Serial.println(comando);
    }
  }
}

void encenderLinterna(bool estado) {
  digitalWrite(PIN_LED_A, estado);
  digitalWrite(PIN_LED_B, estado);
  digitalWrite(PIN_LED_C, estado);
}
