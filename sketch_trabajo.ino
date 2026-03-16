#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <LittleFS.h>
#include <MPU6050.h>        // Jeff Rowberg
#include <Adafruit_MLX90614.h>
#include <TFT_eSPI.h>       // GC9A01

#include "hardware.h"
#include <esp_sleep.h> // Necesario para deep sleep

// =====================
// DEPURACIÓN (DEBUG MODE)
// 0x01: ON (Imprime mensajes de depuración)
// 0x00: OFF (Desactiva la impresión para producción)
// =====================
#define DEBUG_MODE 0x01

#if defined(DEBUG_MODE) && DEBUG_MODE == 0x01
    // Macro para la depuración, reemplaza Serial.printf/Serial.println
    #define DPRINTF(...) { Serial.printf(__VA_ARGS__); }
    #define DPRINTLN(msg) { Serial.println(msg); }
#else
    // Si DEBUG_MODE está en OFF, las macros se resuelven a nada
    #define DPRINTF(...)
    #define DPRINTLN(msg)
#endif

// =====================
// CONSTANTES DE BACKUP (Si hardware.h no se carga correctamente)
// NOTA: Estas constantes deberían cargarse desde hardware.h
// Se incluyen aquí para resolver el error de "not declared" si el IDE falla.
// =====================
#define LOG_FILE                    "/log.csv"
#define LOG_MAX_SIZE                (1024 * 50) // 50 KB max


// ==== Objetos ====
// Inicializar Wire con los pines I2C correctos
TwoWire I2C_Bus = TwoWire(0);
// Usamos I2C_SDA y I2C_SCL del nuevo hardware.h
MPU6050 imu; // CORRECCIÓN: Se elimina (&I2C_Bus) para evitar el error de conversión
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
// Inicializar la pantalla con los pines SPI correctos (definidos en TFT_eSPI User_Setup.h)
TFT_eSPI display = TFT_eSPI();
WebServer server(WEB_SERVER_PORT);

// ==== Variables ====
volatile int pasos = 0;
float prevMagn = 0.0f;
bool stepDetected = false;
float tempIR = 0.0f;
float voltBat = 0.0f;

unsigned long lastStepSample = 0;
unsigned long lastScreenUpdate = 0;
unsigned long joyCenterPressedTime = 0;
unsigned long lastActivityMillis = 0;

bool peripheralsOn = false;

// Estado para la linterna
static bool linternaOn = false;

// ==== Prototipos ====
void setupPeripherals();
void shutdownPeripherals();
void updateDisplay();
void setupPWM();
void beep(int ms);
void activarRails(bool on);
void logSample();
void rotateLogIfNeeded();
void enterDeepSleep();
void configureMPUMotionInterrupt();
float leerBateria();

// ISR para la interrupción del MPU6050 (solo para detectar actividad y evitar deep sleep)
void IRAM_ATTR MPU_ISR() {
    lastActivityMillis = millis();
    // La interrupción del MPU también podría usarse para incrementar 'pasos' si el MPU estuviera configurado como podómetro.
    // En este caso, solo la usamos para evitar el deep sleep por inactividad.
}

