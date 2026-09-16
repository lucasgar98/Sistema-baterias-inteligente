#include "ui.h"
#include <Arduino.h>

// --- VARIABLES GLOBALES (Labels y TextAreas) ---
lv_obj_t * label_voltaje;
lv_obj_t * label_corriente;
lv_obj_t * label_temp;
lv_obj_t * label_estado;
lv_obj_t * ta_main_qr;
lv_obj_t * label_ah_ri;

lv_obj_t * ta_ssid;
lv_obj_t * ta_pass;
lv_obj_t * ta_mqtt;
lv_obj_t * ta_mqtt_user;
lv_obj_t * ta_mqtt_pass;
lv_obj_t * dd_perfil;
lv_obj_t * ta_corte_carga;
lv_obj_t * ta_corte_descarga;
lv_obj_t * ta_corriente_lim;

lv_obj_t * btn_stop; // Global para control dinámico
lv_obj_t * overlay_error; // Panel de alerta crítica

// --- CONECTIVIDAD ON-DEMAND ---
lv_obj_t * sw_wifi;
lv_obj_t * sw_mqtt;
lv_obj_t * label_wifi_status;
lv_obj_t * label_mqtt_status;

// --- ESTILOS ---
static lv_style_t style_card;
static lv_style_t style_btn_main;

// --- FUNCIONES AUXILIARES ---
static void create_metric_card(lv_obj_t * parent, lv_obj_t ** label, const char * title, lv_color_t color, int x, int y, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x9ca3af), 0); // Gris claro
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, -5, -5);

    *label = lv_label_create(card);
    lv_obj_set_style_text_font(*label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(*label, color, 0);
    lv_obj_align(*label, LV_ALIGN_BOTTOM_LEFT, -5, 5);
    lv_label_set_text(*label, "--");
}

// --- CALLBACKS ---
static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        // Scroll automático
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    }
    if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_qr_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_READY) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        
        // Enviar por UART al Master
        const char * qr_text = lv_textarea_get_text(ta);
        Serial2.print("SET_QR:");
        Serial2.print(qr_text);
        Serial2.print("\n");
    }
}

