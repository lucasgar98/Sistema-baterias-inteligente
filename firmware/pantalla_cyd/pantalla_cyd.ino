#include <SPI.h>
#include <TFT_eSPI.h>  // Biblioteca para manejar la comunicación con la pantalla TFT
#include <lvgl.h>  // Biblioteca para crear una interfaz de usuario en el display
#include <ArduinoJson.h>  // Biblioteca para trabajar con archivos JSON
#include <XPT2046_Touchscreen.h>  // Biblioteca para gestionar la comunicación con el sensor táctil de la pantalla (touchscreen)
#include "ui.h" // Biblioteca que implementa la interfaz de usuario en el display

// Pines de comunicación con el Master (Conector CN1 libre de interferencias)
#define RXD2 22  // Pin RXD del UART2 del ESP32-CYD (pin TXD del UART2 del ESP32 maestro)
#define TXD2 27  // Pin TXD del UART2 del ESP32-CYD (pin RXD del UART2 del ESP32 maestro)

// Pines del chip táctil XPT2046 (CYD Versión R)
#define XPT2046_MOSI 32  // Pin MOSI (GPIO32)
#define XPT2046_MISO 39  // Pin MISO (GPIO39)
#define XPT2046_CLK 25  // Pin CLK (GPIO25)
#define XPT2046_CS 33  // Pin CS (GPIO33)
// Pin del led trasero (backlight)
#define BACKLIGHT_PIN 21  // Pin del LED trasero (backlight) GPIO21 (GPIO22 es RXD2 de CN1)

// Instancias de Hardware
// Creamos una instancia de la clase TFT_eSPI
TFT_eSPI tft;  
// Creamos una instancia aparte de la clase SPI para manejar la comunicación con el chip táctil, ya que el objeto SPI que
// está declarado dentro de la biblioteca tiene acceso al bus VSPI, el cual se conecta a la pantalla TFT. En este caso,
// creamos un objeto que accede al bus HSPI, ya que el controlador XPT2046 está conectado a pines diferentes
SPIClass mySpi(HSPI); // Bus SPI aislado para el táctil
// Creamos una instancia de la clase XPT2046_Touchscreen, pasándole como argumento el pin CS del bus SPI al cual se conecta el XPT2046
XPT2046_Touchscreen ts(XPT2046_CS); // Modo Polling

// Buffer de memoria para el motor gráfico LVGL
static const uint16_t screenWidth  = 320;  // Ancho de la pantalla
static const uint16_t screenHeight = 240;  // Alto de la pantalla
// Declaramos un buffer que almacena los colores de cada píxel para un tamaño de pantalla de 1/10
static lv_color_t buf[screenWidth * screenHeight / 10];  

// Variables globales para los controladores en LVGL v8
// Variable que contiene buffers de gráficos internos llamados buffers de dibujo (draw buffers)
static lv_disp_draw_buf_t draw_buf;  
// Descriptor del driver del display. Esta variable contiene funciones callback para interactuar con el display y manipular
// elementos de dibujo asociados
static lv_disp_drv_t disp_drv;
// Descriptor del driver de un dispositivo de entrada (por ejemplo un touchpad)
static lv_indev_drv_t indev_drv;  

// ==========================================
// --- DRIVERS DE LVGL (API v8) ---
// ==========================================

// ======= FUNCIONES CALLBACK ASOCIADAS A LOS DESCRIPTORES DE LOS DRIVERS DE LVGL =======
// 1. Envío de píxeles a la pantalla (LVGL v8)
// Esta función copia una imagen renderizada a un área del display
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// 2. Lectura del panel táctil (LVGL v8)
// Función que lee un dispositivo de entrada (en este caso, un panel táctil)
void my_touchpad_read(lv_indev_drv_t * indev, lv_indev_data_t * data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    data->state = LV_INDEV_STATE_PR; 
    
    // Mapeo crudo -> píxeles
    data->point.x = map(p.x, 200, 3700, 0, screenWidth);
    data->point.y = map(p.y, 240, 3800, 0, screenHeight);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ==========================================
// --- ACCIONES DE LA INTERFAZ (Botones) ---
// ==========================================
// Ahora todas las órdenes salen por el Serial2 (Pines del CN1)
// Todas estas funciones envían comandos al ESP32 maestro a través del UART2 (que es el mismo número de UART que usa el otro ESP32 para recibir los comandos)
void accion_iniciar_carga() { Serial2.print("START_C\n"); }  // Envía el comando de inicio de carga 
void accion_iniciar_descarga() { Serial2.print("START_D\n"); }  // Envía el comando de inicio de descarga
void accion_parar_todo() { Serial2.print("STOP_ALL\n"); }  // Envía el comando de parada

// Acción para solicitar el escaneo de código QR a la ESP32-CAM a través del Master
void accion_escanear_qr() {
  Serial2.print("START_SCAN\n");
  lv_textarea_set_text(ta_main_qr, "Escaneando...");
  lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0x38bdf8), 0);  // Color celeste indicando escaneo activo
}

