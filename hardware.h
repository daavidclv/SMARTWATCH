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

#define ADDR_MPU6050   0x68
#define ADDR_MLX90614  0x5A

// =====================
// LCD SPI (Display)
// =====================
#define LCD_SCK   18
#define LCD_MOSI  23
#define LCD_CS     5
#define LCD_DC    16
#define LCD_RST   17
// Backlight no existe en el esquema

// =====================
// Linterna (LEDs discretos)
// =====================
#define PIN_LED_A 25
#define PIN_LED_B 26
#define PIN_LED_C 14

// =====================
// Buzzer
// =====================
#define PIN_BUZZER 13
#define PWM_FREQ_BUZZ 2000

// =====================
// Rail de periféricos
// AO3407 PMOS
// HIGH = OFF
// LOW  = ON
// =====================
#define PIN_RAIL_CTRL      33   // CONT_PERIF
#define PIN_RAIL_CTRL_LOW  32   // CONT_PERIF_LOW (filtrado)

// =====================
// ADC
// =====================
#define PIN_BAT_ADC   34
#define BAT_ADC_FACTOR 2.0f

#define PIN_JOYSTICK_ADC 35

// =====================
// Interrupciones / Wake
// =====================
#define PIN_MPU_INT 27   // INT MPU6050 (RTC GPIO)

// =====================
// WiFi
// =====================
#define WIFI_SSID "Cris"
#define WIFI_PASS "123456789"
#define WEB_SERVER_PORT 80

// =====================
// Parámetros funcionales
// =====================
#define STEP_THRESHOLD    1.2f
#define NOISE_THRESHOLD   0.3f
#define STEP_SAMPLE_MS    100
#define INACTIVITY_MS     30000

#endif
