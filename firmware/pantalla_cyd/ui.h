#ifndef UI_H
#define UI_H

#include <lvgl.h>

// --- 1. REFERENCIAS A ELEMENTOS VISUALES ---
extern lv_obj_t * label_voltaje;
extern lv_obj_t * label_corriente;
extern lv_obj_t * label_temp; 
extern lv_obj_t * label_estado;
extern lv_obj_t * ta_main_qr;
extern lv_obj_t * label_ah_ri;
extern lv_obj_t * btn_stop;
extern lv_obj_t * overlay_error;
extern lv_obj_t * btn_scan_qr;  // Botón para iniciar escaneo de código QR

// --- 2. CONFIGURACIONES DE WIFI Y MQTT ---
extern lv_obj_t * ta_ssid;
extern lv_obj_t * ta_pass;
extern lv_obj_t * ta_mqtt;
extern lv_obj_t * ta_mqtt_user;
extern lv_obj_t * ta_mqtt_pass;
extern lv_obj_t * dd_perfil;
extern lv_obj_t * ta_corte_carga;
extern lv_obj_t * ta_corte_descarga;
extern lv_obj_t * ta_corriente_lim;

// --- 3. CONECTIVIDAD ON-DEMAND ---
extern lv_obj_t * sw_wifi;
extern lv_obj_t * sw_mqtt;
extern lv_obj_t * label_wifi_status;
extern lv_obj_t * label_mqtt_status;

// --- 4. FUNCIÓN DE INICIALIZACIÓN ---
void ui_init(void);

// --- 5. PROTOTIPOS DE ACCIONES ---
extern void accion_iniciar_carga();
extern void accion_iniciar_descarga();
extern void accion_parar_todo();
extern void accion_cambiar_perfil();
extern void accion_guardar_config();
extern void accion_guardar_limites();
extern void accion_escanear_qr();  // Callback para iniciar escaneo de código QR

#endif