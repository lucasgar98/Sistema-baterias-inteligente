from machine import Pin, I2C, PWM, ADC, UART
from ina219 import INA219  # Módulo para leer el sensor de corriente INA219 (no está incluido en la carpeta esp32_master)
from time import sleep, ticks_ms, ticks_diff
import network
import math
import json  # Módulo para trabajar con archivos JSON
from umqtt.simple import MQTTClient  # Módulo para trabajar con MQTT (no está incluido en la carpeta esp32_master)

# ========================
# LECTURA DE CONFIGURACIÓN (Wi-Fi y MQTT)
# ========================
# Función que carga la configuración WiFi y MQTT que se encuentra en el archivo config.json
# Si no encuentra el archivo, retorna parámetros de configuración por defecto
def cargar_config():
    try:
        # Utilizamos la estructura with...as, la cual simplifica la gestión de recursos que deben
        # ser limpiados después de su uso. En este caso, nos permite abrir el archivo sin necesidad
        # de cerrarlo manualmente
        with open('config.json', 'r') as f:
            return json.load(f)  # Convierte el JSON a un diccionario de Python
    except:
        # Retornamos credenciales por defecto en caso de un error al abrir el archivo
        return {"ssid": "Proyecto", "pass": "ligafederal", "mqtt": "10.187.200.199"}

config_actual = cargar_config()
# ========= CREDENCIALES DE WIFI y MQTT ===========
# Credenciales de la red WiFi
WIFI_SSID = config_actual["ssid"]  # SSID
WIFI_PASSWORD = config_actual["pass"]  # Contraseña
# Credenciales del broker MQTT
MQTT_BROKER = config_actual["mqtt"]  # Dirección IP del broker MQTT
MQTT_USER = config_actual.get("m_user", "admin")  # Usuario ("admin" si no existe en config.json)
MQTT_PASS = config_actual.get("m_pass", "baterias2026")  # Contraseña ("baterias2026 si no existe en config.json")

# ========================
# CONFIGURACIÓN GENERAL 
# ========================
# ========== PARÁMETROS DE CONFIGURACIÓN DE MQTT ===========
MQTT_PORT = 1884  # Puerto
MQTT_CLIENT_ID = 'Gonza'  # ID del cliente
# Tópico para enviar los valores actuales de tensión y corriente de la batería, junto con parámetros adicionales
# Las cadenas de texto son enviadas por el maestro cada 3 segundos, con el siguiente formato: "v:val,i:val,t:val,s:estado,qr:id,ah:val,p:perfil,vc:val,vd:val"
MQTT_TOPIC_TELEMETRY = 'ESP32/telemetria'
# Tema para el intercambio bidireccional del QR de la batería activa
MQTT_TOPIC_QR = 'ESP32/bateria_qr'
# Tema para enviar mensajes de error debido a exceso de temperatura ("SOBRETEMPERATURA:xxC")
MQTT_TOPIC_ALERTA = 'ESP32/alerta_critica'
# Tópico para publicar los límites de carga y descarga de la batería (V_C:x.x,V_D:y.y")
MQTT_TOPIC_CONFIG = 'ESP32/config_perfil' # Nuevo: Recibir perfiles
# Tópico para enviar comandos para que la batería se cargue ("START_C") o se descargue ("START_D"), así como también para frenar el sistema ("STOP_ALL")
MQTT_TOPIC_COMANDOS = 'ESP32/comandos'     # Nuevo: Recibir comandos desde Web

# Limites de Operación Segura (NiMH 7.2V por defecto)
TEMP_MAXIMA_SEGURA = 45.0  # Temperatura máxima (45°C)
VOLTAJE_CORTE_CARGA = 9.0  # Tensión máxima de carga (9 V)
VOLTAJE_CORTE_DESCARGA = 6.0  # Tensión mínima de descarga (6 V)
CORRIENTE_OBJETIVO = 900  # Corriente de carga (900 mA)
# Parámetros del ADC
V_REF = 1100.0  # Tensión de referencia del ADC integrado del ESP32 (1100 mV)
K = 0.25  # Constante de atenuación del ADC

# Diccionario que almacena los umbrales de carga y descarga para cada tipo de batería
PERFILES_LOCALES = {
    "LI-ION-2S": (8.4, 6.0),  # 2S de iones de litio
    "LI-ION-3S": (12.6, 9.0),  # 3S de iones de litio
    "PB-12V": (14.4, 11.0),  # Plomo ácido de 12 V
    "NiMH-7.2V": (9.0, 6.0)  # Níquel-metalhidruro de 7.2 V
}
TIEMPO_DESCANSO_MS = 15 * 60 * 1000 # Tiempo de descanso (15 minutos)

# ========================
# CONFIGURACIÓN DE HARDWARE
# ========================
# Configuramos los pines GPIO21 y GPIO22 como pines SCL y SDA del bus I2C0
i2c = I2C(0, scl=Pin(21), sda=Pin(22))
# Inicializamos el sensor de corriente INA219, vinculándolo al bus I2C0
ina = INA219(i2c)
ina.configure()
# Configuramos los pines GPIO18 y GPIO19 como salidas PWM a 500 Hz conectadas al circuito de
# carga y descarga formado por transistores MOSFET
pin_Carga = PWM(Pin(18, Pin.OUT), freq=500, duty=0)  # Pin para la carga (GPIO18)
pin_Descarga = PWM(Pin(19, Pin.OUT), freq=500, duty=0)  # Pin para descarga (GPIO19)
# Configuramos el pin GPIO33 como entrada analógica para medir la tensión de la batería
adc_bat = ADC(Pin(33))
adc_bat.width(ADC.WIDTH_12BIT) # Resolución de 12 bits
adc_bat.atten(ADC.ATTN_11DB)  # Atenuación de 11 dB para medir tensiones mayores a 1100 mV (Vref)
# Configuramos el pin GPIO34 como entrada analógica para medir la tensión del termistor NTC, lo cual nos permite obtener la temperatura
adc_temp = ADC(Pin(34))
adc_temp.width(ADC.WIDTH_12BIT)  # Resolución de 12 bits
adc_temp.atten(ADC.ATTN_11DB)  # Atenuación de 11 dB
# Inicializamos los UARTs 1 y 2 a una velocidad de 115200 baudios
uart_cam = UART(1, baudrate=115200, tx=27, rx=26) # UART1 para la comunicación con el módulo ESP-CAM (TX1 = GPIO27, RX1 = GPIO26)
uart_tft = UART(2, baudrate=115200, tx=17, rx=16) # UART2 para la comunicación con la pantalla CYD (TX2 = GPIO17, RX2 = GPIO16)