void ui_init(void) {
    // 1. INICIALIZAR ESTILOS
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1e1e1e));
    lv_style_set_border_width(&style_card, 0);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_shadow_width(&style_card, 10);
    lv_style_set_shadow_ofs_y(&style_card, 5);
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);

    lv_style_init(&style_btn_main);
    lv_style_set_radius(&style_btn_main, 8);
    lv_style_set_text_font(&style_btn_main, &lv_font_montserrat_12);

    // 2. CREAR TILEVIEW (Navegación por gestos)
    lv_obj_t * tv = lv_tileview_create(lv_scr_act());
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x121212), 0);
    
    lv_obj_t * tile_main    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t * tile_cfg_bat = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t * tile_cfg_net = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);

    // --- PANTALLA PRINCIPAL ---
    // Cards de datos
    create_metric_card(tile_main, &label_voltaje, "VOLTAJE", lv_color_hex(0x60a5fa), 10, 10, 145, 50);
    create_metric_card(tile_main, &label_corriente, "CORRIENTE", lv_color_hex(0xa78bfa), 165, 10, 145, 50);
    create_metric_card(tile_main, &label_ah_ri, "CAPACIDAD / RI", lv_color_hex(0x4ade80), 10, 65, 300, 45);
    
    // Card Temperatura
    lv_obj_t * card_temp = lv_obj_create(tile_main);
    lv_obj_set_size(card_temp, 145, 45);
    lv_obj_add_style(card_temp, &style_card, 0);
    lv_obj_align(card_temp, LV_ALIGN_TOP_LEFT, 10, 115);
    lv_obj_clear_flag(card_temp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_temp_title = lv_label_create(card_temp);
    lv_label_set_text(lbl_temp_title, "TEMPERATURA");
    lv_obj_set_style_text_font(lbl_temp_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_temp_title, lv_color_hex(0x9ca3af), 0);
    lv_obj_align(lbl_temp_title, LV_ALIGN_TOP_LEFT, -5, -5);

    label_temp = lv_label_create(card_temp);
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_temp, LV_ALIGN_BOTTOM_LEFT, -5, 5);
    lv_label_set_text(label_temp, "-- C");

    // Card QR (Text Area para poder escribirlo manualmente)
    lv_obj_t * card_qr = lv_obj_create(tile_main);
    lv_obj_set_size(card_qr, 145, 45);
    lv_obj_add_style(card_qr, &style_card, 0);
    lv_obj_align(card_qr, LV_ALIGN_TOP_LEFT, 165, 115);
    lv_obj_clear_flag(card_qr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_qr_title = lv_label_create(card_qr);
    lv_label_set_text(lbl_qr_title, "QR BATERÍA");
    lv_obj_set_style_text_font(lbl_qr_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_qr_title, lv_color_hex(0x9ca3af), 0);
    lv_obj_align(lbl_qr_title, LV_ALIGN_TOP_LEFT, -5, -5);

    ta_main_qr = lv_textarea_create(card_qr);
    lv_obj_set_size(ta_main_qr, 135, 25);
    lv_obj_align(ta_main_qr, LV_ALIGN_BOTTOM_LEFT, -10, 5);
    lv_textarea_set_placeholder_text(ta_main_qr, "Ingresar QR");
    lv_textarea_set_one_line(ta_main_qr, true);
    lv_obj_set_style_bg_opa(ta_main_qr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ta_main_qr, 0, 0);
    lv_obj_set_style_pad_all(ta_main_qr, 0, 0);
    lv_obj_set_style_text_font(ta_main_qr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0xfbbf24), 0);

    // Card Estado Actual
    lv_obj_t * card_estado = lv_obj_create(tile_main);
    lv_obj_set_size(card_estado, 300, 35);
    lv_obj_add_style(card_estado, &style_card, 0);
    lv_obj_align(card_estado, LV_ALIGN_TOP_LEFT, 10, 165);
    lv_obj_clear_flag(card_estado, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_estado_title = lv_label_create(card_estado);
    lv_label_set_text(lbl_estado_title, "ESTADO ACTUAL:");
    lv_obj_set_style_text_font(lbl_estado_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_estado_title, lv_color_hex(0x9ca3af), 0);
    lv_obj_align(lbl_estado_title, LV_ALIGN_LEFT_MID, -5, 0);

    label_estado = lv_label_create(card_estado);
    lv_obj_set_style_text_font(label_estado, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_estado, lv_color_hex(0xfbbf24), 0);
    lv_obj_align(label_estado, LV_ALIGN_RIGHT_MID, 5, 0);
    lv_label_set_text(label_estado, "REPOSO");

    // Botones (Abajo)
    lv_obj_t * btn_test = lv_btn_create(tile_main);
    lv_obj_set_size(btn_test, 55, 30);
    lv_obj_align(btn_test, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_set_style_bg_color(btn_test, lv_color_hex(0x2563eb), 0);
    lv_obj_add_event_cb(btn_test, [](lv_event_t * e){ Serial2.print("START_AUTO\n"); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lt = lv_label_create(btn_test); lv_label_set_text(lt, "AUTO"); lv_obj_center(lt);

    lv_obj_t * btn_c = lv_btn_create(tile_main);
    lv_obj_set_size(btn_c, 55, 30);
    lv_obj_align(btn_c, LV_ALIGN_BOTTOM_LEFT, 70, -5);
    lv_obj_set_style_bg_color(btn_c, lv_color_hex(0x16a34a), 0);
    lv_obj_add_event_cb(btn_c, [](lv_event_t * e){ accion_iniciar_carga(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lc = lv_label_create(btn_c); lv_label_set_text(lc, "CARGA"); lv_obj_center(lc);

    lv_obj_t * btn_d = lv_btn_create(tile_main);
    lv_obj_set_size(btn_d, 55, 30);
    lv_obj_align(btn_d, LV_ALIGN_BOTTOM_LEFT, 130, -5);
    lv_obj_set_style_bg_color(btn_d, lv_color_hex(0xd97706), 0);
    lv_obj_add_event_cb(btn_d, [](lv_event_t * e){ accion_iniciar_descarga(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * ld = lv_label_create(btn_d); lv_label_set_text(ld, "DESC"); lv_obj_center(ld);

    btn_stop = lv_btn_create(tile_main);
    lv_obj_set_size(btn_stop, 110, 30);
    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(btn_stop, [](lv_event_t * e){ accion_parar_todo(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * ls = lv_label_create(btn_stop); lv_label_set_text(ls, "PARAR"); lv_obj_center(ls);



    // --- TECLADO VIRTUAL GLOBAL ---
    // Lo creamos en el screen activo para que flote encima de cualquier pestaña
    lv_obj_t * kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(kb, 320, 110);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // --- PANTALLA DE CONFIGURACIÓN DE BATERÍA ---
    lv_obj_t * cont_cfg_bat = lv_obj_create(tile_cfg_bat);
    lv_obj_set_size(cont_cfg_bat, 320, 240);
    lv_obj_set_scroll_dir(cont_cfg_bat, LV_DIR_VER);
    lv_obj_set_style_bg_opa(cont_cfg_bat, 0, 0);
    lv_obj_set_style_border_width(cont_cfg_bat, 0, 0);

    lv_obj_t * title_cfg_bat = lv_label_create(cont_cfg_bat);
    lv_label_set_text(title_cfg_bat, "PARÁMETROS DE BATERÍA");
    lv_obj_set_style_text_font(title_cfg_bat, &lv_font_montserrat_14, 0);
    lv_obj_align(title_cfg_bat, LV_ALIGN_TOP_MID, 0, 0);

    dd_perfil = lv_dropdown_create(cont_cfg_bat);
    lv_obj_set_size(dd_perfil, 180, 35);
    lv_obj_align(dd_perfil, LV_ALIGN_TOP_MID, 0, 30);
    lv_dropdown_set_options(dd_perfil, "LI-ION-2S\nLI-ION-3S\nPB-12V\nNiMH-7.2V\nPERSONALIZADO");

    lv_obj_t * lbl_corte_carga = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corte_carga, "Corte Carga (V):");
    lv_obj_set_style_text_font(lbl_corte_carga, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_corte_carga, LV_ALIGN_TOP_LEFT, 10, 80);

    ta_corte_carga = lv_textarea_create(cont_cfg_bat); 
    lv_obj_set_size(ta_corte_carga, 120, 35); 
    lv_obj_align(ta_corte_carga, LV_ALIGN_TOP_LEFT, 150, 70);
    lv_textarea_set_placeholder_text(ta_corte_carga, "Carga V");
    lv_textarea_set_one_line(ta_corte_carga, true);

    lv_obj_t * lbl_corte_descarga = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corte_descarga, "Corte Descarga (V):");
    lv_obj_set_style_text_font(lbl_corte_descarga, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_corte_descarga, LV_ALIGN_TOP_LEFT, 10, 125);

    ta_corte_descarga = lv_textarea_create(cont_cfg_bat); 
    lv_obj_set_size(ta_corte_descarga, 120, 35); 
    lv_obj_align(ta_corte_descarga, LV_ALIGN_TOP_LEFT, 150, 115);
    lv_textarea_set_placeholder_text(ta_corte_descarga, "Descarga V");
    lv_textarea_set_one_line(ta_corte_descarga, true);

    lv_obj_t * lbl_corriente_lim = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corriente_lim, "Corriente Lim (mA):");
    lv_obj_set_style_text_font(lbl_corriente_lim, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_corriente_lim, LV_ALIGN_TOP_LEFT, 10, 170);

    ta_corriente_lim = lv_textarea_create(cont_cfg_bat); 
    lv_obj_set_size(ta_corriente_lim, 120, 35); 
    lv_obj_align(ta_corriente_lim, LV_ALIGN_TOP_LEFT, 150, 160);
    lv_textarea_set_placeholder_text(ta_corriente_lim, "Corriente mA");
    lv_textarea_set_one_line(ta_corriente_lim, true);

    lv_obj_t * btn_save_bat = lv_btn_create(cont_cfg_bat);
    lv_obj_set_size(btn_save_bat, 160, 35);
    lv_obj_align(btn_save_bat, LV_ALIGN_TOP_MID, 0, 210);
    lv_obj_set_style_bg_color(btn_save_bat, lv_color_hex(0x9333ea), 0);
    lv_obj_t * lbl_save_bat = lv_label_create(btn_save_bat); 
    lv_label_set_text(lbl_save_bat, "GUARDAR PERFIL"); 
    lv_obj_center(lbl_save_bat);
    lv_obj_add_event_cb(btn_save_bat, [](lv_event_t * e){ 
        accion_cambiar_perfil(); 
        accion_guardar_limites(); 
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(ta_corte_carga, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_corte_descarga, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_corriente_lim, ta_event_cb, LV_EVENT_ALL, kb);

    // --- PANTALLA DE CONFIGURACIÓN DE RED ---
    lv_obj_t * cont_cfg_net = lv_obj_create(tile_cfg_net);
    lv_obj_set_size(cont_cfg_net, 320, 240);
    lv_obj_set_scroll_dir(cont_cfg_net, LV_DIR_VER);
    lv_obj_set_style_bg_opa(cont_cfg_net, 0, 0);
    lv_obj_set_style_border_width(cont_cfg_net, 0, 0);

    lv_obj_t * title_cfg_net = lv_label_create(cont_cfg_net);
    lv_label_set_text(title_cfg_net, "CONEXIÓN WI-FI / MQTT");
    lv_obj_set_style_text_font(title_cfg_net, &lv_font_montserrat_14, 0);
    lv_obj_align(title_cfg_net, LV_ALIGN_TOP_MID, 0, 0);

    ta_ssid = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_ssid, 140, 35); 
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_textarea_set_placeholder_text(ta_ssid, "SSID");
    lv_textarea_set_one_line(ta_ssid, true);
    
    ta_pass = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_pass, 140, 35); 
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 150, 30);
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_one_line(ta_pass, true);

    ta_mqtt = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_mqtt, 290, 35); 
    lv_obj_align(ta_mqtt, LV_ALIGN_TOP_LEFT, 0, 75);
    lv_textarea_set_placeholder_text(ta_mqtt, "Broker IP");
    lv_textarea_set_one_line(ta_mqtt, true);

    ta_mqtt_user = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_mqtt_user, 140, 35); 
    lv_obj_align(ta_mqtt_user, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_textarea_set_placeholder_text(ta_mqtt_user, "MQTT User");
    lv_textarea_set_one_line(ta_mqtt_user, true);
    
    ta_mqtt_pass = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_mqtt_pass, 140, 35); 
    lv_obj_align(ta_mqtt_pass, LV_ALIGN_TOP_LEFT, 150, 120);
    lv_textarea_set_placeholder_text(ta_mqtt_pass, "MQTT Pass");
    lv_textarea_set_password_mode(ta_mqtt_pass, true);
    lv_textarea_set_one_line(ta_mqtt_pass, true);

    // Wi-Fi
    lv_obj_t * lbl_wifi = lv_label_create(cont_cfg_net);
    lv_label_set_text(lbl_wifi, "Wi-Fi");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_LEFT, 0, 168);

    sw_wifi = lv_switch_create(cont_cfg_net);
    lv_obj_set_size(sw_wifi, 40, 20);
    lv_obj_align(sw_wifi, LV_ALIGN_TOP_LEFT, 50, 165);
    lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x16a34a), LV_PART_INDICATOR | LV_STATE_CHECKED);

    label_wifi_status = lv_label_create(cont_cfg_net);
    lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(label_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_align(label_wifi_status, LV_ALIGN_TOP_LEFT, 100, 168);

    // MQTT
    lv_obj_t * lbl_mqtt = lv_label_create(cont_cfg_net);
    lv_label_set_text(lbl_mqtt, "MQTT");
    lv_obj_set_style_text_font(lbl_mqtt, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_mqtt, LV_ALIGN_TOP_LEFT, 155, 168);

    sw_mqtt = lv_switch_create(cont_cfg_net);
    lv_obj_set_size(sw_mqtt, 40, 20);
    lv_obj_align(sw_mqtt, LV_ALIGN_TOP_LEFT, 200, 165);
    lv_obj_set_style_bg_color(sw_mqtt, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(sw_mqtt, lv_color_hex(0x2563eb), LV_PART_INDICATOR | LV_STATE_CHECKED);

    label_mqtt_status = lv_label_create(cont_cfg_net);
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(label_mqtt_status, &lv_font_montserrat_12, 0);
    lv_obj_align(label_mqtt_status, LV_ALIGN_TOP_LEFT, 248, 168);

    // Botón Guardar Red
    lv_obj_t * btn_save_net = lv_btn_create(cont_cfg_net);
    lv_obj_set_size(btn_save_net, 160, 35);
    lv_obj_align(btn_save_net, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(btn_save_net, lv_color_hex(0x2563eb), 0);
    lv_obj_t * lbl_save_net = lv_label_create(btn_save_net); 
    lv_label_set_text(lbl_save_net, "GUARDAR RED"); 
    lv_obj_center(lbl_save_net);
    lv_obj_add_event_cb(btn_save_net, [](lv_event_t * e){ 
        accion_guardar_config(); 
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt_user, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt_pass, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_main_qr, ta_qr_event_cb, LV_EVENT_ALL, kb);

    // --- OVERLAY DE ERROR ---
    overlay_error = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay_error, 320, 240);
    lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(overlay_error, lv_color_hex(0x991b1b), 0);
    lv_obj_t * lerr = lv_label_create(overlay_error);
    lv_label_set_text(lerr, "SOBRE-TEMPERATURA\nCRÍTICA PELIGRO\nSISTEMA BLOQUEADO");
    lv_obj_set_style_text_align(lerr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lerr, &lv_font_montserrat_20, 0);
    lv_obj_center(lerr);

    // Al deslizar al tile de config, pedir datos al Master
    lv_obj_add_event_cb(tv, [](lv_event_t * e){
        // Siempre pedir config y limites al cambiar de tile
        Serial2.print("GET_CONFIG\n");
        Serial2.print("GET_LIMITS\n");
    }, LV_EVENT_VALUE_CHANGED, NULL);
}