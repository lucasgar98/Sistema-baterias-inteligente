#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>  // Biblioteca para crear un servidor HTTP
#include "esp_camera.h"  // Driver para la cámara

// --- CONFIGURACIÓN DE MODELO DE CÁMARA (AI-THINKER) ---
// Definimos los pines del ESP32CAM a los cuales se conecta la cámara OV2640
#define PWDN_GPIO_NUM     32  // Pin de alimentación (GPIO32)
#define RESET_GPIO_NUM    -1  // Pin de reset (se pone en -1 para indicar que no se utilizará, ya que en su lugar se realizará un reset de software)
#define XCLK_GPIO_NUM      0  // Pin del cristal (GPIO0)
// Pines de la interfaz SCCB (Serial Camera Control Bus) de la cámara, los cuales se mapean a un bus I2C
#define SIOD_GPIO_NUM     26  // Pin SDA del bus I2C (GPIO26)
#define SIOC_GPIO_NUM     27  // Pin SCL del bus I2C (GPIO27)
// Pines de datos de la cámara
#define Y9_GPIO_NUM       35  // Pin D7 (GPIO35)
#define Y8_GPIO_NUM       34  // Pin D6 (GPIO34)
#define Y7_GPIO_NUM       39  // Pin D5 (GPIO39)
#define Y6_GPIO_NUM       36  // Pin D4 (GPIO36)
#define Y5_GPIO_NUM       21  // Pin D3 (GPIO21)
#define Y4_GPIO_NUM       19  // Pin D2 (GPIO19)
#define Y3_GPIO_NUM       18  // Pin D1 (GPIO18)
#define Y2_GPIO_NUM        5  // Pin D0 (GPIO5)

#define VSYNC_GPIO_NUM    25  // Pin de sincronismo vertical (GPIO25)
#define HREF_GPIO_NUM     23  // Pin de referencia horizontal o sincronismo horizontal (GPIO23)
#define PCLK_GPIO_NUM     22  // GPIO22

#define LED_RED_STATUS 33  // LED Rojo integrado en la parte trasera (Active LOW)
#define LED_FLASH      4   // LED Flash frontal blanco de alto brillo (Active HIGH)

