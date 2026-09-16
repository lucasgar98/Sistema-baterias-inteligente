#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <XPT2046_Touchscreen.h>
#include "ui.h" // Nuestro contrato visual

// Pines de comunicación con el Master (Conector CN1 libre de interferencias)
#define RXD2 22
#define TXD2 27

// Pines del chip táctil XPT2046 (CYD Versión R)
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Instancias de Hardware
TFT_eSPI tft = TFT_eSPI();
SPIClass mySpi(HSPI); // Bus SPI aislado para el táctil
XPT2046_Touchscreen ts(XPT2046_CS); // Modo Polling

// Buffer de memoria para el motor gráfico LVGL
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;
static lv_color_t buf[screenWidth * screenHeight / 10];

// Variables globales para los controladores en LVGL v8
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// ==========================================
// --- DRIVERS DE LVGL (API v8) ---
// ==========================================

// 1. Envío de píxeles a la pantalla (LVGL v8)
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
void accion_iniciar_carga() { Serial2.print("START_C\n"); }
void accion_iniciar_descarga() { Serial2.print("START_D\n"); }
void accion_parar_todo() { Serial2.print("STOP_ALL\n"); }

void accion_cambiar_perfil() {
  // handled by accion_guardar_limites() to avoid redundant requests
}

void accion_guardar_config() {
  String ssid = lv_textarea_get_text(ta_ssid);
  String pass = lv_textarea_get_text(ta_pass);
  String mqtt = lv_textarea_get_text(ta_mqtt);
  String muser = lv_textarea_get_text(ta_mqtt_user);
  String mpass = lv_textarea_get_text(ta_mqtt_pass);

  Serial2.print("SET_WIFI:");
  Serial2.print(ssid); Serial2.print(",");
  Serial2.print(pass); Serial2.print(",");
  Serial2.print(mqtt); Serial2.print(",");
  Serial2.print(muser); Serial2.print(",");
  Serial2.print(mpass);
  Serial2.print("\n");
}

void accion_guardar_limites() {
  String corte_c = lv_textarea_get_text(ta_corte_carga);
  String corte_d = lv_textarea_get_text(ta_corte_descarga);
  String corr_lim = lv_textarea_get_text(ta_corriente_lim);

  Serial2.print("SET_LIMITS:");
  Serial2.print(corte_c); Serial2.print(",");
  Serial2.print(corte_d); Serial2.print(",");
  Serial2.print(corr_lim);
  Serial2.print("\n");
}

// --- CALLBACKS INTERACTIVOS DE BATERÍA ---
bool is_updating_from_uart = false;

void on_profile_change(lv_event_t * e) {
  if (is_updating_from_uart) return;
  char buf[32];
  lv_dropdown_get_selected_str(dd_perfil, buf, sizeof(buf));
  String profile = String(buf);
  
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

void on_limit_ta_change(lv_event_t * e) {
  if (is_updating_from_uart) return;
  lv_dropdown_set_selected(dd_perfil, 4); // 4 es PERSONALIZADO
}

// --- CALLBACKS CONECTIVIDAD ON-DEMAND ---
void on_wifi_toggle(lv_event_t * e) {
  bool checked = lv_obj_has_state(sw_wifi, LV_STATE_CHECKED);
  if (checked) {
    Serial2.print("WIFI_ON\n");
    lv_label_set_text(label_wifi_status, LV_SYMBOL_REFRESH " ...");
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xfbbf24), 0);
  } else {
    Serial2.print("WIFI_OFF\n");
    lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);
    // Apagar MQTT también
    if (lv_obj_has_state(sw_mqtt, LV_STATE_CHECKED)) {
      lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
      lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
    }
  }
}

void on_mqtt_toggle(lv_event_t * e) {
  bool checked = lv_obj_has_state(sw_mqtt, LV_STATE_CHECKED);
  if (checked) {
    Serial2.print("MQTT_ON\n");
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_REFRESH " ...");
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xfbbf24), 0);
  } else {
    Serial2.print("MQTT_OFF\n");
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
  }
}

