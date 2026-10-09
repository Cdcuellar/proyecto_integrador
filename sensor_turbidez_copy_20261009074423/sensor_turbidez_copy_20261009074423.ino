#define SENSOR_TURBIDEZ 34

const int muestras = 50;

float voltaje = 0;
int adcPromedio = 0;

const float VOLTAJE_MAX = 0.9;  // máximo esperado del sensor

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

  for(int i = 0; i < muestras; i++) {
    suma += analogRead(SENSOR_TURBIDEZ);
    delay(5);
  }


  adcPromedio = suma / muestras;


  // Conversión ADC a voltaje real
  voltaje = (adcPromedio * 3.3) / 4095.0;


  // Escalamiento usando 0 - 0.9V
  float turbidez = (voltaje / VOLTAJE_MAX) * 100;


  // Limitar entre 0 y 100 %
  if(turbidez > 100)
      turbidez = 100;

  if(turbidez < 0)
      turbidez = 0;


  Serial.println("------------------------------");

  Serial.print("ADC promedio : ");
  Serial.println(adcPromedio);

  Serial.print("Voltaje      : ");
  Serial.print(voltaje,3);
  Serial.println(" V");


  Serial.print("Nivel sensor : ");
  Serial.print(turbidez,1);
  Serial.println(" %");


  if(turbidez < 30)
      Serial.println("Estado       : AGUA TURBIA");

  else if(turbidez < 70)
      Serial.println("Estado       : AGUA MEDIA");

  else
      Serial.println("Estado       : AGUA LIMPIA");


  Serial.println("------------------------------");


  delay(1000);
}