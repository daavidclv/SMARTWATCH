#ifndef HARDWARE_H
#define HARDWARE_H

// =====================
// MCU
// =====================
#define MCU_NAME "ESP32-WROOM-32E"

// =====================
// I2C
// =====================
#define I2C_SDA 21
#define I2C_SCL 22

#define ADDR_MLX90614  0x5A
#define ADDR_MPU6050   0x68


// =====================
// LCD SPI (Display)
// =====================
#define TFT_CS   5
#define TFT_DC   17
#define TFT_RST  4

#define TFT_SCK  18
#define TFT_MOSI 23
#define TFT_MISO -1   // NO USADO

// =====================
// Linterna (LEDs discretos)
// =====================
#define PIN_LEDS 25

// =====================
// Buzzer
// =====================
#define PIN_BUZZER 19
#define PWM_FREQ_BUZZ 2000

// =====================
// Rail de periféricos
// AO3407 PMOS
// HIGH = OFF
// LOW  = ON
// =====================
#define PIN_CONT_PERIF      27   // CONT_PERIF
#define PIN_CONT_PERIF_LOW  33  // CONT_PERIF_LOW

// =====================
// ADC
// =====================
#define PIN_BAT_ADC   2
#define BAT_ADC_FACTOR 2.0f

#define PIN_JOYSTICK_ADC 32

// =====================
// Interrupciones / Wake
// =====================
#define PIN_MPU_INT 11   // INT MPU6050 (RTC GPIO)

// =====================
// WiFi
// =====================

#define WEB_SERVER_PORT 80

// =====================
// Parámetros funcionales
// =====================
#define STEP_THRESHOLD    1.2f
#define NOISE_THRESHOLD   0.3f
#define STEP_SAMPLE_MS    100
#define INACTIVITY_MS     30000

#endif
