#include <Arduino.h>
#include <ESP32QRCodeReader.h>

// Pin del LED rojo de estado integrado en el ESP32-CAM (GPIO 33, lógica invertida: LOW=ON, HIGH=OFF)
#define LED_RED_STATUS    33

// Instancia del lector de códigos QR por hardware con modelo AI-THINKER
// (Nota: CAMERA_MODEL_AI_THINKER ya mapea internamente todos los pines del sensor OV2640)
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

// Estados de la máquina de escaneo autónomo
enum ScannerState {
  STATE_IDLE,
  STATE_SCANNING
};

ScannerState currentState = STATE_IDLE;
unsigned long scanStartTime = 0;
const unsigned long SCAN_TIMEOUT_MS = 30000; // 30 segundos de tiempo de escaneo

void setup() {
  // Inicializamos la comunicación serie por hardware a 115200 baudios (pines TX=GPIO1, RX=GPIO3 conectados al Master)
  Serial.begin(115200);
  delay(1000);

  // Configuración del LED de estado
  pinMode(LED_RED_STATUS, OUTPUT);
  digitalWrite(LED_RED_STATUS, HIGH); // Apagado por defecto

  // Inicialización del lector de código QR y del sensor de cámara
  // Se utiliza resolución adecuada y 1 frame buffer para preservar estabilidad en PSRAM/DRAM
  reader.setup();
  reader.begin();

  // Breve parpadeo de inicio para confirmar arranque del firmware
  digitalWrite(LED_RED_STATUS, LOW);
  delay(300);
  digitalWrite(LED_RED_STATUS, HIGH);

  Serial.println("ESP32-CAM QR READER READY");
}

void loop() {
  // 1. Escucha de comandos entrantes desde el ESP32 Master por puerto serie
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "SCAN" || cmd == "START_SCAN") {
      currentState = STATE_SCANNING;
      scanStartTime = millis();
      digitalWrite(LED_RED_STATUS, LOW); // Encender LED indicando escaneo activo
      Serial.println("SCAN_STARTING");
    } 
    else if (cmd == "STOP" || cmd == "STOP_ALL" || cmd == "CANCEL_SCAN") {
      currentState = STATE_IDLE;
      digitalWrite(LED_RED_STATUS, HIGH); // Apagar LED
      Serial.println("SCAN_STOPPED");
    }
  }

  // 2. Procesamiento según el estado actual
  if (currentState == STATE_SCANNING) {
    // Verificamos si se excedió el tiempo máximo de búsqueda
    if (millis() - scanStartTime > SCAN_TIMEOUT_MS) {
      currentState = STATE_IDLE;
      digitalWrite(LED_RED_STATUS, HIGH);
      Serial.println("SCAN_TIMEOUT");
      return;
    }

    // Intentamos recibir un código QR decodificado desde la cola del lector de cámara
    struct QRCodeData qrCodeData;
    if (reader.receiveQrCode(&qrCodeData, 100)) {
      if (qrCodeData.valid) {
        String payload = (const char *)qrCodeData.payload;
        payload.trim();

        // Enviamos el código QR decodificado al ESP32 Master por puerto serie
        Serial.print("QR:");
        Serial.println(payload);

        // Volvemos al estado de reposo tras la lectura exitosa
        currentState = STATE_IDLE;
        digitalWrite(LED_RED_STATUS, HIGH);

        // Señal luminosa de confirmación de captura exitosa (doble destello)
        delay(100);
        digitalWrite(LED_RED_STATUS, LOW);
        delay(150);
        digitalWrite(LED_RED_STATUS, HIGH);
        delay(100);
        digitalWrite(LED_RED_STATUS, LOW);
        delay(150);
        digitalWrite(LED_RED_STATUS, HIGH);
      }
    }
  } 
  else {
    // En reposo (STATE_IDLE): drenamos la cola periódicamente para descartar tramas residuales
    struct QRCodeData dummy;
    reader.receiveQrCode(&dummy, 50);
    delay(50);
  }
}
