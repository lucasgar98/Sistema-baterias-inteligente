#include "ui.h"
#include <Arduino.h>

// --- VARIABLES GLOBALES (Labels y TextAreas) ---
// ======= ELEMENTOS GRÁFICOS DE LA INTERFAZ (OBJETOS O WIDGETS) =======
// Etiquetas
lv_obj_t * label_voltaje;  // Tensión de la batería (V)
lv_obj_t * label_corriente;  // Corriente de la batería (A)
lv_obj_t * label_temp;  // Temperatura (°C)
lv_obj_t * label_estado;  // Estado del sistema
lv_obj_t * label_ah_ri;  // Capacidad de la batería (Ah)
// Campos de texto o áreas de texto
lv_obj_t * ta_main_qr;  // Código QR
lv_obj_t * ta_ssid;  // SSID de la red WiFi
lv_obj_t * ta_pass;  // Contraseña de la red WiFi
lv_obj_t * ta_mqtt;  // IP del broker MQTT
lv_obj_t * ta_mqtt_user;  // Usuario MQTT
lv_obj_t * ta_mqtt_pass;  // Contraseña MQTT
lv_obj_t * ta_corte_carga;  // Tensión de corte de carga
lv_obj_t * ta_corte_descarga;  // Tensión de corte de descarga
lv_obj_t * ta_corriente_lim;  // Límite de corriente
// Otros objetos
lv_obj_t * dd_perfil;  // Lista desplegable para seleccionar el perfil de la batería
lv_obj_t * btn_stop; // Botón de parada (global para control dinámico)
lv_obj_t * btn_scan_qr;  // Botón para solicitar escaneo de código QR
lv_obj_t * overlay_error; // Panel de alerta crítica

// --- CONECTIVIDAD ON-DEMAND ---
lv_obj_t * sw_wifi;  // Permite activar/desactivar la conexión WiFi
lv_obj_t * sw_mqtt;  // Permite activar/desactivar la conexión MQTT
lv_obj_t * label_wifi_status;  // Estado de la conexión WiFi
lv_obj_t * label_mqtt_status;  // Estado de la conexión MQTT

// --- ESTILOS ---
static lv_style_t style_card;  // Estilo de una tarjeta
static lv_style_t style_btn_main;  // Estilo del botón principal

// --- FUNCIONES AUXILIARES ---
// Función que permite crear tarjetas dentro de cada tile o pantalla
static void create_metric_card(lv_obj_t * parent, lv_obj_t ** label, const char * title, lv_color_t color, int x, int y, int w, int h) {
    // Creamos el objeto tarjeta, que será hijo del objeto pasado por parámetro
    lv_obj_t * card = lv_obj_create(parent);
    // Fijamos el tamaño del objeto (ancho y alto)
    lv_obj_set_size(card, w, h);
    // Asignamos a la tarjeta el estilo creado para las tarjetas
    lv_obj_add_style(card, &style_card, 0);
    // Alineamos la tarjeta a la izquierda y hacia arriba y la posicionamos en la pantalla
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);
    // Deshabilitamos la bandera que hace que el objeto sea scrolleable
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Creamos una etiqueta asociada a la tarjeta, la cual será un objeto hijo de la tarjeta. Dicha etiqueta contendrá el título de la tarjeta
    lv_obj_t * t = lv_label_create(card);
    // Fijamos el texto de la etiqueta (que será el título de la tarjeta)
    lv_label_set_text(t, title);
    // Asignamos una fuente y un color al texto de la etiqueta
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);  // Montserrat 12px
    lv_obj_set_style_text_color(t, lv_color_hex(0x9ca3af), 0); // Color #9CA3AF (Gris claro)
    // Alineamos el título a la izquierda y hacia arriba y lo posicionamos en la pantalla
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, -5, -5);

    // Creamos otra etiqueta asociada a la tarjeta, que representa el contenido de esa tarjeta
    *label = lv_label_create(card);
    // Le asignamos a esta etiqueta una fuente y un color de texto
    lv_obj_set_style_text_font(*label, &lv_font_montserrat_14, 0);  // Montserrat 14px
    lv_obj_set_style_text_color(*label, color, 0);
    // Alineamos el contenido a la izquierda y hacia abajo y lo posicionamos en pantalla
    lv_obj_align(*label, LV_ALIGN_BOTTOM_LEFT, -5, 5);
    // Ponemos un texto por defecto para indicar que no se está mostrando nada
    lv_label_set_text(*label, "--");
}