// Función que guarda la configuración de acuerdo a lo ingresado por el usuario en la pantalla
void accion_guardar_config() {
  // Obtenemos el texto de las áreas de texto correspondientes a las credenciales WiFi y MQTT
  String ssid = lv_textarea_get_text(ta_ssid);
  String pass = lv_textarea_get_text(ta_pass);
  String mqtt = lv_textarea_get_text(ta_mqtt);
  String muser = lv_textarea_get_text(ta_mqtt_user);
  String mpass = lv_textarea_get_text(ta_mqtt_pass);
  // Enviamos el comando "SET_WIFI" al ESP32 maestro a través del UART2 junto con las credenciales
  Serial2.print("SET_WIFI:");
  Serial2.print(ssid); Serial2.print(",");
  Serial2.print(pass); Serial2.print(",");
  Serial2.print(mqtt); Serial2.print(",");
  Serial2.print(muser); Serial2.print(",");
  Serial2.print(mpass);
  Serial2.print("\n");
}

void accion_guardar_limites() {
  // Obtenemos el texto de las áreas de texto correspondientes a los límites de corriente y tensión
  String corte_c = lv_textarea_get_text(ta_corte_carga);
  String corte_d = lv_textarea_get_text(ta_corte_descarga);
  String corr_lim = lv_textarea_get_text(ta_corriente_lim);
  // Enviamos el comando "SET_LIMITS" al ESP32 maestro a través del UART2 junto con los límites
  Serial2.print("SET_LIMITS:");
  Serial2.print(corte_c); Serial2.print(",");
  Serial2.print(corte_d); Serial2.print(",");
  Serial2.print(corr_lim);
  Serial2.print("\n");
}

// --- CALLBACKS INTERACTIVOS DE BATERÍA ---
bool is_updating_from_uart = false;  // Variable que indica si se está actualizando la pantalla con los datos recibidos por UART

// Función callback que se ejecuta cuando se selecciona un nuevo perfil de batería en la lista desplegable
void on_profile_change(lv_event_t * e) {
  if (is_updating_from_uart) return;  // Si se está actualizando la pantalla, finalizamos la ejecución de la función
  char buf[32];
  // Obtenemos la opción actual seleccionada como un string de C y la convertimos a string de Arduino
  lv_dropdown_get_selected_str(dd_perfil, buf, sizeof(buf));
  String profile = String(buf);
  // Evaluamos el perfil seleccionado y modificamos el campo de texto corriente a los límites de carga y descarga
  if (profile == "LI-ION-2S") {
    lv_textarea_set_text(ta_corte_carga, "8.4");
    lv_textarea_set_text(ta_corte_descarga, "6.0");
  } else if (profile == "LI-ION-3S") {
    lv_textarea_set_text(ta_corte_carga, "12.6");
    lv_textarea_set_text(ta_corte_descarga, "9.0");
  } else if (profile == "PB-12V") {
    lv_textarea_set_text(ta_corte_carga, "14.4");
    lv_textarea_set_text(ta_corte_descarga, "11.0");
  } else if (profile == "NiMH-7.2V") {
    lv_textarea_set_text(ta_corte_carga, "9.0");
    lv_textarea_set_text(ta_corte_descarga, "6.0");
  }
}
// Callback que se ejecuta cuando se cambian los límites de carga y descarga
void on_limit_ta_change(lv_event_t * e) {
  if (is_updating_from_uart) return;  // Si se está actualizando la pantalla, finalizamos la ejecución de la función
  // Ponemos en la lista desplegable el valor del índice 4, que corresponde a un perfil personalizado
  lv_dropdown_set_selected(dd_perfil, 4); // 4 es PERSONALIZADO
}

