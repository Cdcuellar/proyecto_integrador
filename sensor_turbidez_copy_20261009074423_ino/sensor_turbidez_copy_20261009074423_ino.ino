
#define SENSOR_TURBIDEZ 34

const int muestras = 50;

// Voltajes obtenidos experimentalmente
const float V_LIMPIA = 1.021;
const float V_MEDIA  = 0.957;
const float V_TURBIA = 0.904;

float voltaje = 0;
float nivelSensor = 0;
int adcPromedio = 0;

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  delay(1000);

  Serial.println("==============================");
  Serial.println(" SENSOR DE TURBIDEZ ESP32");
  Serial.println("==============================");
}

void loop() {

  long suma = 0;

  for (int i = 0; i < muestras; i++) {
    suma += analogRead(SENSOR_TURBIDEZ);
    delay(5);
  }

  adcPromedio = suma / muestras;

  // Conversion calibrada a milivoltios
  voltaje = analogReadMilliVolts(SENSOR_TURBIDEZ) / 1000.0;

  // Calibracion por tramos
  if (voltaje <= V_TURBIA) {
    nivelSensor = 0;
  }
  else if (voltaje < V_MEDIA) {
    nivelSensor = (voltaje - V_TURBIA) *
                  50.0 / (V_MEDIA - V_TURBIA);
  }
  else if (voltaje < V_LIMPIA) {
    nivelSensor = 50.0 +
                  (voltaje - V_MEDIA) *
                  50.0 / (V_LIMPIA - V_MEDIA);
  }
  else {
    nivelSensor = 100;
  }

  Serial.println("------------------------------");

  Serial.print("ADC promedio : ");
  Serial.println(adcPromedio);

  Serial.print("Voltaje      : ");
  Serial.print(voltaje, 3);
  Serial.println(" V");

  Serial.print("Nivel sensor : ");
  Serial.print(nivelSensor, 1);
  Serial.println(" %");

  if (nivelSensor < 30) {
    Serial.println("Estado       : AGUA TURBIA");
  }
  else if (nivelSensor < 70) {
    Serial.println("Estado       : AGUA MEDIA");
  }
  else {
    Serial.println("Estado       : AGUA LIMPIA");
  }

  Serial.println("------------------------------");

  delay(1000);
}