// --- CALLBACKS ---
// Callback que se ejecutará cuando se dispare algún evento asociado a los campos de texto de los umbrales y de las credenciales
static void ta_event_cb(lv_event_t * e) {
    // Obtenemos el código del evento
    lv_event_code_t code = lv_event_get_code(e);
    // Obtenemos el objeto al cual se envía el evento (textarea)
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    // Obtenemos el puntero pasado como último parámetro de lv_obj_add_event_cb 
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);
    if(code == LV_EVENT_FOCUSED) {  // Evento que se dispara cuando el usuario toca algún campo de texto
        // Asociamos el teclado con el campo de texto enfocado
        lv_keyboard_set_textarea(kb, ta);
        // Mostramos el teclado en pantalla
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        // Scroll automático
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    }
    if(code == LV_EVENT_DEFOCUSED) {  // Evento que se dispara cuando el usuario deja de tocar algún campo de texto
        // Ocultamos el teclado en pantalla
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// Callback que se ejecutará cuando se dispare algún evento asociado al campo de texto del QR
static void ta_qr_event_cb(lv_event_t * e) {
    // Obtenemos el código del evento
    lv_event_code_t code = lv_event_get_code(e);
    // Obtenemos el objeto al cual se envía el evento (textarea)
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    // Obtenemos el puntero pasado como último parámetro de lv_obj_add_event_cb 
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);/
    if(code == LV_EVENT_FOCUSED) {  // Evento que se dispara cuando el usuario toca algún campo de texto
        // Asociamos el teclado con el campo de texto enfocado
        lv_keyboard_set_textarea(kb, ta);
        // Mostramos el teclado en pantalla
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_DEFOCUSED) {  // Evento que se dispara cuando el usuario deja de tocar algún campo de texto
        // Ocultamos el teclado en pantalla
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_READY) {
        // Desenfocamos el campo de texto
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        // Ocultamos el teclado en pantalla
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        
        // Enviar por UART al Master
        const char * qr_text = lv_textarea_get_text(ta);  // Obtenemos el texto del campo de texto (código QR ingresado)
        Serial2.print("SET_QR:");
        Serial2.print(qr_text);
        Serial2.print("\n");
    }
}
// Función que inicializa la interfaz gráfica. Crea los objetos de la interfaz, fija sus propiedades y los posiciona en la pantalla
void ui_init(void) {
    // 1. INICIALIZAR ESTILOS
    // Iniciamos el estilo tarjeta
    lv_style_init(&style_card);
    // Fijamos las propiedades del estilo
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1e1e1e));  // Color de fondo #1E1E1E (gris oscurando tirando a negro)
    lv_style_set_border_width(&style_card, 0);  // Ancho de borde 0px
    lv_style_set_radius(&style_card, 12);  // Radio 12px
    lv_style_set_shadow_width(&style_card, 10);  // Ancho de sombra 10px
    lv_style_set_shadow_ofs_y(&style_card, 5);  // Offset (desplazamiento) de la sombra en la dirección Y (5px)
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);  // Opacidad de la sombra
    // Inicializamos el estilo botón principal
    lv_style_init(&style_btn_main);
    // Fijamos la propiedades del estilo
    lv_style_set_radius(&style_btn_main, 8);  // Radio (8 px)
    lv_style_set_text_font(&style_btn_main, &lv_font_montserrat_12);  // Fuente del texto (Monstserrat 12)

    // 2. CREAR TILEVIEW (Navegación por gestos)
    // Creamos un tile view, el cual es un objeto contenedor cuyos elementos (llamados baldosas) pueden estar dispuestos en forma de grilla
    // Esto nos permite navegar deslizando la pantalla
    lv_obj_t * tv = lv_tileview_create(lv_scr_act());
    // Fijamos el color de fondo del tileview en #121212 (negro)
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x121212), 0);
    // Agregamos tiles o baldosas
    // LV_DIR_RIGHT significa que se debe deslizar el dedo hacia la derecha para acceder al siguiente tile
    // LV_DIR_LEFT significa que se debe deslizar el dedo hacia la izquierda para acceder al siguiente tile
    // LV_DIR_LEFT | LV_DIR_RIGHT significa que se puede deslizar el dedo hacia la izquierda o hacia la derecha para acceder al siguiente tile
    lv_obj_t * tile_main    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);  // Pantalla principal (fila 0, columna 0)
    lv_obj_t * tile_cfg_bat = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));  // Pantalla de configuración de la batería (fila 1, columna 0)
    lv_obj_t * tile_cfg_net = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);  // Pantalla de configuración de red (fila 2, columna 0)

    // --- PANTALLA PRINCIPAL ---
    // Cards de datos
    // Creamos tarjetas para mostrar los valores de tensión, corriente y capacidad. Todas esas tarjetas serán objetos hijos del tile_main (o sea, estarán
    // dentro de la pantalla principal)
    // Tarjeta de tensión, color #60A5FA (azul claro), posición x=10px y=10px, ancho 145px, alto 50px 
    create_metric_card(tile_main, &label_voltaje, "VOLTAJE", lv_color_hex(0x60a5fa), 10, 10, 145, 50);
    // Tarjeta de corriente, color #A78BFA (lila), posición x=165px y=10px, ancho 145px, alto 50px 
    create_metric_card(tile_main, &label_corriente, "CORRIENTE", lv_color_hex(0xa78bfa), 165, 10, 145, 50);
    // Tarjeta de capacidad, color #4ADE80 (verde claro), posición x=10px y=65px, ancho 300px, alto 45px
    create_metric_card(tile_main, &label_ah_ri, "CAPACIDAD / RI", lv_color_hex(0x4ade80), 10, 65, 300, 45);
    
    // Card Temperatura
    // Creamos una tarjeta para la temperatura, que será hija del tile_main (pantalla principal)
    lv_obj_t * card_temp = lv_obj_create(tile_main);
    // Fijamos el tamaño del objeto (ancho y alto)
    lv_obj_set_size(card_temp, 145, 45);  // Ancho 145px, alto 45px
    // Asignamos a la tarjeta el estilo creado para tarjetas
    lv_obj_add_style(card_temp, &style_card, 0);
    // Alineamos el objeto a la izquierda y hacia arriba y lo posicionamos en la pantalla
    lv_obj_align(card_temp, LV_ALIGN_TOP_LEFT, 10, 115);  // Posición x=10px, y=115px
    // Deshabilitamos la bandera "LV_OBJ_FLAG_SCROLLABLE" para que el objeto no sea scrolleable
    lv_obj_clear_flag(card_temp, LV_OBJ_FLAG_SCROLLABLE);

    // Creamos un título para la tarjeta (hijo de la tarjeta)
    lv_obj_t * lbl_temp_title = lv_label_create(card_temp);
    lv_label_set_text(lbl_temp_title, "TEMPERATURA");
    // Asignamos color y fuente al título de la tarjeta
    lv_obj_set_style_text_font(lbl_temp_title, &lv_font_montserrat_12, 0);  // Montserrat 12px
    lv_obj_set_style_text_color(lbl_temp_title, lv_color_hex(0x9ca3af), 0);  // Color #9CA3AF (gris claro)
    // Alineamos el objeto a la izquierda y hacia arriba y lo posicionamos
    lv_obj_align(lbl_temp_title, LV_ALIGN_TOP_LEFT, -5, -5);

    // Creamos la etiqueta que mostrará el valor de la temperatura (hijo de la tarjeta)
    label_temp = lv_label_create(card_temp);
    // Asignamos color y fuente
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_14, 0);  // Montserrat 14px
    lv_obj_set_style_text_color(label_temp, lv_color_hex(0xFFFFFF), 0);  // Color #FFFFFF (blanco puro)
    // Alineamos el objeto a la izquierda y hacia abajo y lo posicionamos
    lv_obj_align(label_temp, LV_ALIGN_BOTTOM_LEFT, -5, 5);
    // Ponemos un texto por defecto para indicar que no se está mostrando nada
    lv_label_set_text(label_temp, "-- C");

    // Card QR (Text Area para poder escribirlo manualmente y botón para escaneo autónomo)
    // Creamos una tarjeta para el QR (hija del tile_main)
    lv_obj_t * card_qr = lv_obj_create(tile_main);
    // Fijamos el tamaño de la tarjeta (ancho y alto)
    lv_obj_set_size(card_qr, 145, 45);  // Ancho 145px, alto 45px
    // Asignamos a la tarjeta el estilo creado para tarjetas
    lv_obj_add_style(card_qr, &style_card, 0);
    // Alineamos el objeto a la izquierda y hacia arriba y lo posicionamos en la pantalla
    lv_obj_align(card_qr, LV_ALIGN_TOP_LEFT, 165, 115);  // Posición x=165px, y=115px
    // Deshabilitamos la bandera "LV_OBJ_FLAG_SCROLLABLE" para que el objeto no sea scrolleable
    lv_obj_clear_flag(card_qr, LV_OBJ_FLAG_SCROLLABLE);

    // Creamos un título para la tarjeta QR (hijo de la tarjeta)
    lv_obj_t * lbl_qr_title = lv_label_create(card_qr);
    lv_label_set_text(lbl_qr_title, "QR BATERÍA");
    // Asignamos fuente y color al título
    lv_obj_set_style_text_font(lbl_qr_title, &lv_font_montserrat_12, 0);  // Montserrat 12px
    lv_obj_set_style_text_color(lbl_qr_title, lv_color_hex(0x9ca3af), 0);  // Color #9CA3AF (gris claro)
    // Alineamos el título a la izquierda y hacia arriba y lo posicionamos en la pantalla
    lv_obj_align(lbl_qr_title, LV_ALIGN_TOP_LEFT, -5, -5);

    // Creamos un campo de texto para que el usuario pueda ingresar manualmente el QR (hijo de la tarjeta)
    ta_main_qr = lv_textarea_create(card_qr);
    // Fijamos el tamaño del campo de texto (ajustado a 82px para dejar lugar al botón SCAN)
    lv_obj_set_size(ta_main_qr, 82, 25);  // Ancho 82px, alto 25px
    // Alineamos el campo de texto a la izquierda y hacia abajo y lo posicionamos
    lv_obj_align(ta_main_qr, LV_ALIGN_BOTTOM_LEFT, -10, 5);
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_main_qr, "Ingresar QR");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_main_qr, true);
    // Asignamos estilos al campo de texto
    lv_obj_set_style_bg_opa(ta_main_qr, LV_OPA_TRANSP, 0);  // Opacidad de fondo (LV_OPA_TRANSP = totalmente transparente)
    lv_obj_set_style_border_width(ta_main_qr, 0, 0);  // Ancho de borde 0px
    lv_obj_set_style_pad_all(ta_main_qr, 0, 0);  // Padding 0px
    lv_obj_set_style_text_font(ta_main_qr, &lv_font_montserrat_12, 0);  // Fuente Montserrat 12px
    lv_obj_set_style_text_color(ta_main_qr, lv_color_hex(0xfbbf24), 0);  // Color #FBBF24 (amarillo)

    // Botón SCAN para activar el lector físico de la ESP32-CAM por comando serie
    btn_scan_qr = lv_btn_create(card_qr);
    lv_obj_set_size(btn_scan_qr, 48, 24);  // Ancho 48px, alto 24px
    lv_obj_align(btn_scan_qr, LV_ALIGN_BOTTOM_RIGHT, 10, 5);
    lv_obj_set_style_bg_color(btn_scan_qr, lv_color_hex(0x0284c7), 0);  // Color azul cian
    lv_obj_set_style_radius(btn_scan_qr, 6, 0);  // Radio 6px
    lv_obj_set_style_pad_all(btn_scan_qr, 0, 0);  // Padding 0px
    lv_obj_add_event_cb(btn_scan_qr, [](lv_event_t * e){ accion_escanear_qr(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_btn_scan = lv_label_create(btn_scan_qr);
    lv_label_set_text(lbl_btn_scan, "SCAN");
    lv_obj_set_style_text_font(lbl_btn_scan, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_btn_scan);

    // Card Estado Actual
    // Creamos una tarjeta para mostrar el estado actual (hija del tile_main)
    lv_obj_t * card_estado = lv_obj_create(tile_main);
    // Fijamos el tamaño de la tarjeta
    lv_obj_set_size(card_estado, 300, 35); // Ancho 300px, alto 35px
    // Asignamos a la tarjeta el estilo creado para las tarjetas
    lv_obj_add_style(card_estado, &style_card, 0);
    // Alineamos la tarjeta a la izquierda y hacia arriba y la posicionamos en pantalla
    lv_obj_align(card_estado, LV_ALIGN_TOP_LEFT, 10, 165);  // Posición x=10px, y=165px
    // Deshabilitamos la bandera "LV_OBJ_FLAG_SCROLLABLE" para que el objeto no sea scrolleable
    lv_obj_clear_flag(card_estado, LV_OBJ_FLAG_SCROLLABLE);

    // Creamos un título para la tarjeta (hijo de la tarjeta)
    lv_obj_t * lbl_estado_title = lv_label_create(card_estado);
    lv_label_set_text(lbl_estado_title, "ESTADO ACTUAL:");
    // Asignamos fuente y color al título
    lv_obj_set_style_text_font(lbl_estado_title, &lv_font_montserrat_12, 0);  // Montserrat 12px
    lv_obj_set_style_text_color(lbl_estado_title, lv_color_hex(0x9ca3af), 0);  // Color #9CA3AF (gris claro)
    // Alineación a la izquierda y al medio
    lv_obj_align(lbl_estado_title, LV_ALIGN_LEFT_MID, -5, 0);

    // Creamos una etiqueta para mostrar el valor de la tarjeta (hijo de la tarjeta)
    label_estado = lv_label_create(card_estado);
    // Asignamos fuente y color al texto
    lv_obj_set_style_text_font(label_estado, &lv_font_montserrat_14, 0);  // Montserrat 14px
    lv_obj_set_style_text_color(label_estado, lv_color_hex(0xfbbf24), 0);  // Color #FBBF24 (amarillo)
    // Alineación a la derecha y al medio
    lv_obj_align(label_estado, LV_ALIGN_RIGHT_MID, 5, 0);
    // Fijamos un texto por defecto
    lv_label_set_text(label_estado, "REPOSO");

    // Botones (Abajo)
    // Creamos un botón para iniciar el modo automático (varios ciclos de carga y descarga)
    lv_obj_t * btn_test = lv_btn_create(tile_main);  // Hijo del tile_main (pantalla principal)
    // Fijamos el tamaño del botón
    lv_obj_set_size(btn_test, 55, 30);  // Ancho 55px, alto 30px
    // Alineamos el botón a la izquierda y hacia abajo y lo posicionamos en pantalla
    lv_obj_align(btn_test, LV_ALIGN_BOTTOM_LEFT, 10, -5);  // 10px hacia la derecha, 5px hacia arriba
    // Fijamos el color de fondo del botón
    lv_obj_set_style_bg_color(btn_test, lv_color_hex(0x2563eb), 0);  // Color #2563EB (azul)
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // La función callback se implementa como una función lambda, que es una característica propia de C++ para ahorrar código
    // Cuando se presiona el botón, enviamos el comando "START_AUTO" al ESP32 maestro a través del UART2, para iniciar el modo automático
    lv_obj_add_event_cb(btn_test, [](lv_event_t * e){ Serial2.print("START_AUTO\n"); }, LV_EVENT_CLICKED, NULL);
    
    // Creamos la etiqueta del botón (hija del botón), le asignamos el texto "AUTO" y lo centramos
    lv_obj_t * lt = lv_label_create(btn_test); lv_label_set_text(lt, "AUTO"); lv_obj_center(lt);

    // Creamos un botón para iniciar la carga
    lv_obj_t * btn_c = lv_btn_create(tile_main);  // Hijo de tile_main
    // Fijamos el tamaño del botón
    lv_obj_set_size(btn_c, 55, 30);  // Ancho 55px, alto 30px
    // Alineamos el botón a la izquierda y hacia abajo y lo posicionamos tomando como referencia la esquina inferior izquierda
    lv_obj_align(btn_c, LV_ALIGN_BOTTOM_LEFT, 70, -5);  // 70px hacia la derecha, 5px hacia arriba
    // Fijamos el color de fondo del botón
    lv_obj_set_style_bg_color(btn_c, lv_color_hex(0x16a34a), 0);  // Color #16A34A (verde)
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // Cuando se presiona el botón, se envía el comando "START_C" al ESP32 maestro a través del UART2 para iniciar la carga
    lv_obj_add_event_cb(btn_c, [](lv_event_t * e){ accion_iniciar_carga(); }, LV_EVENT_CLICKED, NULL);
    // Creamos la etiqueta del botón (hija del botón), le asignamos el texto "CARGA" y lo centramos
    lv_obj_t * lc = lv_label_create(btn_c); lv_label_set_text(lc, "CARGA"); lv_obj_center(lc);

    // Creamos un botón para iniciar la descarga (hijo de tile_main) y fijamos su tamaño
    lv_obj_t * btn_d = lv_btn_create(tile_main);
    lv_obj_set_size(btn_d, 55, 30); // Ancho 55px, alto 30px
    // Alineamos el botón con la esquina inferior izquierda y lo posicionamos tomando esa referencia
    lv_obj_align(btn_d, LV_ALIGN_BOTTOM_LEFT, 130, -5);  // 130px hacia la derecha, 5px hacia arriba
    // Fijamos el color de fondo del botón
    lv_obj_set_style_bg_color(btn_d, lv_color_hex(0xd97706), 0);  // Color #D97706 (amarillo oscuro tirando a naranja)
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // Cuando se presiona el botón, se envía el comando "START_D" al maestro para iniciar la descarga
    lv_obj_add_event_cb(btn_d, [](lv_event_t * e){ accion_iniciar_descarga(); }, LV_EVENT_CLICKED, NULL);
    // Creamos la etiqueta del botón (hija del botón), le asignamos el texto "DESC" y lo centramos
    lv_obj_t * ld = lv_label_create(btn_d); lv_label_set_text(ld, "DESC"); lv_obj_center(ld);

    // Creamos el botón para hacer la parada del sistema (hijo de tile_main) y fijamos su tamaño
    btn_stop = lv_btn_create(tile_main);
    lv_obj_set_size(btn_stop, 110, 30);  // Ancho 110px, alto 30px
    // Alineamos el botón con la esquina inferior derecha y lo posicionamos
    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -10, -5);  //10px hacia la izquierda, 5px hacia arriba
    // Fijamos el color de fondo del botón
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x444444), 0);  // Color #444444 (gris oscuro)
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // Cuando se presiona el botón, se envía el comando "STOP" al maestro para parar el sistema
    lv_obj_add_event_cb(btn_stop, [](lv_event_t * e){ accion_parar_todo(); }, LV_EVENT_CLICKED, NULL);
    // Creamos la etiqueta del botón (hija del botón), le asignamos el texto "PARAR" y la centramos
    lv_obj_t * ls = lv_label_create(btn_stop); lv_label_set_text(ls, "PARAR"); lv_obj_center(ls);



    // --- TECLADO VIRTUAL GLOBAL ---
    // Lo creamos en el screen activo para que flote encima de cualquier pestaña
    // Creamos un teclado virtual para escribir texto dentro de un campo de texto (textarea). Este teclado será hijo de la pantalla actual,
    // de esta manera flotará encima de cualquier pestaña
    lv_obj_t * kb = lv_keyboard_create(lv_scr_act());
    // Fijamos el tamaño del teclado
    lv_obj_set_size(kb, 320, 110);  // Ancho 320px, alto 110px
    // Alineamos el objeto con la mitad inferior, y lo posicionamos tomando esa referencia
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Ocultamos el teclado activando la bandera "LV_OBJ_FLAG_HIDDEN"
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // --- PANTALLA DE CONFIGURACIÓN DE BATERÍA ---
    // Creamos un objeto contenedor para la configuración de la batería, que será hijo de tile_cfg_bat (pantalla de configuración de batería)
    lv_obj_t * cont_cfg_bat = lv_obj_create(tile_cfg_bat);
    // Fijamos el tamaño del contenedor
    lv_obj_set_size(cont_cfg_bat, 320, 240);  // Ancho 320px, alto 240px
    // Fijamos la dirección de scroll (scroll vertical)
    lv_obj_set_scroll_dir(cont_cfg_bat, LV_DIR_VER);
    // Asignamos estilos al contenedor
    lv_obj_set_style_bg_opa(cont_cfg_bat, 0, 0);  // Opacidad de fondo (0 = completamente transparente)
    lv_obj_set_style_border_width(cont_cfg_bat, 0, 0);  // Ancho de borde (0px)

    // Creamos el título (hijo del contenedor)
    lv_obj_t * title_cfg_bat = lv_label_create(cont_cfg_bat);
    lv_label_set_text(title_cfg_bat, "PARÁMETROS DE BATERÍA");
    // Asignamos fuente al texto
    lv_obj_set_style_text_font(title_cfg_bat, &lv_font_montserrat_14, 0);  // Montserrat 14px
    // Alineamos el título con la mitad superior
    lv_obj_align(title_cfg_bat, LV_ALIGN_TOP_MID, 0, 0);

    // Creamos la lista desplegable para seleccionar el perfil (hijo del contenedor)
    dd_perfil = lv_dropdown_create(cont_cfg_bat);
    // Fijamos el tamaño de la lista desplegable
    lv_obj_set_size(dd_perfil, 180, 35);  // Ancho 180px, alto 35px
    // Alineamos el objeto con la mitad superior y lo posicionamos
    lv_obj_align(dd_perfil, LV_ALIGN_TOP_MID, 0, 30);  // 30px hacia abajo
    // Seteamos las opciones de la lista desplegable para elegir el perfil de la batería
    lv_dropdown_set_options(dd_perfil, "LI-ION-2S\nLI-ION-3S\nPB-12V\nNiMH-7.2V\nPERSONALIZADO");

    // Creamos un título para la tensión de corte de carga (hijo del contenedor)
    lv_obj_t * lbl_corte_carga = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corte_carga, "Corte Carga (V):");
    // Fijamos la fuente del texto
    lv_obj_set_style_text_font(lbl_corte_carga, &lv_font_montserrat_12, 0);  // Montserrat 12px
    // Alineamos el título con la esquira superior izquierda y lo posicionamos tomando esa referencia
    lv_obj_align(lbl_corte_carga, LV_ALIGN_TOP_LEFT, 10, 80);  // 10px hacia la derecha, 80px hacia abajo

    // Creamos un campo de texto para fijar el valor de la tensión de corte de carga (hijo del contenedor)
    ta_corte_carga = lv_textarea_create(cont_cfg_bat);
    // Fijamos el tamaño del campo de texto
    lv_obj_set_size(ta_corte_carga, 120, 35);  // Ancho 120px, alto 35px
    // Alineamos el objeto con la esquina superior izquierda y lo posicionamos tomando esa referencia 
    lv_obj_align(ta_corte_carga, LV_ALIGN_TOP_LEFT, 150, 70);  // 150px hacia la derecha, 70px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_corte_carga, "Carga V");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_corte_carga, true);

    // Creamos un título para la tensión de corte de descarga (hijo del contenedor) y fijamos su texto
    lv_obj_t * lbl_corte_descarga = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corte_descarga, "Corte Descarga (V):");
    // Fijamos la fuente del texto
    lv_obj_set_style_text_font(lbl_corte_descarga, &lv_font_montserrat_12, 0); // Montserrat 12px
    // Alineamos el título con la esquina superior izquierda y lo posicionamos tomando esa referencia
    lv_obj_align(lbl_corte_descarga, LV_ALIGN_TOP_LEFT, 10, 125);  // 10px hacia la derecha, 125px hacia abajo

    // Creamos un campo de texto para fijar el valor de la tensión de corte de descarga (hijo del contenedor)
    ta_corte_descarga = lv_textarea_create(cont_cfg_bat); 
    // Fijamos el tamaño del campo de texto
    lv_obj_set_size(ta_corte_descarga, 120, 35); // Ancho 120px, alto 35px
    // Alineamos el objeto con la esquina superior izquierda y lo posicionamos tomando esa referencia 
    lv_obj_align(ta_corte_descarga, LV_ALIGN_TOP_LEFT, 150, 115);  // 150px hacia la derecha, 115px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_corte_descarga, "Descarga V");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_corte_descarga, true);

    // Creamos un título para la corriente límite (hijo del contenedor cfg_bat) y fijamos su texto
    lv_obj_t * lbl_corriente_lim = lv_label_create(cont_cfg_bat);
    lv_label_set_text(lbl_corriente_lim, "Corriente Lim (mA):");
    // Fijamos la fuente del texto
    lv_obj_set_style_text_font(lbl_corriente_lim, &lv_font_montserrat_12, 0);
    // Alineamos el objeto a la esquina superior izquierda y los posicionamos tomando esa referencia
    lv_obj_align(lbl_corriente_lim, LV_ALIGN_TOP_LEFT, 10, 170);  // 10px hacia la derecha, 170px hacia abajo

    // Creamos un campo de texto para fijar la corriente límite (hijo del contenedor)
    ta_corriente_lim = lv_textarea_create(cont_cfg_bat);
    // Fijamos el tamaño del campo de texto
    lv_obj_set_size(ta_corriente_lim, 120, 35);  // Ancho 120px, alto 35px
    // Alineamos el objeto a la esquina superior izquierda y lo posicionamos tomando esa referencia
    lv_obj_align(ta_corriente_lim, LV_ALIGN_TOP_LEFT, 150, 160);  // 150px hacia la derecha, 160px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_corriente_lim, "Corriente mA");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_corriente_lim, true);

    // Creamos un botón para guardar la configuración de la batería (hijo de cont_cfg_bat)
    lv_obj_t * btn_save_bat = lv_btn_create(cont_cfg_bat);
    // Fijamos el tamaño del botón
    lv_obj_set_size(btn_save_bat, 160, 35);  // Ancho 160px, alto 35px
    // Alineamos el botón a la mitad superior y lo posicionamos tomando esa referencia
    lv_obj_align(btn_save_bat, LV_ALIGN_TOP_MID, 0, 210);  // 210px hacia abajo
    // Fijamos el color del botón
    lv_obj_set_style_bg_color(btn_save_bat, lv_color_hex(0x9333ea), 0);  // Color #9333EA (violeta)
    // Creamos la etiqueta del botón (hijo del botón) y fijamos el texto
    lv_obj_t * lbl_save_bat = lv_label_create(btn_save_bat);
    lv_label_set_text(lbl_save_bat, "GUARDAR PERFIL");
    // Centramos el botón
    lv_obj_center(lbl_save_bat);
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // Al presionar este botón, se enviarán al ESP32 maestro los comandos para cambiar el perfil y guardar los límites
    lv_obj_add_event_cb(btn_save_bat, [](lv_event_t * e){ 
        accion_cambiar_perfil(); 
        accion_guardar_limites(); 
    }, LV_EVENT_CLICKED, NULL);
    // Añadimos una función callback que se ejecutará cuando se dispare cualquier evento vinculado a los campos de texto de los umbrales de tensión y corriente
    // A cada callback le pasamos como parámetro el teclado en pantalla (kb)
    lv_obj_add_event_cb(ta_corte_carga, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_corte_descarga, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_corriente_lim, ta_event_cb, LV_EVENT_ALL, kb);

    // --- PANTALLA DE CONFIGURACIÓN DE RED ---
    // Creamos el contenedor de todos los objetos para la configuración de red (hijo del tile de configuración de red)
    lv_obj_t * cont_cfg_net = lv_obj_create(tile_cfg_net);
    // Fijamos el tamaño del contenedor
    lv_obj_set_size(cont_cfg_net, 320, 240);  // Ancho 320px, alto 240px
    // Fijamos la dirección de scroll (scroll vertical)
    lv_obj_set_scroll_dir(cont_cfg_net, LV_DIR_VER);
    // Asignamos estilos al contenedor
    lv_obj_set_style_bg_opa(cont_cfg_net, 0, 0);  // Opacidad de fondo (0 = completamente transparente)
    lv_obj_set_style_border_width(cont_cfg_net, 0, 0);  // Ancho de borde 0px

    // Creamos el título de la pantalla de configuración de red (hijo del contenedor)
    lv_obj_t * title_cfg_net = lv_label_create(cont_cfg_net);
    lv_label_set_text(title_cfg_net, "CONEXIÓN WI-FI / MQTT");
    // Fijamos la fuente del texto
    lv_obj_set_style_text_font(title_cfg_net, &lv_font_montserrat_14, 0);  // Montserrat 14px
    // Alineamos el objeto a la mitad superior, y lo posicionamos
    lv_obj_align(title_cfg_net, LV_ALIGN_TOP_MID, 0, 0);

    // Creamos el campo de texto para el SSID
    ta_ssid = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_ssid, 140, 35);  // Ancho 140px, alto 35px
    // Alineación a la esquina superior izquierda
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 0, 30);  // 30px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_ssid, "SSID");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_ssid, true);
    
    // Creamos el campo de texto para la contraseña WiFi
    ta_pass = lv_textarea_create(cont_cfg_net);
    lv_obj_set_size(ta_pass, 140, 35);  // Ancho 140px, alto 35px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 150, 30);  // 150px hacia la derecha, 30px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    // Habilitamos el modo contraseña para ocultar los caracteres
    lv_textarea_set_password_mode(ta_pass, true);
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_pass, true);

    // Creamos el campo de texto para la IP del broker MQTT
    ta_mqtt = lv_textarea_create(cont_cfg_net);
    lv_obj_set_size(ta_mqtt, 290, 35);  // Ancho 290px, alto 35px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(ta_mqtt, LV_ALIGN_TOP_LEFT, 0, 75);  // 75px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_mqtt, "Broker IP");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_mqtt, true);

    // Creamos el campo de texto para el usuario MQTT
    ta_mqtt_user = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_mqtt_user, 140, 35);  // Ancho 140px, alto 35px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(ta_mqtt_user, LV_ALIGN_TOP_LEFT, 0, 120);  // 120px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_mqtt_user, "MQTT User");
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_mqtt_user, true);
    
    // Creamos el campo de texto para la contraseña MQTT
    ta_mqtt_pass = lv_textarea_create(cont_cfg_net); 
    lv_obj_set_size(ta_mqtt_pass, 140, 35);  // Ancho 140px, alto 35px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(ta_mqtt_pass, LV_ALIGN_TOP_LEFT, 150, 120);  // 150px hacia la derecha, 120px hacia abajo
    // Fijamos el texto que se mostrará cuando el campo de texto esté vacío
    lv_textarea_set_placeholder_text(ta_mqtt_pass, "MQTT Pass");
    // Habilitamos el modo contraseña para ocultar los caracteres
    lv_textarea_set_password_mode(ta_mqtt_pass, true);
    // Hacemos que el campo de texto sea de una sola línea
    lv_textarea_set_one_line(ta_mqtt_pass, true);

    // Wi-Fi
    // Creamos una etiqueta que se ubicará al lado del interruptor WiFi (hijo del contenedor)
    lv_obj_t * lbl_wifi = lv_label_create(cont_cfg_net);
    lv_label_set_text(lbl_wifi, "Wi-Fi");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_12, 0);  // Fuente Montserrat 12px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_LEFT, 0, 168);  // 168px hacia abajo

    // Creamos el interruptor para activar/desactivar el WiFi
    sw_wifi = lv_switch_create(cont_cfg_net);
    lv_obj_set_size(sw_wifi, 40, 20);  // Ancho 40px, alto 20px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(sw_wifi, LV_ALIGN_TOP_LEFT, 50, 165);  // 50px hacia la derecha, 165px hacia abajo
    // Fijamos el color de fondo
    lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x333333), 0);  // Color #333333 (gris oscuro)
    lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x16a34a), LV_PART_INDICATOR | LV_STATE_CHECKED);  // Color #16A34A (verde)

    // Creamos la etiqueta que muestra el estado de la red WiFi
    label_wifi_status = lv_label_create(cont_cfg_net);
    // Fijamos el texto de la etiqueta correspondiente al estado del WiFi (le asignamos el texto "Off")
    lv_label_set_text(label_wifi_status, LV_SYMBOL_CLOSE " Off");
    // Fijamos el color y la fuente del texto
    lv_obj_set_style_text_color(label_wifi_status, lv_color_hex(0x888888), 0);  // Color #888888 (gris)
    lv_obj_set_style_text_font(label_wifi_status, &lv_font_montserrat_12, 0);  // Montserrat 12px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(label_wifi_status, LV_ALIGN_TOP_LEFT, 100, 168);  // 100px hacia la derecha, 168px hacia abajo

    // MQTT
    // Creamos una etiqueta que se ubicará al lado del interruptor MQTT (hijo del contenedor)
    lv_obj_t * lbl_mqtt = lv_label_create(cont_cfg_net);
    lv_label_set_text(lbl_mqtt, "MQTT");
    lv_obj_set_style_text_font(lbl_mqtt, &lv_font_montserrat_12, 0);  // Fuente Montserrat 12px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(lbl_mqtt, LV_ALIGN_TOP_LEFT, 155, 168);  // 155px hacia la derecha, 168px hacia abajo

    // Creamos el interruptor para activar/desactivar MQTT
    sw_mqtt = lv_switch_create(cont_cfg_net);
    lv_obj_set_size(sw_mqtt, 40, 20);  // Ancho 40px, alto 20px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(sw_mqtt, LV_ALIGN_TOP_LEFT, 200, 165);  // 200px hacia la derecha, 165px hacia abajo
    // Fijamos el color de fondo
    lv_obj_set_style_bg_color(sw_mqtt, lv_color_hex(0x333333), 0);  // Color #333333 (gris oscuro)
    lv_obj_set_style_bg_color(sw_mqtt, lv_color_hex(0x2563eb), LV_PART_INDICATOR | LV_STATE_CHECKED);  // Color #2563EB (azul)

    // Creamos la etiqueta que muestra el estado de la conexión MQTT
    label_mqtt_status = lv_label_create(cont_cfg_net);
    // Fijamos el texto de la etiqueta correspondiente al estado del MQTT (le asignamos el texto "Off")
    lv_label_set_text(label_mqtt_status, LV_SYMBOL_CLOSE " Off");
    // Fijamos el color y la fuente del texto
    lv_obj_set_style_text_color(label_mqtt_status, lv_color_hex(0x888888), 0);  // Color #888888 (gris)
    lv_obj_set_style_text_font(label_mqtt_status, &lv_font_montserrat_12, 0); // Fuente Montserrat 12px
    // Alineacion con la esquina superior izquierda
    lv_obj_align(label_mqtt_status, LV_ALIGN_TOP_LEFT, 248, 168);  // 248px hacia la derecha, 168px hacia abajo

    // Botón Guardar Red
    // Creamos un botón para guardar la configuración de la red
    lv_obj_t * btn_save_net = lv_btn_create(cont_cfg_net);
    lv_obj_set_size(btn_save_net, 160, 35);  // Ancho 160px, alto 35px
    // Alineación con la mitad superior
    lv_obj_align(btn_save_net, LV_ALIGN_TOP_MID, 0, 200);  // 200px hacia abajo
    // Fijamos el color de fondo del botón
    lv_obj_set_style_bg_color(btn_save_net, lv_color_hex(0x2563eb), 0);  // Color #2563EB (azul)

    // Creamos la etiqueta del botón guardar red (hijo del botón)
    lv_obj_t * lbl_save_net = lv_label_create(btn_save_net); 
    lv_label_set_text(lbl_save_net, "GUARDAR RED");
    // Centramos el objeto
    lv_obj_center(lbl_save_net);
    // Agregamos la función callback que se ejecutará cuando se dispare el evento de presionado del botón ("LV_EVENT_CLICKED")
    // Al presionar este botón, se envía al ESP32 maestro el comando "SET_WIFI" junto a las credenciales
    lv_obj_add_event_cb(btn_save_net, [](lv_event_t * e){ 
        accion_guardar_config(); 
    }, LV_EVENT_CLICKED, NULL);

    // Añadimos una función callback que se ejecutará cuando se dispare cualquier evento vinculado a los campos de texto de las credenciales
    // A cada callback le pasamos como parámetro el teclado en pantalla (kb)
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt_user, ta_event_cb, LV_EVENT_ALL, kb);
    lv_obj_add_event_cb(ta_mqtt_pass, ta_event_cb, LV_EVENT_ALL, kb);
    // Añadimos una función callback que se ejecutará cuando se dispare cualquier evento vinculado al campo de texto del QR
    lv_obj_add_event_cb(ta_main_qr, ta_qr_event_cb, LV_EVENT_ALL, kb);

    // --- OVERLAY DE ERROR ---
    overlay_error = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay_error, 320, 240);  // 320px ancho, 240px alto
    // Ocultamos el objeto activando la bandera correspondiente
    lv_obj_add_flag(overlay_error, LV_OBJ_FLAG_HIDDEN);
    // Fijamos el color de fondo
    lv_obj_set_style_bg_color(overlay_error, lv_color_hex(0x991b1b), 0);  // Color #991B1B (rojo oscuro / bordó)
    // Creamos la etiqueta para el overlay_error
    lv_obj_t * lerr = lv_label_create(overlay_error);
    // Fijamos el texto
    lv_label_set_text(lerr, "SOBRE-TEMPERATURA\nCRÍTICA PELIGRO\nSISTEMA BLOQUEADO");
    lv_obj_set_style_text_align(lerr, LV_TEXT_ALIGN_CENTER, 0);  // Alineación central
    lv_obj_set_style_text_font(lerr, &lv_font_montserrat_20, 0);  // Fuente Montserrat 20
    lv_obj_center(lerr);  // Centramos el objeto

    // Al deslizar al tile de config, pedir datos al Master
    // Añadimos un callback que se ejecutará al cambiar de tile
    lv_obj_add_event_cb(tv, [](lv_event_t * e){
        // Siempre pedir config y limites al cambiar de tile
        Serial2.print("GET_CONFIG\n");
        Serial2.print("GET_LIMITS\n");
    }, LV_EVENT_VALUE_CHANGED, NULL);
}