// ==========================================
// --- SETUP ---
// ==========================================
void setup() {
  // Inicializamos el puerto nativo por si querés ver debug en la PC
  Serial.begin(115200); 
  delay(1000);
  Serial.println("BOOT: Serial iniciado.");
  
  // INICIAMOS EL PUERTO SERIAL 2 EN LOS PINES DEL CN1
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); 
  Serial.println("BOOT: Serial2 iniciado.");
  
  // Pin de retroiluminación de la pantalla CYD
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  Serial.println("BOOT: Retroiluminacion encendida.");

  tft.begin();
  tft.invertDisplay(true); // Corrige la inversión de colores
  tft.setRotation(1);
  Serial.println("BOOT: TFT iniciado.");

  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.begin(mySpi);
  ts.setRotation(1);
  Serial.println("BOOT: Touch SPI iniciado.");
  
  lv_init();
  Serial.println("BOOT: LVGL iniciado.");

  // --- LVGL v8: Display ---
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);
  
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  Serial.println("BOOT: LVGL Display creado.");

  // --- LVGL v8: Input (Touchscreen) ---
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
  Serial.println("BOOT: LVGL Input creado.");

  ui_init(); 
  Serial.println("BOOT: UI inicializada.");
  
  // Registrar callbacks de los toggles después de ui_init()
  lv_obj_add_event_cb(sw_wifi, on_wifi_toggle, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(sw_mqtt, on_mqtt_toggle, LV_EVENT_VALUE_CHANGED, NULL);

  // Registrar callbacks interactivos para los parámetros de la batería
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
String rx_buffer = "";

void procesar_comando_recibido(String json_str) {
  json_str.trim();
  if (json_str.length() == 0) return;

  // Logueamos recepción por puerto Serial
  String debug_txt = "RX: " + json_str.substring(0, 15); 
  Serial.println(debug_txt);

  if (json_str.length() > 5) {
    
    // 1. Procesar pedido de Configuración de Red
    if (json_str.startsWith("VALUE_CONFIG:")) {
      String data = json_str.substring(13);
      int p1 = data.indexOf(',');
      int p2 = data.indexOf(',', p1 + 1);
      int p3 = data.indexOf(',', p2 + 1);
      int p4 = data.indexOf(',', p3 + 1);
      
      if (p1 > 0 && p2 > 0) {
         lv_textarea_set_text(ta_ssid, data.substring(0, p1).c_str());
         lv_textarea_set_text(ta_pass, data.substring(p1 + 1, p2).c_str());
         if (p3 > 0) {
           lv_textarea_set_text(ta_mqtt, data.substring(p2 + 1, p3).c_str());
           if (p4 > 0) {
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
    else if (json_str.startsWith("VALUE_LIMITS:")) {
      String data = json_str.substring(13);
      int p1 = data.indexOf(',');
      int p2 = data.indexOf(',', p1 + 1);
      int p3 = data.indexOf(',', p2 + 1);
      
      if (p1 > 0 && p2 > 0 && p3 > 0) {
         String perf = data.substring(0, p1);
         String corte_c = data.substring(p1 + 1, p2);
         String corte_d = data.substring(p2 + 1, p3);
         String corr_lim = data.substring(p3 + 1);
         
         int idx = 4; // default PERSONALIZADO
         if (perf == "LI-ION-2S") idx = 0;
         else if (perf == "LI-ION-3S") idx = 1;
         else if (perf == "PB-12V") idx = 2;
         else if (perf == "NiMH-7.2V") idx = 3;
         
         is_updating_from_uart = true;
         
         lv_dropdown_set_selected(dd_perfil, idx);
         lv_textarea_set_text(ta_corte_carga, corte_c.c_str());
         lv_textarea_set_text(ta_corte_descarga, corte_d.c_str());
         lv_textarea_set_text(ta_corriente_lim, corr_lim.c_str());
         
         is_updating_from_uart = false;
      }
    }
    
    // 2. Respuestas de estado de conectividad
    else if (json_str.startsWith("WIFI_STATUS:")) {
      String status = json_str.substring(12);
      if (status == "OK") {
        lv_label_set_text(label_wifi_status, LV_SYMBOL_OK " On");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x4ade80), 0);
        lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
      } else if (status == "FAIL") {
        lv_label_set_text(label_wifi_status, LV_SYMBOL_WARNING " Error");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0xef4444), 0);
        lv_obj_clear_state(sw_wifi, LV_STATE_CHECKED);
      } else { // OFF
        lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
        lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);
        lv_obj_clear_state(sw_wifi, LV_STATE_CHECKED);
      }
    }
    
    else if (json_str.startsWith("MQTT_STATUS:")) {
      String status = json_str.substring(12);
      if (status == "OK") {
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_OK " On");
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x4ade80), 0);
        lv_obj_add_state(sw_mqtt, LV_STATE_CHECKED);
      } else if (status == "NO_WIFI") {
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_WARNING " No WiFi");
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xf97316), 0);
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      } else if (status == "FAIL") {
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_WARNING " Error");
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0xef4444), 0);
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      } else { // OFF
        lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
        lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
        lv_obj_clear_state(sw_mqtt, LV_STATE_CHECKED);
      }
    }
    
    // 3. Procesar paquete de Telemetría (JSON)
    else if (json_str.startsWith("{")) {
      JsonDocument doc; 
      DeserializationError error = deserializeJson(doc, json_str);
      
      if (!error) {
        float v = doc["v"];
        int i = doc["i"];
        float t = doc["t"];
        const char* st = doc["st"];
        const char* qr = doc["qr"];
        float ah = doc["ah"] | 0.0;
        int ri = doc["ri"] | 0;
        
        char buf_str[64];
        
        sprintf(buf_str, "%.2f V", v);
        lv_label_set_text(label_voltaje, buf_str);
        
        sprintf(buf_str, "%d mA", i);
        lv_label_set_text(label_corriente, buf_str);
        
        sprintf(buf_str, "%.3f Ah | %d mO", ah, ri);
        lv_label_set_text(label_ah_ri, buf_str);
        
        sprintf(buf_str, "T: %.1f C", t);
        lv_label_set_text(label_temp, buf_str);
        
        lv_label_set_text(label_estado, st);
        
        // Pisamos con el QR real si el usuario no está editándolo
        if (!lv_obj_has_state(ta_main_qr, LV_STATE_FOCUSED)) {
            lv_textarea_set_text(ta_main_qr, qr);
        }
 
        // --- Lógica Dinámica de Botón STOP y Colores ---
        if (strcmp(st, "CARGANDO") == 0 || strcmp(st, "DESCARGANDO") == 0 || strncmp(st, "AUTO", 4) == 0) {
            lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x991b1b), 0);
            lv_obj_clear_state(btn_stop, LV_STATE_DISABLED);
            lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(st, "ERROR_TEMP") == 0) {
            lv_obj_clear_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x444444), 0);
            lv_obj_add_state(btn_stop, LV_STATE_DISABLED);
            lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
}

unsigned long last_heartbeat = 0;

void loop() {
  lv_timer_handler(); 
  lv_tick_inc(5);
  delay(5);
 
  if (millis() - last_heartbeat > 5000) {
    Serial.println("LOOP: ejecutandose (heartbeat)...");
    last_heartbeat = millis();
  }

  // Lectura no bloqueante del Serial2
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    if (c == '\n') {
      Serial.print("UART: recibida linea: ");
      Serial.println(rx_buffer);
      procesar_comando_recibido(rx_buffer);
      rx_buffer = "";
    } else if (c != '\r') {
      rx_buffer += c;
    }
  }
}