# ========================
# ESTADOS DEL SISTEMA
# ========================
# ==== Variables bandera ====
estado_Carga = False
estado_Descarga = False
alerta_termica = False
wifi_conectado = False
estado_anterior_activo = False  # Indica si la batería estuvo inactiva o no (se usa para el cálculo de la capacidad)
modo_auto = False  # Indica si está activado el modo auto (uno o varios ciclos completos de carga/descarga)
# ==== Variables asociadas con la batería ====
bateria_qr_actual = "NINGUNA"  # Variable que guarda el QR de la batería
perfil_actual = "NiMH-7.2V"  # Tipo de batería
capacidad_ah = 0.0  # Capacidad (en Ah)
ultimo_calculo_ah = ticks_ms()  # Instante de tiempo en el que se hizo por última vez el cálculo de la capacidad
inicio_espera = 0  # Instante de tiempo para iniciar una demora de 15 minutos luego de la carga o la descarga
nivelPWM = 0  # Almacena el ciclo de actividad como un número entre 0 y 1023, el cual se pasa como argumento del método PWM.duty()
# Variable que guarda el estado del sistema en modo auto. Los posibles estados son IDLE, DESC1, WAIT1, CARGA, WAIT2 y DESC2
paso_auto = "IDLE"
# Variable que almacena la temperatura filtrada
temperatura_filtrada = None
# ==== Variables para la comunicación
buffer_uart_tft = ""  # Buffer que almacena los caracteres recibidos de la pantalla a través del UART2 (en formato string)
buffer_uart_cam = ""  # Buffer para guardar los caracteres recibidos de la cámara
espcam_ip_actual = "NINGUNA"  # Almacena la IP de la cámara
mqtt_client = None  # Objeto o instancia de la clase MQTTClient para configurar MQTT

# ========================
# FUNCIONES AUXILIARES
# ========================
# Función que establece la conexión con la red WiFi
def conectar_wifi():
    global wifi_conectado
    try:
        print(f"Conectando a {WIFI_SSID}...")
        # Creamos una instancia de la clase WLAN, seleccionando la interfaz estación
        wlan = network.WLAN(network.STA_IF)
        # Activamos la interfaz de red
        wlan.active(True)
        # Establecemos la conexión al punto de acceso
        wlan.connect(WIFI_SSID, WIFI_PASSWORD)
        intentos = 0  # Número de reintentos de conexión al AP
        # Intentamos conectarnos al AP con un número máximo de 20 intentos
        while not wlan.isconnected() and intentos < 20:
            sleep(0.5)  # Demora de 500 ms
            intentos += 1
        if wlan.isconnected():
            # Si el ESP32 logró conectarse al AP, mostramos en pantalla los parámetros de la interfaz de red
            # (dirección IP, máscara de subred, pasarela y servidor DNS)
            print("Wi-Fi Conectado:", wlan.ifconfig()[0])
            wifi_conectado = True
            return True
        else:
            # Si el ESP32 no pudo conectarse al AP, mostramos un mensaje de error
            print("Fallo al conectar Wi-Fi.")
            wifi_conectado = False
            return False
    except Exception as e:
        # Capturamos la excepción y la mostramos en pantalla
        print("Error Wi-Fi:", e)
        wifi_conectado = False
        return False

# Función que desconecta el dispositivo de la red WiFi
def desconectar_wifi():
    global wifi_conectado, mqtt_client
    try:
        # Desconectar MQTT primero si está activo
        if mqtt_client:
            desconectar_mqtt()
        # Creamos una instancia de la clase WLAN y realizamos la desconexión    
        wlan = network.WLAN(network.STA_IF)
        wlan.disconnect()
        wlan.active(False)
    except:
        pass # ===== Falta el manejo de la excepción =====
    wifi_conectado = False
    print("Wi-Fi desconectado.")

# Función que desconecta el dispositivo del broker MQTT
def desconectar_mqtt():
    global mqtt_client
    try:
        # Desconectamos el cliente MQTT del broker
        mqtt_client.disconnect()
    except:
        pass  # ===== Falta el manejo de la excepción =====
    mqtt_client = None
    print("MQTT desconectado.")