// Creamos una instancia de la clase WebServer, y le pasamos como parámetro el número de puerto (80)
// Creamos un objeto WebServer 
WebServer server(80);
// Credenciales de la red WiFi (las cuales son obtenidas del ESP32 maestro)
String wifi_ssid = "";  // SSID
String wifi_pass = "";  // Contraseña
// ===== Constantes relacionadas con la solicitud HTTP =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* Función que se ejecuta cada vez que el cliente accede a la ruta http://[IP]/stream. Es un middleware ya que es una 
función callback que se ejecuta entre la recepción de la petición y el envío de la respuesta */
void handle_stream() {
  // Creamos un objeto que envuelve el socket TCP de la conexión aceptada por el servidor. De esta manera, podemos
  // enviar continuamente respuestas HTTP sin terminarlas, a diferencia del método .send() que solamente envía respuestas de única vez
  // Además, nos permite construir manualmente la respuesta HTTP, sin pasar por las abstracciones de WebServer
  WiFiClient client = server.client();

  // Enviar cabeceras HTTP de MJPEG
  // Iniciamos la solicitud HTTP. Debemos establecer el encabezado Content-Type en "multipart/x-mixed-replace; boundary=<PART_BOUNDARY>" para transmitir
  // tramas JPEG continuamente al cliente, manteniendo siempre la conexión abierta.
  // "multipart" indica que el contenido de la respuesta HTTP está formado por múltiples partes independientes, mientras que "x-mixed-replace"
  // indica que esas partes se van enviando sucesivamente y que cada nueva parte reemplaza visualmente a la anterior
  // "boundary" es un separador que permite al cliente saber dónde termina una parte y comienza la siguiente. Puede tomar cualquier valor
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: ");
  client.print(_STREAM_CONTENT_TYPE);
  client.print("\r\n\r\n");

  // Encendemos el flash frontal de la cámara para iluminar (Desactivado por defecto para evitar caídas de tensión y reflejos)
  // digitalWrite(LED_FLASH, HIGH);
  // Bucle para escribir cada trama de la imagen mientras el cliente permanezca conectado
  while (client.connected()) {
    // Adquirimos una trama (frame) de la cámara
    camera_fb_t * fb = esp_camera_fb_get();  // Devuelve un puntero a un frame buffer (buffer de trama)
    if (!fb) {
      // Si falló la captura de la cámara, salimos del bucle
      break;
    }

    // Escribir cabecera de parte
    // Escribimos el delimitador de partes de la solicitud HTTP (boundary)
    client.print(_STREAM_BOUNDARY);
    // Para cada parte enviamos dos headers:
    // "Content-Type: image/jpeg" --> Tipo de imagen (JPEG)
    // "Content-Length: <fb->len>"  --> Longitud del buffer de trama (en bytes)
    char head[128];
    sprintf(head, _STREAM_PART, fb->len);
    client.print(head);

    // Escribir buffer JPEG
    // Enviamos la trama JPEG
    client.write(fb->buf, fb->len);
    // Retornamos el frame buffer de vuelta al driver para que pueda ser reutilizado
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
/* Función para obtener las credenciales de la red WiFi del ESP32 maestro */
void get_config_from_master() {
  unsigned long last_request = 0;  // Almacena el instante de tiempo de la última solicitud
  String rx_buf = "";  // Variable que almacena los datos recibidos del ESP32 maestro a través del UART1
  // Bucle que se ejecuta hasta recibir las credenciales
  while (wifi_ssid.length() == 0) {
    // Latido LED mientras espera config
    // Parpadeamos el LED mientras se espera la recepción de las credenciales
    digitalWrite(LED_RED_STATUS, !digitalRead(LED_RED_STATUS));
    delay(200);
    // Cada 2 segundos hacemos una solicitud al ESP32 maestro
    if (millis() - last_request > 2000) {
      Serial.println("GET_CONFIG");
      last_request = millis();  // Registramos el instante de tiempo actual
    }
    // Verificamos si hay caracteres disponibles en el UART0 del ESP32-CAM (conectado al UART1 del ESP32 maestro)
    while (Serial.available() > 0) {
      // Recibimos los caracteres de a uno
      char c = Serial.read();
      if (c == '\n') {
        // Si el caracter recibido es un salto de línea, significa que terminó de recibirse el mensaje
        // Eliminamos los espacios al comienzo y al final del buffer
        rx_buf.trim();
        // La cadena recibida tiene el formato "VALUE_CONFIG:{SSID},{PASSWORD}\n"
        if (rx_buf.startsWith("VALUE_CONFIG:")) {
          // Guardamos los datos recibidos creando una subcadena a partir del índice 13 de la trama recibida
          String data = rx_buf.substring(13);
          // Obtenemos la posición de la coma en la cadena anterior
          int comma = data.indexOf(',');
          if (comma > 0) {
            // Parseamos el SSID y la contraseña creando subcadenas limitadas por la coma
            wifi_ssid = data.substring(0, comma);
            wifi_pass = data.substring(comma + 1);
            wifi_ssid.trim();
            wifi_pass.trim();
          }
        }
        rx_buf = "";  // Vaciamos el buffer
      } else if (c != '\r') {
        // Si el caracter recibido no es \n (salto de línea) ni \r (retorno de carro) lo guardamos en el buffer
        rx_buf += c;
      }
    }
  }
  
  // Apagar LED de estado al salir
  digitalWrite(LED_RED_STATUS, HIGH);
}

void setup() {
  Serial.begin(115200);  // Iniciamos el puerto serie a 115200 baudios
  // Configuramos los pines de los LEDs indicadores como salidas
  pinMode(LED_RED_STATUS, OUTPUT);
  pinMode(LED_FLASH, OUTPUT);
  // Apagamos ambos LEDs
  digitalWrite(LED_RED_STATUS, HIGH); // El LED rojo se prende con un 0 y se apaga con un 1 (activo bajo)
  digitalWrite(LED_FLASH, LOW);       // El LED flash se prende con un 1 y se apaga con un 0 (activo alto)

  // 1. OBTENER CONFIG DE RED DESDE EL MASTER
  get_config_from_master();

  // 2. INICIALIZAR CÁMARA
  // Creamos una instancia de la estructura camera_config_t, la cual contiene los pines de la cámara y otros parámetros de configuración
  camera_config_t config = {
    .ledc_channel = LEDC_CHANNEL_0,  // Canal LEDC que se usa para generar la señal XCLK (señal PWM)
    .ledc_timer = LEDC_TIMER_0,  // Temporizador LEDC que se usa para generar la señal XCLK (señal PWM)
    .pin_d0 = Y2_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .xclk_freq_hz = 20000000, // Frecuencia de la señal XCLK en Hz (restaurado a 20MHz para asegurar compatibilidad con todos los sensores)
    .pixel_format = PIXFORMAT_JPEG,  // Elegimos el formato de imagen JPEG
    .jpeg_quality = 12  // Calidad de la salida JPEG (más bajo = mayor calidad). Calidad estable para evitar saturación de bus
  };
  
  // Configuración de resolución SVGA para un QR nítido en caso que el módulo tenga una PSRAM (RAM pseudo-estática)
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;  // Tamaño de la imagen de salida (SVGA, resolución 800x600)
    // Números de frame buffers (buffers de trama) que son asignados. Si hay más de uno, entonces cada trama será adquirida (velocidad doble)
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;  // Tamaño VGA (resolución 640x480)
    config.fb_count = 1;
  }
  // Inicializamos la cámara
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
  sensor_t * s = esp_camera_sensor_get();  // Obtenemos un puntero a la estructura de control del sensor de imagen
  if (s) {
    s->set_hmirror(s, 0); // Desactivar espejo
    s->set_vflip(s, 1);   // Voltear vertical
  }

  // 3. CONECTAR A WI-FI
  // Iniciamos la comunicación WiFi
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  int attempts = 0;  // Número de intentos de conexión al AP
  // El número máximo de intentos de conexión al AP es 30
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    // Mientras esperamos a que el ESP32 se conecte al AP, parpadeamos el LED rojo
    digitalWrite(LED_RED_STATUS, LOW);
    delay(100);
    digitalWrite(LED_RED_STATUS, HIGH);
    delay(150);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Notificar IP al Master por Serial
    // Si se estableció la conexión, enviamos el IP de la cámara al ESP32 maestro por puerto serie
    Serial.print("IP:");
    Serial.println(WiFi.localIP());
    
    // Iniciar servidor HTTP
    // Definimos la ruta "/stream" junto a una función que se ejecutará al recibir una petición
    server.on("/stream", handle_stream);
    // Iniciamos el servidor HTTP
    server.begin();
    
    // Parpadeo de éxito (3 parpadeos del LED rojo)
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
  // Manejamos continuamente las peticiones provenientes del cliente
  server.handleClient();
  
  // 1. Escuchar si el Master ordena reiniciar (ej. por cambio de red)
  while (Serial.available() > 0) {
    // Leemos el comando proveniente del ESP32 maestro hasta que haya un salto de línea
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();  // Eliminamos los espacios al comienzo y al final de la cadena
    if (cmd == "RESET") {
      // Si el comando recibido es "RESET", hacemos un reset de software
      ESP.restart();
    }
  }

  // 2. Reporte periódico de la IP al Master (cada 10 segundos)
  static unsigned long last_ip_report = 0;
  // Cada 10 segundos, el ESP32-CAM envía su IP al ESP32 maestro
  if (WiFi.status() == WL_CONNECTED && millis() - last_ip_report > 10000) {
    // Enviamos la IP por puerto serie
    Serial.print("IP:");
    Serial.println(WiFi.localIP());
    last_ip_report = millis();  // Registramos el instante de tiempo en que se envío la última IP
  }

  // 3. Watchdog de conexión Wi-Fi (si pierde conexión por más de 15s, reinicia)
  static unsigned long last_wifi_check = 0;
  if (millis() - last_wifi_check > 15000) {
    if (WiFi.status() != WL_CONNECTED) {
      ESP.restart();  // Reset de software
    }
    last_wifi_check = millis();
  }
  
  delay(1);  // Demora de 1 milisegundo
}