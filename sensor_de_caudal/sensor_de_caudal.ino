volatile int pulseCount = 0;

const int sensorPin = 4;   // Pin donde conectas el YF-S401
float flowRate = 0.0;
unsigned long lastTime = 0;

// Función de interrupción
void IRAM_ATTR contarPulsos() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);

  pinMode(sensorPin, INPUT_PULLUP);

  // Configurar interrupción
  attachInterrupt(digitalPinToInterrupt(sensorPin), contarPulsos, RISING);

  Serial.println("Sensor de flujo listo...");
}

void loop() {
  if (millis() - lastTime >= 1000) { // cada 1 segundo

    noInterrupts();
    int pulsos = pulseCount;
    pulseCount = 0;
    interrupts();

    // Fórmula del YF-S401
    flowRate = pulsos / 7.5; // L/min

    Serial.print("Pulsos: ");
    Serial.print(pulsos);
    Serial.print(" | Caudal: ");
    Serial.print(flowRate);
    Serial.println(" L/min");

    lastTime = millis();
  }
}