// --- CALLBACKS CONECTIVIDAD ON-DEMAND ---
// Callback que se ejecuta cuando cambia el estado de la comunicación WiFi
void on_wifi_toggle(lv_event_t * e) {
  // Leemos el estado del switch para activar/desactivar la comunicación WiFi
  bool checked = lv_obj_has_state(sw_wifi, LV_STATE_CHECKED);
  if (checked) {
    // Si el switch está en la posición correspondiente a WiFi activado, enviamos al ESP32 maestro
    // un comando para encender el WiFi
    Serial2.print("WIFI_ON\n");
    // Fijamos el texto de la etiqueta correspondiente al estado de la comunicación WiFi
    lv_label_set_text(label_wifi_status, LV_SYMBOL_REFRESH " ...");
    // Cambiamos el color del texto de la etiqueta a #FBBF24 (amarillo oscuro)
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xfbbf24), 0);
  } else {
    // Si el switch está en la posición correspondiente a WiFi desactivado, enviamos al ESP32 maestro
    // un comando para apagar el WiFi
    Serial2.print("WIFI_OFF\n");
    // Fijamos el texto de la etiqueta correspondiente al estado de la comunicación WiFi
    lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
    // Cambiamos el color del texto de la etiqueta a #888888 (gris)
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);
    // Apagar MQTT también
    if (lv_obj_has_state(sw_mqtt, LV_STATE_CHECKED)) {
      // Si el switch está en la posición correspondiente a MQTT activado, limpiamos el estado del switch
      lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      // Cambiamos el texto de la etiqueta correspondiente al estado de la comuniación MQTT
      lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
      // Cambiamos el color de texto de la etiqueta a #888888 (gris)
      lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
    }
  }
}
// Callback que se ejecuta cuando cambia el estado de la comunicación MQTT
void on_mqtt_toggle(lv_event_t * e) {
  // Leemos el estado del switch para activar/desactivar la comunicación MQTT
  bool checked = lv_obj_has_state(sw_mqtt, LV_STATE_CHECKED);
  if (checked) {
    // Si el switch está en la posición correspondiente a MQTT activado, enviamos al ESP32 maestro
    // un comando para encender el MQTT
    Serial2.print("MQTT_ON\n");
    // Cambiamos el texto de la etiqueta correspondiente al estado MQTT
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_REFRESH " ...");
    // Cambiamos el color del texto de la etiqueta a #FBBF24 (amarillo oscuro)
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xfbbf24), 0);
  } else {
    // Si el switch está en la posición correspondiente a MQTT desactivado, enviamos al ESP32 maestro
    // un comando para apagar el MQTT
    Serial2.print("MQTT_OFF\n");
    // Cambiamos el texto de la etiqueta correspondiente al estado MQTT
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
    // Cambiamos el color de texto de la etiqueta a #888888 (gris)
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
  }
}

