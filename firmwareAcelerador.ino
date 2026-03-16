#include #define MPU6050_ADDR 0x68

long lastStepTime = 0;
int stepCount = 0;

void setup() {
Serial.begin(115200);
Wire.begin(21, 22);

// Inicializa MPU6050
Wire.beginTransmission(MPU6050_ADDR);
Wire.write(0x6B);
Wire.write(0);
Wire.endTransmission(true);

Serial.println("Contador de pasos iniciado");
}

void loop() {
int16_t AcX, AcY, AcZ;
Wire.beginTransmission(MPU6050_ADDR);
Wire.write(0x3B);
Wire.endTransmission(false);
Wire.requestFrom(MPU6050_ADDR, 6, true);

AcX = Wire.read() << 8 | Wire.read();
AcY = Wire.read() << 8 | Wire.read();
AcZ = Wire.read() << 8 | Wire.read();

// Convertir a 'g'
float Ax = AcX / 16384.0;
float Ay = AcY / 16384.0;
float Az = AcZ / 16384.0;

// Magnitud total
float A_total = sqrt(Ax*Ax + Ay*Ay + Az*Az);

// Umbrales de detección
float upperThreshold = 1.2;
float lowerThreshold = 0.9;

static bool stepPossible = false;

// Detección de flanco ascendente (paso)
if (A_total < lowerThreshold) {
stepPossible = true;
}
if (A_total > upperThreshold && stepPossible) {
if (millis() - lastStepTime > 300) { // evitar falsos pasos
stepCount++;
lastStepTime = millis();
Serial.print("Paso detectado! Total: ");
Serial.println(stepCount);
}
stepPossible = false;
}

delay(50); // 20 Hz de muestreo
}