// ==== Setup ====
void setup() {
    Serial.begin(115200);
    delay(100);

    // Determinar la causa del despertar
    esp_sleep_source_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        // Despertar por botón (PIN_WAKE_BUTTON) o MPU (PIN_MPU_INT)
        DPRINTLN("Despertar por Ext1 (Movimiento o Botón)");
        // Se mantiene la lógica original: activación manual por botón pulsado
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        DPRINTLN("Despertar por Temporizador (No implementado en enterDeepSleep)");
    } else {
        DPRINTLN("Reset normal/Power-on");
    }

    // Montar LittleFS
    if (!LittleFS.begin()) DPRINTLN("No se pudo montar LittleFS");

    // Pines de control
    // Usamos la constante PIN_RAIL_CTRL (GPIO33) definida en hardware.h
    pinMode(PIN_RAIL_CTRL, OUTPUT); // Rail 3V3_PERIF (MLX/MPU/Display/Buzzer/Linterna)
    activarRails(false); // Rail OFF por defecto (HIGH)
    peripheralsOn = false;

    // Inicializar I2C para MPU/MLX
    I2C_Bus.begin(I2C_SDA, I2C_SCL); // Usamos las nuevas constantes de I2C

    setupPWM();

    // WiFi AP
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    DPRINTF("AP iniciado: %s IP: %s\n", WIFI_SSID, WiFi.softAPIP().toString().c_str());

    // Web endpoints
    server.on("/", [](){
        // HTML Mejorado: Responsive, estilos básicos y controles (botones ON/OFF/PWM)
        String html="<html><head><meta http-equiv='refresh' content='2'/><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html+="<style>";
        html+="body{font-family:Arial,sans-serif;text-align:center;padding:10px;background-color:#f4f4f9;}";
        html+="h1{color:#007bff;margin-bottom:15px;}";
        html+="h2,h3{color:#333;}";
        html+="b{font-weight:bold;}";
        html+="p{margin:5px 0;}";
        html+="button, a.button{background-color:#007bff;color:white;padding:10px 20px;border:none;border-radius:8px;cursor:pointer;margin:5px;display:inline-block;text-decoration:none;box-shadow:0 4px #0056b3;}";
        html+="button:active, a.button:active{box-shadow: 0 2px #0056b3; transform: translateY(2px);}";
        html+=".status-box{border: 1px solid #ccc; padding: 15px; margin-bottom: 20px; border-radius: 10px; background-color: #fff; box-shadow: 0 2px 4px rgba(0,0,0,0.1);}";
        html+="</style>";

        // Script para el control asíncrono del slider (UX)
        html+="<script>";
        html+="function updateBrightness(duty) {";
        html+="  document.getElementById('pwm_level').innerText = duty;"; // Muestra valor instantáneo
        html+="  fetch('/light?duty=' + duty);"; // Envía el valor sin recargar la página
        html+="}";
        html+="</script></head><body>";

        html+="<div class='status-box'>";
        html+="<h1>Wearable IoT Status</h1>";
        html+="<p><b>Pasos:</b> "+String(pasos)+"</p>";
        html+="<p><b>Temp. IR:</b> "+String(tempIR,1)+"&deg;C</p>";
        html+="<p><b>Batería:</b> "+String(voltBat,2)+" V</p>";
        html+="<p><b>Periféricos:</b> "+(peripheralsOn?"<span style='color:green;'>ON (Activo)</span>":"<span style='color:red;'>OFF (Deep Sleep)</span>")+"</p>";
        html+="</div>";

        // --- Controles ---
        if(peripheralsOn){
            html+="<h2>Controles Actuadores</h2>";

            // Control Linterna
            html+="<h3>Linterna</h3>";
            if(linternaOn){
                html+="<a class='button' style='background-color:#dc3545;box-shadow:0 4px #c82333;' href='/light?flashlight=0'>Apagar Linterna</a>";
            } else {
                html+="<a class='button' href='/light?flashlight=1'>Encender Linterna</a>";
            }

            // Control Backlight (PWM) - UX Mejorada con JS
            html+="<h3>Brillo de Pantalla (PWM)</h3>";
            html+="<div style='margin:20px 0;'>";
            html+="<input type='range' name='duty' min='0' max='255' value='"+String(ledcRead(PWM_CHANNEL_BL))+"' onchange='updateBrightness(this.value)' oninput='document.getElementById(\"pwm_level\").innerText=this.value;' style='width:80%;'>";
            html+="<p>Nivel PWM: <span id='pwm_level'>"+String(ledcRead(PWM_CHANNEL_BL))+"</span></p>";
            html+="</div>";

            // Botón de forzar medición/log
            html+="<h3>Medición Manual</h3>";
            html+="<a class='button' style='background-color:#28a745;box-shadow:0 4px #1e7e34;' href='/measure'>Forzar Medición y Log</a>";

        } else {
             html+="<p style='color:red;padding:10px;border:1px dashed red;border-radius:5px;'>El control total (ON/OFF) requiere una pulsación larga del Joystick Central (2s) por protocolo de bajo consumo.</p>";
        }

        html+="<hr style='margin:30px 0;'>";
        html+="<h2>Sincronización de Hora</h2>";
        // Se mantiene la explicación del endpoint /syncTime
        html+="<p>La hora se sincroniza automáticamente por SNTP al encender.</p>";
        html+="<p>Sincronización manual: <pre>/syncTime?ts=[timestamp_unix]</pre></p>";

        html+="</body></html>";
        server.send(200,"text/html",html);
    });
    server.on("/status", [](){
        String json="{\"steps\":"+String(pasos)+",\"temp\":"+String(tempIR,1)+",\"bat\":"+String(voltBat,2)+",\"active\":"+(peripheralsOn?"true":"false")+"}";
        server.send(200,"application/json",json);
    });
    server.on("/measure", [](){
        // Forzar lectura si no está activo, pero idealmente solo funciona si peripheralsOn es true
        if(!peripheralsOn){
             server.send(400,"text/plain","Peripherals are OFF. Turn ON first.");
             return;
        }
        tempIR = mlx.readObjectTempC();
        voltBat = leerBateria();
        logSample(); // Registrar la muestra
        server.send(200,"application/json","{\"temp\":"+String(tempIR,1)+",\"bat\":"+String(voltBat,2)+"}");
    });
    server.on("/light", [](){
        if(server.hasArg("duty")){
            int d=constrain(server.arg("duty").toInt(),0,255);
            ledcWrite(PWM_CHANNEL_BL,d);
            lastActivityMillis = millis(); // Actividad por interacción web
            server.send(200,"text/plain","OK");
        } else if (server.hasArg("flashlight")) {
            // Control de linterna ON/OFF (0 o 1)
            int d = server.arg("flashlight").toInt();
            linternaOn = (d > 0);
            ledcWrite(PWM_CHANNEL_LED, linternaOn ? 255 : 0);
            lastActivityMillis = millis();
            server.send(200, "text/plain", linternaOn ? "Flashlight ON" : "Flashlight OFF");
        } else {
            server.send(400,"text/plain","Missing duty or flashlight arg");
        }
    });
    server.on("/syncTime", [](){
        if(server.hasArg("ts")){
            long ts=server.arg("ts").toInt();
            struct timeval tv={ts,0};
            settimeofday(&tv,NULL);
            server.send(200,"text/plain","Time set");
            lastActivityMillis = millis(); // Actividad por interacción web
        } else server.send(400,"text/plain","Missing ts");
    });
    server.begin();

    // SNTP
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    // Configurar pin de interrupción MPU (siempre activo para wake-up)
    pinMode(PIN_MPU_INT, INPUT_PULLUP);

    DPRINTLN("Setup completo.");
}