# --- NUEVA LÓGICA MQTT (RECIBIR CONFIG Y COMANDOS) ---
# Función callback que se ejecuta al recibir mensajes de los tópicos MQTT
def on_message_mqtt(topic, msg):
    global estado_Carga, estado_Descarga, modo_auto, paso_auto, perfil_actual, bateria_qr_actual, nivelPWM
    global PERFILES_LOCALES, VOLTAJE_CORTE_CARGA, VOLTAJE_CORTE_DESCARGA
    # Fijamos la codificación UTF-8 para los mensajes de los tópicos MQTT
    topic_str = topic.decode('utf-8')  # Nombre del tópico
    payload = msg.decode('utf-8')  # Payload (contenido de los mensajes )
    
    print(f"MQTT RECIBIDO [{topic_str}]: {payload}")
    
    if topic_str == MQTT_TOPIC_CONFIG:  
        # Datos recibidos del tema config_perfil, con formato: V_C:8.4,V_D:6.0
        try:
            # Guardamos los umbrales de tensión en un diccionario, en el cual las claves son los símbolos provenientes del mensaje
            datos = dict(item.split(":") for item in payload.split(","))
            # Convertimos los valores de tensión a float
            VOLTAJE_CORTE_CARGA = float(datos['V_C'])
            VOLTAJE_CORTE_DESCARGA = float(datos['V_D'])
            
            # Buscar perfil correspondiente para mantener sincronizada la pantalla
            perfil_actual = "PERSONALIZADO"
            # Recorremos el diccionario de perfiles y comparamos los umbrales de carga y descarga recibidos del
            # mensaje con los umbrales asignados a cada tipo de batería. Si la diferencia es menor a 0.05 V,
            # registramos el nombre de la nueva batería
            for name, (vc, vd) in PERFILES_LOCALES.items():
                if abs(VOLTAJE_CORTE_CARGA - vc) < 0.05 and abs(VOLTAJE_CORTE_DESCARGA - vd) < 0.05:
                    perfil_actual = name  # Actualizamos el perfil de la batería
                    break
            print(f"Perfil actualizado -> Carga: {VOLTAJE_CORTE_CARGA}V, Descarga: {VOLTAJE_CORTE_DESCARGA}V, Perfil: {perfil_actual}")
        except:
            print("Error parseando perfil")
 
    elif topic_str == MQTT_TOPIC_QR:
        # Datos recibidos del tópico bateria_qr
        # Quitamos los espacios al comienzo y al final del mensaje recibido
        nuevo_qr = payload.strip()
        if nuevo_qr and nuevo_qr != "NINGUNA":
            # Si se recibió un código QR y además es distinto de "NINGUNA", lo actualizamos
            bateria_qr_actual = nuevo_qr
            print(f"QR actualizado desde MQTT: {bateria_qr_actual}")
 
    elif topic_str == MQTT_TOPIC_COMANDOS:
        # Datos recibidos del tópico comandos (órdenes de control para iniciar/detener la carga/descarga)
        # Evaluamos la orden recibida y la ejecutamos, siempre y cuando no se haya recibido un aleta térmica
        if payload == "STOP_ALL":  # Detener todo el sistema
            # Actualizamos el estado del sistema
            modo_auto = False
            paso_auto = "IDLE"
            estado_Carga = False
            estado_Descarga = False
            # Ponemos en 0 el ciclo de actividad de las señales PWM de los pines de carga y descarga para detener
            # la carga o descarga
            pin_Carga.duty(0)
            pin_Descarga.duty(0)
            # Notificamos a la cámara para detener el escaneo si estuviese activo
            uart_cam.write("STOP\n")
        elif not alerta_termica:
            nivelPWM = 0
            if payload in ("START_SCAN", "SCAN"):  # Orden de escaneo remoto recibida desde MQTT
                uart_cam.write("SCAN\n")
                uart_tft.write("SCAN_STATUS:SCANNING\n")
                print("ORDEN DE ESCANEO ENVIADA A LA CÁMARA DESDE MQTT")
            elif payload == "START_C":  # Iniciar carga
                # Actualizamos el estado del sistema
                estado_Carga = True
                estado_Descarga = False
            elif payload == "START_D":  # Iniciar descarga
                estado_Descarga = True
                estado_Carga = False
            elif payload == "START_AUTO":  # Iniciar el modo auto (uno o dos ciclos completos de carga/descarga)
                modo_auto = True
                paso_auto = "DESC1"  # Estado de descarga (DESC1)
                estado_Descarga = True
                estado_Carga = False
                print("MODO AUTO INICIADO DESDE MQTT")

# Función para iniciar la comunicación MQTT
def iniciar_mqtt():
    global mqtt_client
    if not wifi_conectado:
        # Si el dispositivo no está conectado a una red WiFi, no podemos iniciar MQTT
        print("MQTT requiere Wi-Fi.")
        return False
    try:
        # Creamos una instancia de la clase MQTTClient para configurar la comunicación MQTT
        # Le pasamos al constructor el ID del cliente, el IP del broker, el puerto, el usuario y la contraseña
        mqtt_client = MQTTClient(MQTT_CLIENT_ID, MQTT_BROKER, port=MQTT_PORT, user=MQTT_USER, password=MQTT_PASS)
        # Establecemos un callback que se ejecutará cuando se reciban mensajes en los tópicos a los que se suscribe el ESP32 maestro
        mqtt_client.set_callback(on_message_mqtt)
        # Establecemos la conexión con el broker
        mqtt_client.connect()
        # Suscribimos el ESP32 maestro a los tópicos "config_perfil", "comandos" y "bateria_qr"
        mqtt_client.subscribe(MQTT_TOPIC_CONFIG)
        mqtt_client.subscribe(MQTT_TOPIC_COMANDOS)
        mqtt_client.subscribe(MQTT_TOPIC_QR)
        print("MQTT Suscrito a perfiles, comandos y QR.")
        return True
    except Exception as e:
        print("Error iniciando MQTT:", e)
        mqtt_client = None
        return False


# Función que lee la temperatura del NTC y aplica un filtro de ser necesario
def leer_temperatura():
    global temperatura_filtrada, V_REF, K
    # Declaramos constantes para el cálculo de la temperatura
    R_FIJA = 100000.0  # Resistencia que forma un divisor de tensión con el NTC (100k)
    T0 = 298.15  # Temperatura de 25°C (298.15 K)
    BETA_COEF = 3950.0  # Coeficiente beta del termistor
    R0 = 100000.0  # Resistencia del termisor a una temperatura de 25°C
    muestras = []  # Lista que almacena las muestras de temperatura
    for i in range(15):  # Promediamos 15 muestras tomadas cada 1 ms
        adc_val = adc_temp.read()  # Leemos el valor codificado del ADC (entre 0 y 4095)
        # Si el valor codificado está por debajo de 100 o por encima de 3995, lo descartamos
        if adc_val <= 100 or adc_val >= 3995: continue # Descartar picos/ruido extremo
        # Obtenemos la tensión sobre el termistor NTC, para lo cual debemos obtener primero la
        # tensión que muestrea el ADC, que es la tensión sobre el NTC con una atenuación de 12 dB
        v_ntc = (V_REF / K) * (adc_val / 4095.0)  # Tensión en mV
        # Calculamos la resistencia del NTC
        r_ntc = v_ntc * R_FIJA / (3300 - v_ntc)
        # Evitar logaritmo de cero o valores negativos
        if r_ntc <= 0: continue
        # Calculamos la temperatura en K a partir de la siguiente fórmula:
        # r_ntc = R0*e^(BETA*(1/temp_k - 1/T0))
        # temp_k = 1 / (1/T0 + 1/BETA * log(r_ntc/R0))
        temp_k = 1.0 / (1.0 / T0 + 1.0 / BETA_COEF * math.log(r_ntc / R0))
        # Guardamos el valor de temperatura en °C en la lista de muestras
        muestras.append(temp_k - 273.15)
        sleep(0.001)  # Demora de 1 ms
    
    if not muestras:
        # Si no se tomaron muestras, retornamos el valor anterior de temperatura, siempre y cuando 
        # esté definido; de lo contrario, retornamos un valor de -99°C (valor ficticio)
        return temperatura_filtrada if temperatura_filtrada is not None else -99.0
    # Calculamos la temperatura promedio
    raw_temp = sum(muestras) / len(muestras)
    
    # Filtro de Media Móvil Exponencial (EMA)
    if temperatura_filtrada is None:
        temperatura_filtrada = raw_temp
    else:
        alpha = 0.08 # Factor de suavizado (0.08 da alta estabilidad sin retrasar de más)
        temperatura_filtrada = (alpha * raw_temp) + ((1.0 - alpha) * temperatura_filtrada)
        
    return temperatura_filtrada