// ==========================================
// --- SETUP ---
// ==========================================
void setup() {
  // Inicializamos el puerto nativo (UART0) por si querés ver debug en la PC
  Serial.begin(115200); 
  delay(1000);
  Serial.println("BOOT: Serial iniciado.");
  
  // INICIAMOS EL PUERTO SERIAL 2 EN LOS PINES DEL CN1
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); 
  Serial.println("BOOT: Serial2 iniciado.");
  
  // Pin de retroiluminación de la pantalla CYD (GPIO21)
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);  // Ponemos el LED en 1 para encenderlo
  Serial.println("BOOT: Retroiluminacion encendida.");
  // Inicializamos la pantalla TFT
  tft.begin();
  tft.invertDisplay(true); // Corrige la inversión de colores
  tft.setRotation(1);  // Fijamos la orientación de la imagen en la pantalla 
  Serial.println("BOOT: TFT iniciado.");
  // Inicializamos el bus SPI al cual se conecta el controlador XPT2046 (HSPI)
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  // Inicializamos la pantalla táctil
  ts.begin(mySpi);
  ts.setRotation(1);
  Serial.println("BOOT: Touch SPI iniciado.");
  // Inicializamos la biblioteca LVGL
  lv_init();
  Serial.println("BOOT: LVGL iniciado.");

  // --- LVGL v8: Display ---
  // Inicializamos el buffer de dibujo con un solo buffer de color. LVGL dibujará el contenido de la pantalla
  // en el buffer de dibujo y lo envía al display
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);
  // Inicializamos el driver del display y fijamos sus parámetros
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;  // Ancho de la pantalla
  disp_drv.ver_res = screenHeight;  // Alto de la pantalla
  disp_drv.flush_cb = my_disp_flush;  // Función callback para copiar el contenido de un buffer a un área específica del display
  disp_drv.draw_buf = &draw_buf;  // Puntero a la variable lv_disp_draw_buf_t inicializada (buffer de dibujo)
  // Registramos el driver del display inicializado
  lv_disp_drv_register(&disp_drv);
  Serial.println("BOOT: LVGL Display creado.");

  // --- LVGL v8: Input (Touchscreen) ---
  // Inicializamos el driver del dispositivo de entrada y fijamos sus parámtros
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;  // El touchpad es un dispositivo tipo puntero
  indev_drv.read_cb = my_touchpad_read;  // Función callback que será llamada periódicamente para reportar el estado del dispositivo de entrada
  // Registramos el driver del dispositivo de entrada
  lv_indev_drv_register(&indev_drv);
  Serial.println("BOOT: LVGL Input creado.");
  // Inicializamos la interfaz de usuario (la cual está definida en ui.cpp)
  ui_init(); 
  Serial.println("BOOT: UI inicializada.");
  
  // Registrar callbacks de los toggles después de ui_init()
  // Agregamos funciones callback para los switches que permiten activar/desactivar WiFi y MQTT. Dichas funciones se ejecutarán cuando
  // se dispare el evento "LV_EVENT_VALUE_CHANGED" (es decir, cuando cambia la posición de los interruptores en la interfaz)
  lv_obj_add_event_cb(sw_wifi, on_wifi_toggle, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(sw_mqtt, on_mqtt_toggle, LV_EVENT_VALUE_CHANGED, NULL);

  // Registrar callbacks interactivos para los parámetros de la batería
  // Agregamos funciones callback para la lista desplegable que permite seleccionar el perfil de batería y para los campos de texto que muestran
  // los límites de carga y descarga. Dichas funciones se ejecutarán cuando cambie el valor de los objetos en la interfaz
  lv_obj_add_event_cb(dd_perfil, on_profile_change, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(ta_corte_carga, on_limit_ta_change, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(ta_corte_descarga, on_limit_ta_change, LV_EVENT_VALUE_CHANGED, NULL);
  Serial.println("BOOT: Event callbacks registrados.");
  
  // Mensaje inicial en la zona del QR
  lv_textarea_set_text(ta_main_qr, "OFFLINE");
  Serial.println("BOOT: setup completado con exito.");
}

// ==========================================
// --- LOOP PRINCIPAL ---
// ==========================================
String rx_buffer = "";  // Buffer para guardar los caracteres recibidos del ESP32 maestro a través del UART2
// Función que procesa los comandos recibidos del ESP32 maestro, así como también los paquetes de telemetría en formato JSON
void procesar_comando_recibido(String json_str) {
  json_str.trim();  // Eliminamos los espacios al comienzo y al final de la cadena
  if (json_str.length() == 0) return;  // Si no se recibió nada, finalizamos la ejecución de la función

  // Logueamos recepción por puerto Serial
  String debug_txt = "RX: " + json_str.substring(0, 15); // Creamos una subcadena que va del índice 0 al 14 (15 caracteres)
  Serial.println(debug_txt);
  // El comando recibido debe tener como mínimo 6 caracteres
  if (json_str.length() > 5) {
    
    // 1. Procesar pedido de Configuración de Red
    // El valor recibido tiene el siguiente formato: "VALUE_CONFIG:{WIFI_SSID},{WIFI_PASS},{MQTT_BROKER},{MQTT_USER},{MQTT_PASS}\n"
    if (json_str.startsWith("VALUE_CONFIG:")) {
      // Recibimos las credenciales de WiFi y MQTT y las guardamos en una cadena
      String data = json_str.substring(13);
      // Buscamos el índice donde se encuentran las comas (es decir, los separadores de los valores recibidos)
      int p1 = data.indexOf(',');  // Índice de la coma 1 (separa WIFI_SSID de WIFI_PASS)
      int p2 = data.indexOf(',', p1 + 1);  // Índice de la coma 2 (separa WIFI_PASS de MQTT_BROKER)
      int p3 = data.indexOf(',', p2 + 1);  // Índice de la coma 3 (separa MQTT_BROKER de MQTT_USER)
      int p4 = data.indexOf(',', p3 + 1);  // Índice de la coma 4 (separa MQTT_USER de MQTT_PASS)
      
      if (p1 > 0 && p2 > 0) {
        // Si se recibieron las credenciales WiFi, actualizamos los campos de texto correspondientes en la interfaz
        // Usamos los índices de las comas para extraer las credenciales de la cadena recibida
         lv_textarea_set_text(ta_ssid, data.substring(0, p1).c_str());  // SSID de la red WiFi
         lv_textarea_set_text(ta_pass, data.substring(p1 + 1, p2).c_str());  // Contraseña WiFi
         if (p3 > 0) {
          // Si se recibió la IP del broker MQTT, actualizamos el campo de texto correspondiente
           lv_textarea_set_text(ta_mqtt, data.substring(p2 + 1, p3).c_str());
           if (p4 > 0) {
              // Si se recibieron el usuario y contraseña MQTT, actualizamos las áreas de texto correspondientes en la interfaz
              lv_textarea_set_text(ta_mqtt_user, data.substring(p3 + 1, p4).c_str());
              lv_textarea_set_text(ta_mqtt_pass, data.substring(p4 + 1).c_str());
           } else {
              lv_textarea_set_text(ta_mqtt_user, data.substring(p3 + 1).c_str());
           }
         } else {
           lv_textarea_set_text(ta_mqtt, data.substring(p2 + 1).c_str());
         }
      }
    }
    
    // 1.5. Procesar pedido de Límites de Batería
    // El valor recibido tiene el siguiente formato: "VALUE_LIMITS:{PERFIL_ACTUAL},{VOLTAJE_CORTE_CARGA},{VOLTAJE_CORTE_DESCARGA},{CORRIENTE_OBJETIVO}\n"
    else if (json_str.startsWith("VALUE_LIMITS:")) {
      // Recibimos los valores y los guardamos en una cadena
      String data = json_str.substring(13);
      // Buscamos el índice donde se encuentran las comas (es decir, los separadores de los valores recibidos)
      int p1 = data.indexOf(',');  // Índice de la coma 1 (entre PERFIL_ACTUAL y VOLTAJE_CORTE_CARGA)
      int p2 = data.indexOf(',', p1 + 1);  // Índice de la coma 2 (entre VOLTAJE_CORTE_CARGA y VOLTAJE_CORTE_DESCARGA)
      int p3 = data.indexOf(',', p2 + 1);  // Índice de la coma 3 (entre VOLTAJE_CORTE_DESCARGA y CORRIENTE_OBJETIVO)
      
      if (p1 > 0 && p2 > 0 && p3 > 0) {
        // Si se recibieron los valores, usamos los índices de las comas para extraer los valores recibidos
         String perf = data.substring(0, p1);  // Perfil de la batería
         String corte_c = data.substring(p1 + 1, p2);  // Tensión de corte de carga
         String corte_d = data.substring(p2 + 1, p3);  // Tensión de corte de descarga
         String corr_lim = data.substring(p3 + 1);  // Límite de corriente
         // Definimos el índice de la lista desplegable en función del tipo de batería
         int idx = 4; // default PERSONALIZADO
         if (perf == "LI-ION-2S") idx = 0;  // Batería de Li-ON 2S (índice 0)
         else if (perf == "LI-ION-3S") idx = 1;  // Li-On 3S (índice 1)
         else if (perf == "PB-12V") idx = 2;  // Plomo-ácido de 12 V (índice 2)
         else if (perf == "NiMH-7.2V") idx = 3;  // Níquel-MH de 7.2 V (índice 3)
         
         is_updating_from_uart = true;
         // Actualizamos la lista desplegable con el nuevo perfil de la batería
         lv_dropdown_set_selected(dd_perfil, idx);
         // Actualizamos los campos de texto correspondientes a los límites de corriente y tensión
         lv_textarea_set_text(ta_corte_carga, corte_c.c_str());
         lv_textarea_set_text(ta_corte_descarga, corte_d.c_str());
         lv_textarea_set_text(ta_corriente_lim, corr_lim.c_str());
         
         is_updating_from_uart = false;
      }
    }
    
    // 2. Respuestas de estado de conectividad
    // Los tres valores recibidos posibles son: "WIFI_STATUS:OK", "WIFI_STATUS:FAIL" y "WIFI_STATUS_OFF"
    else if (json_str.startsWith("WIFI_STATUS:")) {
      // Guardamos el estado de conectividad recibido
      String status = json_str.substring(12);
      if (status == "OK") {
        // Fijamos el texto de la etiqueta correspondiente al estado del WiFi (le asignamos el texto "On")
        lv_label_set_text(label_wifi_status, LV_SYMBOL_OK " On");
        // Fijamos el color del texto de la etiqueta en #4ADE80 (verde)
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x4ade80), 0);
        // Cambiamos el estado del interruptor WiFi de la interfaz a "LV_STATE_CHECKED" (encendido)
        lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
      } else if (status == "FAIL") {
        // Fijamos el texto de la etiqueta correspondiente al estado del WiFi (le asignamos el texto "Error")
        lv_label_set_text(label_wifi_status, LV_SYMBOL_WARNING " Error");
        // Fijamos el color del texto de la etiqueta en #EF4444 (rojo)
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xef4444), 0);
        // Cambiamos el estado del interruptor WiFi de la interfaz a apagado
        lv_obj_clear_state(sw_wifi, LV_STATE_CHECKED);
      } else { // OFF
        // Fijamos el texto de la etiqueta correspondiente al estado del WiFi (le asignamos el texto "Off")
        lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
        // Fijamos el color del texto de la etiqueta en #888888 (gris)
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);
        // Cambiamos el estado del interruptor WiFi de la interfaz a apagado
        lv_obj_clear_state(sw_wifi, LV_STATE_CHECKED);
      }
    }
    // Los cuatro valores recibidos posibles son: "MQTT_STATUS:NO_WIFI", "MQTT_STATUS:OK", "MQTT_STATUS:FAIL", "MQTT_STATUS:OFF"
    else if (json_str.startsWith("MQTT_STATUS:")) {
      // Guardamos el estado de conectividad recibido
      String status = json_str.substring(12);
      if (status == "OK") {
        // Fijamos el texto de la etiqueta correspondiente al estado del MQTT (le asignamos el texto "On")
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_OK " On");
        // Cambiamos el color del texto de la etiqueta a #4ADE80 (verde)
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x4ade80), 0);
        // Cambiamos el estado del interruptor MQTT de la interfaz a "LV_STATE_CHECKED" (encendido)
        lv_obj_add_state(sw_mqtt, LV_STATE_CHECKED);
      } else if (status == "NO_WIFI") {
        // Fijamos el texto de la etiqueta correspondiente al estado del MQTT (le asignamos el texto "No WiFi")
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_WARNING " No WiFi");
        // Cambiamos el color del texto de la etiqueta a #F97316 (naranja)
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xf97316), 0);
        // Cambiamos el estado del interruptor MQTT de la interfaz a apagado
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      } else if (status == "FAIL") {
        // Fijamos el texto de la etiqueta correspondiente al estado del MQTT (le asignamos el texto "Error")
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_WARNING " Error");
        // Cambiamos el color del texto de la etiqueta a #EF4444 (rojo)
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xef4444), 0);
        // Cambiamos el estado del interruptor MQTT de la interfaz a apagado
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      } else { // OFF
        // Fijamos el texto de la etiqueta correspondiente al estado del MQTT (le asignamos el texto "Off")
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
        // Fijamos el color del texto de la etiqueta en #888888 (gris)
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
        // Cambiamos el estado del interruptor WiFi de la interfaz a apagado
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      }
    }

    // 2.5. Respuestas de estado del escáner QR provenientes del Master
    else if (json_str.startsWith("SCAN_STATUS:")) {
      String status = json_str.substring(12);
      status.trim();
      if (status == "TIMEOUT") {
        lv_textarea_set_text(ta_main_qr, "TIMEOUT");
        lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0xef4444), 0);  // Rojo si hubo timeout
      } else if (status == "SCANNING") {
        lv_textarea_set_text(ta_main_qr, "Escaneando...");
        lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0x38bdf8), 0);  // Celeste durante escaneo
      }
    }
    
    // 3. Procesar paquete de Telemetría (JSON)
    else if (json_str.startsWith("{")) {
      // Debemos des-serializar el paquete JSON recibido
      JsonDocument doc;  // Variable para almacenar el documento des-serializado
      // Des-serializamos el JSON recibido, el cual se guardará en un documento tipo JsonDocument que contendrá los miembros de paquete JSON en
      // forma de pares clave-valor, similar a los diccionarios de Python 
      DeserializationError error = deserializeJson(doc, json_str); 
      
      if (!error) {
        // Si no hubo error, extraemos los valores recibidos accediendo a cada una de las claves del documento, como si fuera
        // un diccionario de Python
        float v = doc["v"];  // Tensión de la batería
        int i = doc["i"];  // Corriente de la batería
        float t = doc["t"];  // Temperatura
        const char* st = doc["st"];  // Estado del sistema
        const char* qr = doc["qr"];  // QR de la batería
        float ah = doc["ah"] | 0.0;  // Capacidad
        int ri = doc["ri"] | 0;  // Resistencia interna
        
        char buf_str[64];
        // Actualizamos la etiqueta de tensión
        sprintf(buf_str, "%.2f V", v);
        lv_label_set_text(label_voltaje, buf_str);
        // Actualizamos la etiqueta de corriente
        sprintf(buf_str, "%d mA", i);
        lv_label_set_text(label_corriente, buf_str);
        // Actualizamos la etiqueta de capacidad
        sprintf(buf_str, "%.3f Ah | %d mO", ah, ri);
        lv_label_set_text(label_ah_ri, buf_str);
        // Actualizamos la etiqueta de temperatura
        sprintf(buf_str, "T: %.1f C", t);
        lv_label_set_text(label_temp, buf_str);
        // Actualizamos la etiqueta de estado
        lv_label_set_text(label_estado, st);
        
        // Pisamos con el QR real si el usuario no está editándolo
        // Si el usuario estuviera editando el campo de texto, estaría en el estado "LV_STATE_FOCUSED"
        if (!lv_obj_has_state(ta_main_qr, LV_STATE_FOCUSED)) {
            lv_textarea_set_text(ta_main_qr, qr);
            lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0xfbbf24), 0);  // Restaurar color dorado
        }
 
        // --- Lógica Dinámica de Botón STOP y Colores ---
        if (strcmp(st, "CARGANDO") == 0 || strcmp(st, "DESCARGANDO") == 0 || strncmp(st, "AUTO", 4) == 0) {
            // Fijamos el color de fondo del botón de parada en #991B1B (rojo oscuro)
            lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x991b1b), 0);
            // Habilitamos el botón, es decir, lo ponemos en un estado opuesto a "LV_STATE_DISABLED" (deshabilitado)
            lv_obj_clear_state(btn_stop, LV_STATE_DISABLED);
            // Ocultamos el objeto de panel de alerta crítica
            lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(st, "ERROR_TEMP") == 0) {
            // Mostramos el objeto de panel de alerta crítica, poniéndolo en un estado opuesto a "LV_OBJ_FLAG_HIDDEN"
            lv_obj_clear_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Fijamos el color del botón de parada en #444444 (gris oscuro)
            lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x444444), 0);
            // Deshabilitamos el botón de parada
            lv_obj_add_state(btn_stop, LV_STATE_DISABLED);
            // Ocultamos el panel de alerta crítica
            lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
}