// ==== Loop principal ====
void loop() {
    server.handleClient();
    unsigned long now = millis();

    // --- Lectura joystick center (PIN_WAKE_BUTTON/GPIO32) ---
    // El pin 32 es un pin RTC, lo que permite el wake-up de deep sleep.
    // Usamos PIN_WAKE_BUTTON para la lógica de encendido/apagado/wake-up.
    pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
    int joyState = digitalRead(PIN_WAKE_BUTTON); // Leemos el botón de wake-up

    if (joyState == LOW && joyCenterPressedTime == 0) joyCenterPressedTime = now;

    // Si se suelta el botón...
    if (joyState == HIGH && joyCenterPressedTime != 0){
        unsigned long duration = now - joyCenterPressedTime;
        joyCenterPressedTime = 0;

        // Pulsación Larga: ENCENDER Periféricos (2s - 5s)
        if(duration >= 2000 && duration < 5000 && !peripheralsOn){
            activarRails(true);
            peripheralsOn = true;
            DPRINTLN("Periféricos ENCENDIDOS (pulsación 2s)");
            setupPeripherals();
            lastActivityMillis = now;
        }
        // Pulsación Muy Larga: APAGAR Periféricos (>= 5s)
        else if(duration >= 5000 && peripheralsOn){
            DPRINTLN("Periféricos APAGADOS (pulsación 5s)");
            shutdownPeripherals();
            activarRails(false);
            peripheralsOn = false;
            // Se debe entrar a deep sleep inmediatamente después de apagar
            enterDeepSleep();
        }
    }

    // Si el botón se mantiene presionado para apagado
    else if (peripheralsOn && joyCenterPressedTime != 0 && (now - joyCenterPressedTime) >= 5000) {
         // Indicador visual/sonoro de que se va a apagar
         beep(50);
         // Dejar que se detecte el soltado del botón para apagar
    }

    // --- Sensores, pantalla y log ---
    if(peripheralsOn){
        // Conteo pasos (polling)
        if(now - lastStepSample >= STEP_SAMPLE_MS){
            int16_t ax,ay,az;
            imu.getAcceleration(&ax,&ay,&az);
            float aX=ax/16384.0f,aY=ay/16384.0f,aZ=az/16384.0f;
            float magn = sqrt(aX*aX + aY*aY + aZ*aZ);
            // Usamos las constantes actualizadas STEP_THRESHOLD y NOISE_THRESHOLD
            if((magn-prevMagn)>STEP_THRESHOLD && !stepDetected){ pasos++; stepDetected=true; lastActivityMillis=now;}
            if(fabs(magn-prevMagn)<NOISE_THRESHOLD) stepDetected=false;
            prevMagn = magn;
            lastStepSample = now;
        }

        // Lectura temperatura, batería y actualización de display
        if(now - lastScreenUpdate >= 1000){
            tempIR = mlx.readObjectTempC();
            voltBat = leerBateria();
            updateDisplay();
            logSample();
            lastScreenUpdate = now;
        }

        // Joystick up/down/left/right (usamos las nuevas constantes)
        pinMode(JOY_LEFT, INPUT_PULLUP); pinMode(JOY_RIGHT, INPUT_PULLUP);
        pinMode(JOY_UP, INPUT_PULLUP); pinMode(JOY_DOWN, INPUT_PULLUP);

        if(digitalRead(JOY_LEFT)==LOW){ ledcWrite(PWM_CHANNEL_BL,max(0,ledcRead(PWM_CHANNEL_BL)-20)); lastActivityMillis=now; delay(200);}
        if(digitalRead(JOY_RIGHT)==LOW){ ledcWrite(PWM_CHANNEL_BL,min(255,ledcRead(PWM_CHANNEL_BL)+20)); lastActivityMillis=now; delay(200);}
        if(digitalRead(JOY_UP)==LOW){ linternaOn=!linternaOn; ledcWrite(PWM_CHANNEL_LED,linternaOn?255:0); lastActivityMillis=now; delay(200);}
        if(digitalRead(JOY_DOWN)==LOW){ beep(200); lastActivityMillis=now; delay(200);}
    }

    // --- Inactividad ---
    // Solo si los periféricos están encendidos
    if(peripheralsOn && (now - lastActivityMillis) > INACTIVITY_MS){
        DPRINTLN("Inactividad. Entrando deep sleep...");
        delay(100);
        enterDeepSleep();
    }

    delay(10);
}

