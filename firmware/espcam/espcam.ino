#include <Arduino.h>
#include <ESP32QRCodeReader.h>

// --- CONFIGURACIÓN DE MODELO DE CÁMARA (AI-THINKER ESP32-CAM) ---
// Definimos los pines del ESP32-CAM a los cuales se conecta el sensor de cámara OV2640
#define PWDN_GPIO_NUM     32  // Pin de alimentación (GPIO32)
#define RESET_GPIO_NUM    -1  // Pin de reset (-1 indica reset por software)
#define XCLK_GPIO_NUM      0  // Pin del cristal / reloj externo (GPIO0)
// Pines de la interfaz SCCB (Serial Camera Control Bus), mapeados a un bus I2C
#define SIOD_GPIO_NUM     26  // Pin SDA del bus SCCB (GPIO26)
#define SIOC_GPIO_NUM     27  // Pin SCL del bus SCCB (GPIO27)
// Pines de datos paralelos (D0 - D7)
#define Y9_GPIO_NUM       35  // Pin D7 (GPIO35)
#define Y8_GPIO_NUM       34  // Pin D6 (GPIO34)
#define Y7_GPIO_NUM       39  // Pin D5 (GPIO39)
#define Y6_GPIO_NUM       36  // Pin D4 (GPIO36)
#define Y5_GPIO_NUM       21  // Pin D3 (GPIO21)
#define Y4_GPIO_NUM       19  // Pin D2 (GPIO19)
#define Y3_GPIO_NUM       18  // Pin D1 (GPIO18)
#define Y2_GPIO_NUM        5  // Pin D0 (GPIO5)
// Pines de sincronización
#define VSYNC_GPIO_NUM    25  // Pin de sincronización vertical (GPIO25)
#define HREF_GPIO_NUM     23  // Pin de sincronización horizontal (GPIO23)
#define PCLK_GPIO_NUM     22  // Pin de reloj de pixel (GPIO22)

// Pin del LED rojo de estado integrado en el ESP32-CAM (GPIO 33, lógica invertida: LOW=ON, HIGH=OFF)
#define LED_RED_STATUS    33

// Instancia del lector de códigos QR por hardware con modelo AI-THINKER
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
    else if (cmd == "STOP" || cmd == "STOP_ALL") {
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