# ===================================================   
# BUCLE PRINCIPAL (Arranca en modo offline)
# ===================================================
def main():
    global V_REF, K, PERFILES_LOCALES
    print("Sistema iniciado en modo OFFLINE. Activar Wi-Fi/MQTT desde la pantalla.")
    # Obtenemos el instante de tiempo actual en ms, y lo tomamos como el instante en que se hizo
    # el último envío por MQTT
    ultimo_envio_mqtt = ticks_ms()

    while True:
        try:
            # Escuchar mensajes MQTT (Comandos y Perfiles)
            if mqtt_client:
                try:
                    # Chequeamos si hay algún mensaje pendiente del servidor, entregando el
                    # mensaje recibido al callback
                    mqtt_client.check_msg()
                except:
                    mqtt_client = None  # Desactivar MQTT roto, no crashear el loop

            # 1. LECTURA DE SENSORES (50 muestras con breve delay)
            tension_sum, corriente_sum = 0, 0
            for i in range(50):
                # Leemos el valor del ADC de tensión de batería y acumulamos
                tension_sum += adc_bat.read()
                corriente_sum += ina.current()
                sleep(0.002)  # Demora de 2 ms para dar respiro a los buses
            # Calculamos la tensión promedio escalada por el divisor resistivo del hardware (calibrado a 12.7V fondo de escala)
            v_bat_prom = (abs(tension_sum / 50.0) * 12.7) / 4095.0
            i_bat_prom = abs(corriente_sum / 50.0)  # i_bat_prom = abs(corriente_sum / 50.0) * 0.8
            # Calculamos la corrección de tensión que compensa dinámicamente las lecturas de la tensión en función de la 
            # caída de tensión introducida por shunts y cables durante la carga (-0.0001 V por mA) y descarga (+0.00015 V por mA)
            ajuste = -0.0001 if estado_Carga else (0.00015 if estado_Descarga else 0)
            v_bat_aj = v_bat_prom + (ajuste * i_bat_prom)
            # Leemos la temperatura del termistor NTC
            temperatura = leer_temperatura()

            # 2. LÓGICA DE SEGURIDAD
            if temperatura > TEMP_MAXIMA_SEGURA and not alerta_termica:
                # Si la temperatura supera el umbral de seguridad, cortamos la carga y descarga
                estado_Carga = False
                estado_Descarga = False
                pin_Carga.duty(0)
                pin_Descarga.duty(0)
                alerta_termica = True
                # Publicamos la alerta de sobretemperatura en el tópico "alerta_critica"
                if mqtt_client:
                    mqtt_client.publish(MQTT_TOPIC_ALERTA, f"SOBRETEMPERATURA:{temperatura:.1f}C")

            if alerta_termica and temperatura < (TEMP_MAXIMA_SEGURA - 5.0):
                # Si hubo una alerta térmica pero la temperatura está dentro de los límites seguros, cesamos el alerta
                alerta_termica = False

            # 3. ESCUCHAR A LA PANTALLA (UART2)
            # Verificamos si se recibieron caracteres en el UART2
            if uart_tft.any():  
                # Leemos los caracteres recibidos
                raw_tft = uart_tft.read()  # Devuelte un objeto de tipo bytes (con todos los caracteres recibidos)
                if raw_tft:
                    # Guardamos los caracteres en un buffer, con codificación UTF-8
                    try:
                        buffer_uart_tft += raw_tft.decode('utf-8')
                    except UnicodeError:
                        pass  # ===== Falta el manejo de la excepción =====
                    # Leemos uno por uno los comandos recibidos de la pantalla, los cuales están separados por el caracter de salto de línea (\n)
                    while "\n" in buffer_uart_tft:
                        # Separamos el buffer, con una sola separación a la vez
                        linea, buffer_uart_tft = buffer_uart_tft.split("\n", 1)
                        # Quitamos los espacios vacíos al comienzo y al final de la línea
                        comando_raw = linea.strip()
                        if not comando_raw:
                            continue  # Si el comando está vacío, pasamos a la siguiente iteración
                        
                        print(f"Recibido TFT: {comando_raw} (Tension: {v_bat_aj:.2f}V, Corte Desc: {VOLTAJE_CORTE_DESCARGA:.2f}V, Corte Carg: {VOLTAJE_CORTE_CARGA:.2f}V)")
                        if comando_raw == "GET_CONFIG":  # Comando para obtener la configuración
                            # Enviamos a la pantalla los parámetros de configuración de WiFi y MQTT
                            respuesta = f"VALUE_CONFIG:{WIFI_SSID},{WIFI_PASSWORD},{MQTT_BROKER},{MQTT_USER},{MQTT_PASS}\n"
                            uart_tft.write(respuesta)
                        
                        elif comando_raw == "GET_LIMITS":  # Comando para obtener los límites
                            # Enviamos a la pantalla los umbrales de tensión de carga y descarga, así como también la corriente objetivo
                            respuesta = f"VALUE_LIMITS:{perfil_actual},{VOLTAJE_CORTE_CARGA},{VOLTAJE_CORTE_DESCARGA},{CORRIENTE_OBJETIVO}\n"
                            uart_tft.write(respuesta)
                        
                        elif comando_raw.startswith("SET_WIFI:"):  # Comando para establecer nuevas credenciales de WiFi y MQTT
                            # Quitamos el comando del string recibido para leer los datos
                            datos = comando_raw.replace("SET_WIFI:", "").split(',')
                            # Se deben recibir al menos 3 datos: el SSID de la red WiFi (datos[0]), la contraseña (datos[1])
                            # y el IP del broker MQTT (datos[2])
                            if len(datos) >= 3:
                                # Actualizamos las credenciales con los valores recibidos
                                WIFI_SSID = datos[0]
                                WIFI_PASSWORD = datos[1]
                                MQTT_BROKER = datos[2]
                                # Creamos un diccionario para guardar la nueva configuración
                                nueva_config = {
                                    "ssid": datos[0],
                                    "pass": datos[1],
                                    "mqtt": datos[2]
                                }
                                # Opcionalmente, se pueden recibir 2 datos adicionales: el usuario MQTT (datos[3]) y la contraseña MQTT (datos[4])
                                if len(datos) >= 5:
                                    # Actualizamos las credenciales y las guardamos en el diccionario
                                    MQTT_USER = datos[3]
                                    MQTT_PASS = datos[4]
                                    nueva_config["m_user"] = datos[3]
                                    nueva_config["m_pass"] = datos[4]
                                # Guardamos la nueva configuración de WiFi y MQTT en el archivo config.json
                                try:
                                    with open('config.json', 'w') as f:
                                        json.dump(nueva_config, f)  # Convierte un diccionario a JSON
                                    print("Config guardada.")
                                    uart_cam.write("RESET\n") # Forzar reinicio de cámara para sincronizar
                                except:
                                    pass  # ===== Falta el manejo de la excepción =====
                        
                        # --- COMANDOS DE CONECTIVIDAD ON-DEMAND ---
                        elif comando_raw == "WIFI_ON":  # Encender el WiFi
                            # Llamamos a la función para establecer la conexión con el punto de acceso
                            if conectar_wifi():
                                # Si la conexión fue exitosa, enviamos un mensaje de éxito a la pantalla
                                uart_tft.write("WIFI_STATUS:OK\n")
                            else:
                                # Si la conexión falló, enviamos un mensaje de error a la pantalla
                                uart_tft.write("WIFI_STATUS:FAIL\n")
                        
                        elif comando_raw == "WIFI_OFF":  # Apagar el WiFi
                            # Llamamos a la función para desconectar el ESP32 del AP
                            desconectar_wifi()
                            # Enviamos un mensaje de confirmación a la pantalla
                            uart_tft.write("WIFI_STATUS:OFF\n")
                        
                        elif comando_raw == "MQTT_ON":  # Encender MQTT
                            if not wifi_conectado:
                                # Si el ESP32 no está conectado a la red WiFi, no podemos encender el MQTT
                                uart_tft.write("MQTT_STATUS:NO_WIFI\n")
                            elif iniciar_mqtt():
                                # Si el cliente MQTT fue iniciado correctamente, enviamos un mensaje de éxito a la pantalla
                                uart_tft.write("MQTT_STATUS:OK\n")
                            else:
                                # Si el cliente MQTT no se pudo inicializar, enviamos un mensaje de error
                                uart_tft.write("MQTT_STATUS:FAIL\n")
                        
                        elif comando_raw == "MQTT_OFF":  # Apagar MQTT
                            # Desconectamos el ESP32 del broker MQTT
                            desconectar_mqtt()
                            uart_tft.write("MQTT_STATUS:OFF\n")
                        
                        elif comando_raw.startswith("SET_PROFILE:"):  # Comando para establecer nuevos umbrales de batería
                            # Quitamos el comando del string recibido para leer los umbrales
                            perfil_nombre = comando_raw.replace("SET_PROFILE:", "")
                            if perfil_nombre in PERFILES_LOCALES:
                                # Si el nombre del perfil está dentro del diccionario de perfiles, actualizamos el perfil de batería actual
                                perfil_actual = perfil_nombre
                                # Leemos los umbrales de tensión de carga y descarga
                                VOLTAJE_CORTE_CARGA, VOLTAJE_CORTE_DESCARGA = PERFILES_LOCALES[perfil_nombre]
                                print(f"Perfil cambiado desde pantalla: {perfil_nombre}")
                        
                        elif comando_raw.startswith("SET_QR:"):  # Comando para ingresar el QR manualmente desde la pantalla
                            # Quitamos el comando del string recibido y los espacios al inicio y al final de la cadena
                            # para obtener el qr leído
                            qr_leido = comando_raw.replace("SET_QR:", "").strip()
                            if len(qr_leido) > 0:
                                # Actualizamos el QR de la batería
                                bateria_qr_actual = qr_leido
                                print(f"QR actualizado manualmente desde pantalla: {bateria_qr_actual}")
                                if mqtt_client:
                                    # Si el cliente MQTT está conectado al broker, publicamos el QR en el tópico bateria_qr
                                    try:
                                        mqtt_client.publish(MQTT_TOPIC_QR, bateria_qr_actual)
                                        print(f"QR enviado por MQTT: {bateria_qr_actual}")
                                    except Exception as e:
                                        print("Error al publicar QR por MQTT:", e)

                        elif comando_raw.startswith("SET_LIMITS:"):  # Comando para establecer los límites de corriente y tensión
                            # Quitamos el comando del string y separamos los valores recibidos
                            datos = comando_raw.replace("SET_LIMITS:", "").split(',')
                            try:
                                # Los parámetros recibidos son: umbral de tensión de carga (datos[0]), umbral de tensión de descarga (datos[1])
                                # y la corriente objetivo (datos[2]). Se puede enviar solamente la tensión de carga (1 parámetro), la tensión de carga 
                                # y descarga (2 parámetros), o la tensión de carga y descarga y la corriente objetivo (3 parámetros)
                                if len(datos) >= 1 and datos[0].strip():
                                    VOLTAJE_CORTE_CARGA = float(datos[0])
                                if len(datos) >= 2 and datos[1].strip():
                                    VOLTAJE_CORTE_DESCARGA = float(datos[1])
                                if len(datos) >= 3 and datos[2].strip():
                                    CORRIENTE_OBJETIVO = int(float(datos[2]))
                                
                                perfil_actual = "PERSONALIZADO"
                                # Recorremos el diccionario de perfiles y comparamos los umbrales de carga y descarga recibidos del
                                # mensaje con los umbrales asignados a cada tipo de batería. Si la diferencia es menor a 0.05 V,
                                # registramos el nombre de la nueva batería
                                for name, (vc, vd) in PERFILES_LOCALES.items():
                                    if abs(VOLTAJE_CORTE_CARGA - vc) < 0.05 and abs(VOLTAJE_CORTE_DESCARGA - vd) < 0.05:
                                        perfil_actual = name
                                        break
                                print(f"Limites personalizados guardados: Carga={VOLTAJE_CORTE_CARGA}V, Descarga={VOLTAJE_CORTE_DESCARGA}V, Corriente={CORRIENTE_OBJETIVO}mA, Perfil={perfil_actual}")
                            except ValueError as e:
                                print("Error al convertir limites numericos:", e)
                        
                        elif comando_raw == "STOP_ALL":  # Comando para detener todo el sistema
                            # Cortamos la carga y la descarga y actualizamos el estado del sistema
                            modo_auto = False
                            paso_auto = "IDLE"
                            estado_Carga = False
                            estado_Descarga = False
                            pin_Carga.duty(0)
                            pin_Descarga.duty(0)
                            # Notificamos a la cámara para detener el escaneo si estuviese activo
                            uart_cam.write("STOP\n")

                        elif comando_raw in ("START_SCAN", "SCAN_QR", "SCAN"):  # Comando para iniciar escaneo QR desde la pantalla
                            # Enviamos la orden 'SCAN' a la ESP32-CAM por UART1 y notificamos a la pantalla
                            uart_cam.write("SCAN\n")
                            uart_tft.write("SCAN_STATUS:SCANNING\n")
                            print("[SCAN] Solicitud de escaneo enviada a la ESP32-CAM")
                        
                        elif not alerta_termica:
                            nivelPWM = 0
                            if comando_raw == "START_C" and v_bat_aj < VOLTAJE_CORTE_CARGA:  # Comando para iniciar la carga
                                # Si la tensión de la batería está por debajo del umbral de carga, actualizamos el estado
                                # del sistema para iniciar la carga
                                estado_Carga = True
                                estado_Descarga = False
                            elif comando_raw == "START_D" and v_bat_aj > VOLTAJE_CORTE_DESCARGA:  # Comando para iniciar la descarga
                                # Si la tensión de la batería está por encima del umbral de descarga, actualizamos el estado
                                # del sistema para iniciar la descarga
                                estado_Descarga = True
                                estado_Carga = False
                            elif comando_raw == "START_AUTO":  # Iniciar el modo auto (uno o dos ciclos completos de carga/descarga)
                                # Actualizamos el estado del sistema
                                modo_auto = True
                                paso_auto = "DESC1"  # Estado de descarga (DESC1)
                                estado_Descarga = True  # Hacemos primero la descarga
                                estado_Carga = False
                                print("MODO AUTO INICIADO")

            # 4. ESCUCHAR A LA CÁMARA (UART1)
            """La cámara solicita datos WiFi al maestro por UART1 al iniciar, pero dado que la idea es que la propia cámara
            haga el descifrado de QR en lugar del back-end, no es necesario que se conecte a la red WiFi para enviar la trama
            JPEG al servidor. En su lugar, la cámara enviará el QR por puerto serie al ESP32 maestro """
            # Verificamos si se recibieron caracteres en el UART1
            if uart_cam.any():
                # Leemos los caracteres recibidos
                raw_cam = uart_cam.read()
                if raw_cam:
                    # Guardamos los caracteres en un buffer, con codificación UTF-8
                    try:
                        buffer_uart_cam += raw_cam.decode('utf-8')
                    except UnicodeError:
                        pass  # ===== Falta el manejo de la excepción =====
                    # Leemos uno por uno los comandos recibidos de la cámara, los cuales están separados por el caracter de salto de línea (\n)
                    while "\n" in buffer_uart_cam:
                        # Separamos el buffer, con una sola separación a la vez
                        linea, buffer_uart_cam = buffer_uart_cam.split("\n", 1)
                        # Quitamos los espacios vacíos al comienzo y al final de la línea
                        comando_cam = linea.strip()
                        if not comando_cam:
                            continue  # Si el comando está vacío, pasamos a la siguiente iteración
                        
                        print(f"Recibido CAM: {comando_cam}")
                        
                        if comando_cam.startswith("QR:"):  # Código QR recibido y decodificado por la cámara
                            # Extraemos el identificador del código QR recibido
                            nuevo_qr = comando_cam.replace("QR:", "").strip()
                            if nuevo_qr:
                                bateria_qr_actual = nuevo_qr
                                print(f"[CAM] QR decodificado recibido con éxito: {bateria_qr_actual}")
                                # Actualizamos inmediatamente el valor en la pantalla táctil CYD
                                uart_tft.write(f"SET_QR:{bateria_qr_actual}\n")
                                # Si el cliente MQTT está conectado, publicamos el nuevo QR en el tópico correspondiente
                                if mqtt_client:
                                    try:
                                        mqtt_client.publish(MQTT_TOPIC_QR, bateria_qr_actual)
                                        print(f"QR publicado en MQTT: {bateria_qr_actual}")
                                    except Exception as e:
                                        print("Error publicando QR en MQTT:", e)

                        elif comando_cam == "SCAN_TIMEOUT":  # Tiempo de espera agotado sin detectar QR
                            print("[CAM] Tiempo de escaneo agotado sin detección.")
                            uart_tft.write("SCAN_STATUS:TIMEOUT\n")

                        elif comando_cam.startswith("SCAN_STARTING"):  # Confirmación de inicio de escaneo
                            print("[CAM] Cámara iniciando escaneo.")
                            uart_tft.write("SCAN_STATUS:SCANNING\n")

                        elif comando_cam == "GET_CONFIG":  # Obtener la configuración de la red WiFi (compatibilidad)
                            # Enviamos las credenciales de la red WiFi a la cámara 
                            respuesta = f"VALUE_CONFIG:{WIFI_SSID},{WIFI_PASSWORD}\n"
                            uart_cam.write(respuesta)
                            print("Enviada configuracion de red a la ESP32-CAM.")
                            
                        elif comando_cam.startswith("IP:"):  # IP recibida de la cámara
                            # Quitamos el comando del string recibido, así como también los espacios al comienzo y al final para
                            # obtener la dirección IP de la cámara
                            ip_cam = comando_cam.replace("IP:", "").strip()
                            # Limpiar caracteres que no pertenezcan a una IP (números y puntos)
                            # Para eso iteramos sobre la cadena que guarda la IP recibida, y nos quedamos con aquellos caracteres
                            # que sean números o puntos
                            ip_cam = "".join(c for c in ip_cam if c.isdigit() or c == '.')
                            if len(ip_cam) > 7:
                                # Para que la IP sea correcta, la longitud debe ser como mínimo 7 (4 dígitos y 3 puntos)
                                print(f"ESP32-CAM reportó IP limpia: {ip_cam}")
                                # Actualizamos la IP de la cámara y la publicamos en el tópico espcam_ip
                                espcam_ip_actual = ip_cam
                                if mqtt_client:
                                    try:
                                        mqtt_client.publish("ESP32/espcam_ip", ip_cam)
                                        print("IP de la cámara publicada por MQTT.")
                                    except Exception as e:
                                        print("Error publicando IP de la cámara:", e)

            # 5. CONTROL DE POTENCIA (CARGA)
            # Si no hay alerta térmica, procedemos a la carga de la batería
            if estado_Carga and not alerta_termica:
                # Deshabilitamos la descarga poniendo en 0 el ciclo de actividad del pin de descarga
                pin_Descarga.duty(0)
                # Procedemos con la carga si la tensión de la batería está por debajo del umbral de carga
                if v_bat_aj <= VOLTAJE_CORTE_CARGA:
                    # Hacemos un control de la corriente sobre la batería modificando el ciclo de actividad
                    # de a 20 pasos sobre 1023 (ya que el método duty de la clase PWM recibe como parámetro
                    # un número que se divide por 1023 para obtener el ciclo de actividad). De esta manera,
                    # la corriente se mantiene cerca del valor objetivo, en un rango de 
                    # [CORRIENTE_OBJETIVO - 50 mA; CORRIENTE_OBJETIVO + 50 mA]
                    if i_bat_prom < (CORRIENTE_OBJETIVO - 50): 
                        nivelPWM += 20  # Si la corriente está por debajo del rango, aumentamos el ciclo de actividad
                    if i_bat_prom > (CORRIENTE_OBJETIVO + 50): 
                        nivelPWM -= 20  # Si la corriente está por encima del rango, bajamos el ciclo de actividad
                    # Ponemos un límite de 1000/1023 (97.75%) al ciclo de actividad
                    nivelPWM = max(0, min(1000, nivelPWM)) 
                    pin_Carga.duty(nivelPWM)
                else:
                    # Si la tensión de la batería está por encima del umbral de carga, deshabilitamos la carga
                    estado_Carga = False
                    pin_Carga.duty(0)
            else:
                # Si hay alerta térmica, deshabilitamos la carga
                pin_Carga.duty(0)

            # 6. CONTROL DE POTENCIA (DESCARGA)
            # Si no hay alerta térmica, procedemos a la descarga de la batería
            if estado_Descarga and not alerta_termica:
                # Deshabilitamos la carga poniendo en 0 el ciclo de actividad del pin de carga
                pin_Carga.duty(0)
                # Procedemos con la descarga si la tensión de la batería está por encima del umbral de descarga
                if v_bat_aj > VOLTAJE_CORTE_DESCARGA:
                    # Hacemos el mismo control de corriente que hacemos para la carga (es decir, variamos
                    # el ciclo de actividad hasta alcanzar la corriente objetivo)
                    if i_bat_prom < (CORRIENTE_OBJETIVO - 50): nivelPWM += 20
                    if i_bat_prom > (CORRIENTE_OBJETIVO + 50): nivelPWM -= 20
                    # Ponemos un límite de 1000/1023 (97.75%) al ciclo de actividad
                    nivelPWM = max(0, min(1000, nivelPWM))
                    pin_Descarga.duty(nivelPWM)
                else:
                    # Si la tensión de la batería está por debajo del umbral de carga, deshabilitamos la descarga
                    estado_Descarga = False
                    pin_Descarga.duty(0)
            else:
                # Si hay alerta térmica, deshabilitamos la descarga
                pin_Descarga.duty(0)

            # 6.5 LÓGICA MÁQUINA DE ESTADOS (MODO AUTO)
            # Esta máquina de estados permite implementar la secuencia de carga y descarga del modo auto
            if modo_auto:
                match paso_auto:
                    case "DESC1" if not estado_Descarga:  # Terminó DESC1
                        # Si terminó la descarga, pasamos al estado WAIT1 para esperar a que se enfríe la batería
                        # antes de proceder con la carga
                        paso_auto = "WAIT1"
                        inicio_espera = ticks_ms()  # Registramos el instante de tiempo en el que empieza la espera de 15 minutos
                        print("PASO 2: ESPERANDO ENFRIAMIENTO...")
                    case "WAIT1" if ticks_diff(ticks_ms(), inicio_espera) > TIEMPO_DESCANSO_MS:
                        # Si ya transcurrieron 15 minutos desde que terminó la descarga, pasamos al estado
                        # CARGA para iniciar la carga de la batería
                        paso_auto = "CARGA"
                        estado_Carga = True
                        nivelPWM = 0
                        print("PASO 3: INICIANDO CARGA...")
                    case "CARGA" if not estado_Carga:  # Terminó CARGA
                        # Si terminó la carga, pasamos al estado "WAIT2" para esperar a que se estabilice la batería
                        # antes de empezar una segunda descarga
                        paso_auto = "DESC2"
                        inicio_espera = ticks_ms()  # Registramos el instante de tiempo en el que empieza la espera de 15 minutos
                        print("PASO 4: ESPERANDO ESTABILIZACIÓN...")
                    case "WAIT2" if ticks_diff(ticks_ms(), inicio_espera) > TIEMPO_DESCANSO_MS:
                        # Si ya transcurrieron 15 minutos desde que terminó la carga, pasamos al estado
                        # DESC2 para iniciar la segunda descarga de la batería
                        paso_auto = "DESC2"
                        estado_Descarga = True
                        nivelPWM = 0
                        print("PASO 5: INICIANDO TEST FINAL...")
                    case "DESC2" if not estado_Descarga:  # Terminó DESC2
                        # Si terminó la segunda descarga, damos por finalizado el modo auto
                        modo_auto = False
                        paso_auto = "IDLE"
                        print("MODO AUTO COMPLETADO")

            # --- Cálculo de Capacidad (Ah) ---
            # Variable que indica si la batería actualmente se está cargando o descargando
            activo_actual = estado_Carga or estado_Descarga
            if activo_actual:
                # Si la batería está cargándose o descargándose pero el estado anterior era inactivo, no
                # hacemos el cálculo de la capacidad
                if not estado_anterior_activo:
                    capacidad_ah = 0.0  # Le asignamos un valor de 0
                    ultimo_calculo_ah = ticks_ms()  # Registramos el instante de tiempo para el cálculo de la capacidad
                else:
                    # Hallamos la diferencia de tiempo entre el instante actual y el instante anterior
                    t_act = ticks_ms()
                    dt = ticks_diff(t_act, ultimo_calculo_ah)
                    # Para calcular la capacidad, resolvemos una integral, es decir, sumamos la corriente instantánea
                    # multiplicada por un diferencial de tiempo
                    if dt > 0:
                        # corriente está en mA, integramos en Ah (Amperios-hora)
                        capacidad_ah += (i_bat_prom * dt) / 3600000000.0
                        ultimo_calculo_ah = t_act
            else:
                ultimo_calculo_ah = ticks_ms()
            # Actualizamos el estado de carga/descarga de la batería
            estado_anterior_activo = activo_actual

            # 7. TELEMETRÍA (Pantalla y MQTT)
            # Guardamos el estado del sistema
            estado_str = f"AUTO:{paso_auto}" if modo_auto else ("ERROR_TEMP" if alerta_termica else ("CARGANDO" if estado_Carga else ("DESCARGANDO" if estado_Descarga else "REPOSO")))
            
            # Enviamos Ah y RI a la pantalla también
            # Enviamos las variables a la pantalla en formato JSON
            json_tft = json.dumps({
                "v": round(v_bat_aj, 2),  # Tensión de la batería [V] (con 2 decimales)
                "i": round(i_bat_prom, 0),  # Corriente de la batería [mA]
                "t": round(temperatura, 1),  # Temperatura [°C]
                "st": estado_str,  # Estado del sistema
                "qr": bateria_qr_actual,  # Código QR de la batería
                "ah": round(capacidad_ah, 3),  # Capacidad de la batería [Ah] (con 3 decimales)
                "ri": 0
            })
            uart_tft.write(json_tft + "\n")
            # Publicamos las variables en el tópico MQTT "telemetria" cada 3 segundos
            if ticks_diff(ticks_ms(), ultimo_envio_mqtt) > 3000: # Enviamos más seguido (3s)
                # Enviamos las siguientes variables:
                # v: Tensión de la batería [V], i: Corriente de la batería [mA], t: Temperatura [°C]
                # s: Estado del sistema, qr: Código QR, ah: Capacidad [Ah], p: Perfil o tipo de batería
                # vc: Umbral de tensión de carga [V], vd: Umbral de tensión de descarga
                mensaje_mqtt = f"v:{v_bat_aj:.2f},i:{i_bat_prom:.0f},t:{temperatura:.1f},s:{estado_str},qr:{bateria_qr_actual},ah:{capacidad_ah:.3f},p:{perfil_actual},vc:{VOLTAJE_CORTE_CARGA:.2f},vd:{VOLTAJE_CORTE_DESCARGA:.2f}"
                if mqtt_client:
                    try:
                        mqtt_client.publish(MQTT_TOPIC_TELEMETRY, mensaje_mqtt)
                        print(f"DATOS ENVIADOS -> {mensaje_mqtt}") # <--- ¡EL CHISMOSO!
                        # Publicamos la IP de la cámara en el tópico correspondiente
                        if espcam_ip_actual != "NINGUNA":
                            mqtt_client.publish("ESP32/espcam_ip", espcam_ip_actual)
                    except:
                        iniciar_mqtt() # Reconectar si falló
                ultimo_envio_mqtt = ticks_ms()

            sleep(0.1)  # Demora de 100 ms para la ejecución del bucle
        
        except OSError as e:
            # Si el sensor INA219 o el bus I2C fallan por ruido eléctrico
            print("Fallo de lectura en sensor (Ruido I2C). Ignorando...")
            sleep(0.5) # Pausa breve antes de reintentar
            
        except BaseException as e:
            import sys
            print("--- ERROR CRÍTICO O INTERRUPCIÓN ---")
            sys.print_exception(e)
            # Deshabilitamos la carga y la descarga
            pin_Carga.duty(0)
            pin_Descarga.duty(0)
            sleep(2)  # Demora de 2 segundos
            # Opcional: machine.reset() si queres que se recupere solo

# Ejecutamos la función principal definida anteriormente si el nombre del archivo es main
if __name__ == "__main__":
    main()