unsigned long last_print = 0;  // Instante de tiempo en el que se imprimió por última vez un mensaje en el monitor serie

void loop() {
  // Función que maneja todas las tareas asociadas a LVGL, como refrescar el display, leer dispositivos de entrada, despedir
  // eventos basados en entrada de usuario, correr animaciones y correr temporizadores creados por el usuario
  lv_timer_handler();
  // Función que le dice a LVGL cuánto tiempo transcurrió desde la última vez que se actualizó su reloj interno (en este caso, 5 milisegundos)
  // Es conveniente registrar un contador de tiempo real mediante lv_tick_set_cb() en el setup, para que LVGL tenga una referencia temporal correcta
  lv_tick_inc(5);
  // El timer_handler se ejecuta cada 5 milisegundos
  delay(5);
  // Imprimimos cada 5 segundos un mensaje en el monitor serie que indica que se está ejecutando el bucle
  if (millis() - last_print > 5000) {
    Serial.println("LOOP: ejecutandose (heartbeat)...");
    last_print = millis();
  }

  // Lectura no bloqueante del Serial2
  while (Serial2.available() > 0) {
    // Leemos los caracteres de a uno
    char c = Serial2.read();
    if (c == '\n') {
      // Si el caracter recibido es \n, procesamos el comando recibido
      Serial.print("UART: recibida linea: ");
      Serial.println(rx_buffer);
      procesar_comando_recibido(rx_buffer);
      rx_buffer = "";
    } else if (c != '\r') {
      // Si el caracter recibido no es \n (salto de línea) ni \r (retorno de carro), lo guardamos en un buffer
      rx_buffer += c;
    }
  }
}