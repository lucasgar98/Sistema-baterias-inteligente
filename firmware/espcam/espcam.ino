#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// --- CONFIGURACIÓN DE MODELO DE CÁMARA (AI-THINKER) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define LED_RED_STATUS 33  // LED Rojo integrado en la parte trasera (Active LOW)
#define LED_FLASH      4   // LED Flash frontal blanco de alto brillo (Active HIGH)

WebServer server(80);

String wifi_ssid = "";
String wifi_pass = "";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

void handle_stream() {
  WiFiClient client = server.client();

  // Enviar cabeceras HTTP de MJPEG
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: ");
  client.print(_STREAM_CONTENT_TYPE);
  client.print("\r\n\r\n");

  // Encendemos el flash frontal de la cámara para iluminar (Desactivado por defecto para evitar caídas de tensión y reflejos)
  // digitalWrite(LED_FLASH, HIGH);

  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      break;
    }

    // Escribir cabecera de parte
    client.print(_STREAM_BOUNDARY);
    char head[128];
    sprintf(head, _STREAM_PART, fb->len);
    client.print(head);

    // Escribir buffer JPEG
    client.write(fb->buf, fb->len);

    esp_camera_fb_return(fb);

    // Parpadeo corto del LED indicador trasero
    digitalWrite(LED_RED_STATUS, !digitalRead(LED_RED_STATUS));

    // Controlar FPS (delay de 100ms = ~10 FPS, ideal para bajo lag)
    delay(100);
  }

  // Apagar flash y apagar LED trasero al desconectar
  // digitalWrite(LED_FLASH, LOW);
  digitalWrite(LED_RED_STATUS, HIGH); // Apagado
}

void get_config_from_master() {
  unsigned long last_request = 0;
  String rx_buf = "";

  while (wifi_ssid.length() == 0) {
    // Latido LED mientras espera config
    digitalWrite(LED_RED_STATUS, !digitalRead(LED_RED_STATUS));
    delay(200);

    if (millis() - last_request > 2000) {
      Serial.println("GET_CONFIG");
      last_request = millis();
    }

    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n') {
        rx_buf.trim();
        if (rx_buf.startsWith("VALUE_CONFIG:")) {
          String data = rx_buf.substring(13);
          int comma = data.indexOf(',');
          if (comma > 0) {
            wifi_ssid = data.substring(0, comma);
            wifi_pass = data.substring(comma + 1);
            wifi_ssid.trim();
            wifi_pass.trim();
          }
        }
        rx_buf = "";
      } else if (c != '\r') {
        rx_buf += c;
      }
    }
  }
  
  // Apagar LED de estado al salir
  digitalWrite(LED_RED_STATUS, HIGH);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_RED_STATUS, OUTPUT);
  pinMode(LED_FLASH, OUTPUT);
  
  digitalWrite(LED_RED_STATUS, HIGH); // Apagado
  digitalWrite(LED_FLASH, LOW);       // Apagado

  // 1. OBTENER CONFIG DE RED DESDE EL MASTER
  get_config_from_master();

  // 2. INICIALIZAR CÁMARA
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; // Restaurado a 20MHz para asegurar compatibilidad con todos los sensores
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Configuración de resolución SVGA para un QR nítido
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12; // Calidad estable para evitar saturación de bus
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // Parpadeo de error infinito si falla la inicialización de la cámara
    while(true) {
      digitalWrite(LED_RED_STATUS, LOW);
      delay(100);
      digitalWrite(LED_RED_STATUS, HIGH);
      delay(100);
    }
  }

  // Ajustes de orientación de imagen
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_hmirror(s, 0); // Desactivar espejo
    s->set_vflip(s, 1);   // Voltear vertical
  }

  // 3. CONECTAR A WI-FI
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    digitalWrite(LED_RED_STATUS, LOW);
    delay(100);
    digitalWrite(LED_RED_STATUS, HIGH);
    delay(150);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Notificar IP al Master por Serial
    Serial.print("IP:");
    Serial.println(WiFi.localIP());
    
    // Iniciar servidor HTTP
    server.on("/stream", handle_stream);
    server.begin();
    
    // Parpadeo de éxito
    for(int i = 0; i < 3; i++) {
      digitalWrite(LED_RED_STATUS, LOW);
      delay(200);
      digitalWrite(LED_RED_STATUS, HIGH);
      delay(200);
    }
  } else {
    // Si no conecta, se reinicia para intentar nuevamente
    ESP.restart();
  }
}

void loop() {
  server.handleClient();
  
  // 1. Escuchar si el Master ordena reiniciar (ej. por cambio de red)
  while (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "RESET") {
      ESP.restart();
    }
  }

  // 2. Reporte periódico de la IP al Master (cada 10 segundos)
  static unsigned long last_ip_report = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - last_ip_report > 10000) {
    Serial.print("IP:");
    Serial.println(WiFi.localIP());
    last_ip_report = millis();
  }

  // 3. Watchdog de conexión Wi-Fi (si pierde conexión por más de 15s, reinicia)
  static unsigned long last_wifi_check = 0;
  if (millis() - last_wifi_check > 15000) {
    if (WiFi.status() != WL_CONNECTED) {
      ESP.restart();
    }
    last_wifi_check = millis();
  }
  
  delay(1);
}
