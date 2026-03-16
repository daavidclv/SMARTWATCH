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
// Buzzer
// =====================
#define PIN_BUZZER 19
#define PWM_FREQ_BUZZ 2000



// =====================
// ADC
// =====================

#define PIN_JOYSTICK_ADC 32



// =====================
// WiFi
// =====================

#define WEB_SERVER_PORT 80



#endif
