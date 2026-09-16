#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando prueba de hardware de la pantalla...");

  // Encender retroiluminación de la Cheap Yellow Display (GPIO 21)
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  // Inicializar pantalla y rotación
  tft.begin();
  tft.setRotation(1); // Modo horizontal (landscape)
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Iniciando Test...", 10, 10);
  delay(1500);
}

void loop() {
  Serial.println("Paso: Rojo");
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.drawString("COLOR: ROJO", 60, 100);
  delay(1000);

  Serial.println("Paso: Verde");
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextSize(2);
  tft.drawString("COLOR: VERDE", 60, 100);
  delay(1000);

  Serial.println("Paso: Azul");
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(2);
  tft.drawString("COLOR: AZUL", 60, 100);
  delay(1000);

  Serial.println("Paso: Blanco");
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("TEST HARDWARE OK", 50, 100);
  tft.drawString("Pantalla Sana", 75, 130);
  delay(2000);
}