// ==== Funciones auxiliares ====

// El rail 3V3_PERIF se controla con PIN_RAIL_CTRL (HIGH=OFF, LOW=ON)
void activarRails(bool on){
    digitalWrite(PIN_RAIL_CTRL, on ? LOW : HIGH);
    DPRINTF("Rail 3V3_PERIF %s\n", on ? "ON (LOW)" : "OFF (HIGH)");
}

void setupPeripherals(){
    if(!peripheralsOn) return;

    // MPU6050
    imu.initialize();
    if(!imu.testConnection()){
        DPRINTLN("MPU6050 NO detectado!");
    } else {
        configureMPUMotionInterrupt();
        // Adjuntar ISR al pin de interrupción del MPU
        attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), MPU_ISR, RISING);
    }

    // MLX90614
    if(!mlx.begin()){
        DPRINTLN("MLX90614 NO detectado!");
    }

    // TFT_eSPI (ya configurado en User_Setup.h con los pines correctos)
    display.init();
    display.setRotation(0);
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE);
    display.setTextSize(2);
    ledcWrite(PWM_CHANNEL_BL,200);
    lastActivityMillis = millis();
}

void shutdownPeripherals(){
    // Apagar todos los actuadores antes de cortar el rail
    ledcWrite(PWM_CHANNEL_LED,0);
    ledcWrite(PWM_CHANNEL_BUZZ,0);
    ledcWrite(PWM_CHANNEL_BL,0);
    display.fillScreen(TFT_BLACK);
    // Detener I2C y SPI si es posible, aunque el corte de rail (GPIO33) lo hace físicamente.
    // Detaching ISR
    detachInterrupt(digitalPinToInterrupt(PIN_MPU_INT));
}

void updateDisplay(){
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(8,6); display.println("Wearable IoT");

    display.setTextSize(3); display.setCursor(8,40); display.println("Pasos: "+String(pasos));

    display.setTextSize(2); display.setCursor(8,100); display.println("Temp: "+String(tempIR,1)+" C");
    display.setCursor(8,130); display.println("Bat: "+String(voltBat,2)+" V");

    struct tm t;
    if(getLocalTime(&t)){
        char buf[32];
        strftime(buf,sizeof(buf),"%H:%M:%S %d/%m/%Y",&t);
        display.setCursor(8,170);
        display.setTextSize(1);
        display.print(buf);
    } else {
        display.setCursor(8,170);
        display.setTextSize(1);
        display.print("Hora: --:--:--");
    }
}

void setupPWM(){
    // PWM Backlight (LCD_BL)
    ledcSetup(PWM_CHANNEL_BL,PWM_FREQ_BL,8);
    ledcAttachPin(LCD_BL,PWM_CHANNEL_BL);
    ledcWrite(PWM_CHANNEL_BL,0); // Inicialmente apagado
    // PWM Linterna (PIN_LINTERN_LED)
    ledcSetup(PWM_CHANNEL_LED,PWM_FREQ_LED,8);
    ledcAttachPin(PIN_LINTERN_LED,PWM_CHANNEL_LED);
    ledcWrite(PWM_CHANNEL_LED,0);
    // PWM Buzzer (PIN_BUZZER)
    ledcSetup(PWM_CHANNEL_BUZZ,PWM_FREQ_BUZZ,8);
    ledcAttachPin(PIN_BUZZER,PWM_CHANNEL_BUZZ);
    ledcWrite(PWM_CHANNEL_BUZZ,0);
}

void beep(int ms){
    ledcWrite(PWM_CHANNEL_BUZZ,150);
    delay(ms);
    ledcWrite(PWM_CHANNEL_BUZZ,0);
}

// ADC batería (PIN_BAT_ADC) con divisor 100k/100k (ADC_FACTOR=2.0)
float leerBateria(){
    // Usamos las constantes actualizadas
    return analogRead(PIN_BAT_ADC)/4095.0f * 3.3f * ADC_FACTOR;
}

// Configurar MPU6050 para generar interrupción al detectar movimiento
void configureMPUMotionInterrupt(){
    // Restablecer/apagar antes de configurar
    imu.setSleepEnabled(false);
    imu.setMotionDetectionThreshold(2); // Umbral de 2mg (ajustar si es necesario)
    imu.setMotionDetectionDuration(10); // 10ms (ajustar si es necesario)
    imu.setZeroMotionDetectionThreshold(0);
    imu.setZeroMotionDetectionDuration(0);
    imu.setDMPEnabled(false);
    // Habilitar la interrupción por detección de movimiento
    imu.setIntMotionEnabled(true);
    // Configurar el pin de interrupción
    imu.setInterruptMode(true); // Modo 'latch' (se mantiene hasta que se lee el registro)
    imu.setInterruptDrive(false); // Push-pull (true = Open Drain)
    imu.setInterruptLatch(false); // Pulse (true = Latch)
    DPRINTLN("MPU Motion Interrupt configurado.");
}

void logSample(){
    rotateLogIfNeeded();
    time_t nowt = time(NULL);
    // Formato: timestamp,pasos,tempIR,voltBat
    String line = String((unsigned long)nowt)+","+String(pasos)+","+String(tempIR,1)+","+String(voltBat,2)+"\n";
    File f = LittleFS.open(LOG_FILE,"a");
    if(f){
        f.print(line);
        f.close();
    } else {
        DPRINTF("Error al escribir en %s\n", LOG_FILE);
    }
}

void rotateLogIfNeeded(){
    if(!LittleFS.exists(LOG_FILE)) return;

    File f = LittleFS.open(LOG_FILE,"r");
    if(f.size() <= LOG_MAX_SIZE){
        f.close();
        return;
    }

    // Si el archivo es muy grande, se trunca a la mitad más reciente.
    f.seek(f.size()-LOG_MAX_SIZE/2);
    File tmp = LittleFS.open("/tmp.log","w");

    // Copiar la mitad más reciente
    while(f.available()) tmp.write(f.read());

    f.close();
    tmp.close();

    LittleFS.remove(LOG_FILE);
    LittleFS.rename("/tmp.log",LOG_FILE);
    DPRINTF("Log file rotated. New size: %d\n", LittleFS.open(LOG_FILE,"r").size());
}

void enterDeepSleep(){
    // 1. Apagar Periféricos
    shutdownPeripherals();
    activarRails(false); // Cortar la alimentación física de los periféricos
    delay(50); // Pequeño retardo para asegurar que los comandos se ejecutan

    // 2. Configurar Wake-up
    // Bitmask para PIN_WAKE_BUTTON (GPIO32) y PIN_MPU_INT (GPIO13)
    uint64_t mask = (1ULL << PIN_WAKE_BUTTON) | (1ULL << PIN_MPU_INT);
    // Usamos ESP_EXT1_WAKEUP_ANY_LOW porque tanto el botón como el MPU (si está configurado con lógica LOW) lo activan.
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

    // 3. Entrar en Sleep
    DPRINTLN("Entrando deep sleep...");
    delay(50);
    esp_deep_sleep_start();
}
