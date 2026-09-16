from machine import Pin, I2C, PWM, ADC, UART
from ina219 import INA219
from time import sleep, ticks_ms, ticks_diff
import network
import math
import json
import machine
from umqtt.simple import MQTTClient

# ========================
# LECTURA DE CONFIGURACIÓN (Wi-Fi y MQTT)
# ========================
def cargar_config():
    try:
        with open('config.json', 'r') as f:
            return json.load(f)
    except:
        return {"ssid": "Proyecto", "pass": "ligafederal", "mqtt": "10.187.200.199"}

config_actual = cargar_config()
WIFI_SSID = config_actual["ssid"]
WIFI_PASSWORD = config_actual["pass"]
MQTT_BROKER = config_actual["mqtt"]
MQTT_USER = config_actual.get("m_user", "admin")
MQTT_PASS = config_actual.get("m_pass", "baterias2026")

# ========================
# CONFIGURACIÓN GENERAL
# ========================
MQTT_PORT = 1884
MQTT_CLIENT_ID = 'Gonza'
MQTT_TOPIC_TELEMETRY = 'ESP32/telemetria'
MQTT_TOPIC_QR = 'ESP32/bateria_qr'
MQTT_TOPIC_ALERTA = 'ESP32/alerta_critica'
MQTT_TOPIC_CONFIG = 'ESP32/config_perfil' # Nuevo: Recibir perfiles
MQTT_TOPIC_COMANDOS = 'ESP32/comandos'     # Nuevo: Recibir comandos desde Web

# Limites de Operación Segura (NiMH 7.2V por defecto)
TEMP_MAXIMA_SEGURA = 45.0
VOLTAJE_CORTE_CARGA = 9.0
VOLTAJE_CORTE_DESCARGA = 6.0
CORRIENTE_OBJETIVO = 900

# ========================
# CONFIGURACIÓN DE HARDWARE
# ========================
i2c = I2C(0, scl=Pin(21), sda=Pin(22))
ina = INA219(i2c)
ina.configure()

pin_Carga = PWM(Pin(18, Pin.OUT), freq=500, duty=0) # Antes 19
pin_Descarga = PWM(Pin(19, Pin.OUT), freq=500, duty=0) # Antes 18

adc_bat = ADC(Pin(33))
adc_bat.width(ADC.WIDTH_12BIT)
adc_bat.atten(ADC.ATTN_11DB)

adc_temp = ADC(Pin(34))
adc_temp.width(ADC.WIDTH_12BIT)
adc_temp.atten(ADC.ATTN_11DB)

uart_cam = UART(1, baudrate=115200, tx=27, rx=26)
uart_tft = UART(2, baudrate=115200, tx=17, rx=16)

# ========================
# ESTADOS DEL SISTEMA
# ========================
estado_Carga = False
estado_Descarga = False
alerta_termica = False
bateria_qr_actual = "NINGUNA"
modo_auto = False
paso_auto = 0 # 0:IDLE, 1:DESC1, 2:WAIT1, 3:CARGA, 4:WAIT2, 5:DESC2
inicio_espera = 0
TIEMPO_DESCANSO_MS = 15 * 60 * 1000 # 15 minutos
wifi_conectado = False
capacidad_ah = 0.0
ultimo_calculo_ah = ticks_ms()
estado_anterior_activo = False
perfil_actual = "NiMH-7.2V"
buffer_uart_tft = ""
nivelPWM = 0
espcam_ip_actual = "NINGUNA"
mqtt_client = None
buffer_uart_cam = ""

# ========================
# FUNCIONES AUXILIARES
# ========================
def conectar_wifi():
    global wifi_conectado
    try:
        print(f"Conectando a {WIFI_SSID}...")
        wlan = network.WLAN(network.STA_IF)
        wlan.active(True)
        if not wlan.isconnected():
            wlan.connect(WIFI_SSID, WIFI_PASSWORD)
            intentos = 0
            while not wlan.isconnected() and intentos < 20: 
                sleep(0.5)
                intentos += 1
        if wlan.isconnected():
            print("Wi-Fi Conectado:", wlan.ifconfig()[0])
            wifi_conectado = True
            return True
        else:
            print("Fallo al conectar Wi-Fi.")
            wifi_conectado = False
            return False
    except Exception as e:
        print("Error Wi-Fi:", e)
        wifi_conectado = False
        return False

def desconectar_wifi():
    global wifi_conectado, mqtt_client
    try:
        # Desconectar MQTT primero si está activo
        if mqtt_client:
            desconectar_mqtt()
        wlan = network.WLAN(network.STA_IF)
        wlan.disconnect()
        wlan.active(False)
    except:
        pass
    wifi_conectado = False
    print("Wi-Fi desconectado.")

def desconectar_mqtt():
    global mqtt_client
    try:
        if mqtt_client:
            mqtt_client.disconnect()
    except:
        pass
    mqtt_client = None
    print("MQTT desconectado.")

# --- NUEVA LÓGICA MQTT (RECIBIR CONFIG Y COMANDOS) ---
def on_message_mqtt(topic, msg):
    global VOLTAJE_CORTE_CARGA, VOLTAJE_CORTE_DESCARGA, estado_Carga, estado_Descarga, modo_auto, paso_auto, perfil_actual, bateria_qr_actual, nivelPWM
    topic_str = topic.decode('utf-8')
    payload = msg.decode('utf-8')
    
    print(f"MQTT RECIBIDO [{topic_str}]: {payload}")
    
    if topic_str == MQTT_TOPIC_CONFIG:
        # Formato: V_C:8.4,V_D:6.0
        try:
            datos = dict(item.split(":") for item in payload.split(","))
            VOLTAJE_CORTE_CARGA = float(datos['V_C'])
            VOLTAJE_CORTE_DESCARGA = float(datos['V_D'])
            
            # Buscar perfil correspondiente para mantener sincronizada la pantalla
            matching_profile = "PERSONALIZADO"
            perfiles_locales = {
                "LI-ION-2S": (8.4, 6.0),
                "LI-ION-3S": (12.6, 9.0),
                "PB-12V": (14.4, 11.0),
                "NiMH-7.2V": (9.0, 6.0)
            }
            for name, (vc, vd) in perfiles_locales.items():
                if abs(VOLTAJE_CORTE_CARGA - vc) < 0.05 and abs(VOLTAJE_CORTE_DESCARGA - vd) < 0.05:
                    matching_profile = name
                    break
            perfil_actual = matching_profile
            
            print(f"Perfil actualizado -> Carga: {VOLTAJE_CORTE_CARGA}V, Descarga: {VOLTAJE_CORTE_DESCARGA}V, Perfil: {perfil_actual}")
        except:
            print("Error parseando perfil")
 
    elif topic_str == MQTT_TOPIC_QR:
        nuevo_qr = payload.strip()
        if nuevo_qr and nuevo_qr != "NINGUNA":
            bateria_qr_actual = nuevo_qr
            print(f"QR actualizado desde MQTT: {bateria_qr_actual}")
 
    elif topic_str == MQTT_TOPIC_COMANDOS:
        if payload == "STOP_ALL":
            modo_auto = False
            paso_auto = 0
            estado_Carga = False
            estado_Descarga = False
            pin_Carga.duty(0)
            pin_Descarga.duty(0)
        elif not alerta_termica:
            if payload == "START_C":
                estado_Carga = True
                estado_Descarga = False
                nivelPWM = 0
            elif payload == "START_D":
                estado_Descarga = True
                estado_Carga = False
                nivelPWM = 0
            elif payload == "START_AUTO":
                modo_auto = True
                paso_auto = 1
                estado_Descarga = True
                estado_Carga = False
                nivelPWM = 0
                print("MODO AUTO INICIADO DESDE MQTT")

def iniciar_mqtt():
    global mqtt_client
    if not wifi_conectado:
        print("MQTT requiere Wi-Fi.")
        return False
    try:
        mqtt_client = MQTTClient(MQTT_CLIENT_ID, MQTT_BROKER, port=MQTT_PORT, user=MQTT_USER, password=MQTT_PASS)
        mqtt_client.set_callback(on_message_mqtt)
        mqtt_client.connect()
        mqtt_client.subscribe(MQTT_TOPIC_CONFIG)
        mqtt_client.subscribe(MQTT_TOPIC_COMANDOS)
        mqtt_client.subscribe(MQTT_TOPIC_QR)
        print("MQTT Suscrito a perfiles, comandos y QR.")
        return True
    except Exception as e:
        print("Error iniciando MQTT:", e)
        mqtt_client = None
        return False

temperatura_filtrada = None

def leer_temperatura():
    global temperatura_filtrada
    muestras = []
    for _ in range(15): # Promediamos 15 lecturas
        adc_val = adc_temp.read()
        if adc_val <= 100 or adc_val >= 3995: continue # Descartar picos/ruido extremo
        r_fija = 100000.0
        # Evitar división por cero si adc_val es exactamente 4095 (controlado por el if de arriba, pero por las dudas)
        denom = 4095.0 - adc_val
        if denom == 0: continue
        r_ntc = r_fija * (adc_val / denom)
        t0, b_coef, r0 = 298.15, 3950.0, 100000.0
        # Evitar logaritmo de cero o valores negativos
        if r_ntc <= 0: continue
        temp_k = 1.0 / (1.0/t0 + (1.0/b_coef) * math.log(r_ntc/r0))
        muestras.append(temp_k - 273.15)
        sleep(0.001)
    
    if not muestras:
        return temperatura_filtrada if temperatura_filtrada is not None else -99.0
    
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
print("Sistema iniciado en modo OFFLINE. Activar Wi-Fi/MQTT desde la pantalla.")
ultimo_envio_mqtt = ticks_ms()

while True:
    try:
        # Escuchar mensajes MQTT (Comandos y Perfiles)
        if mqtt_client:
            try:
                mqtt_client.check_msg()
            except:
                mqtt_client = None  # Desactivar MQTT roto, no crashear el loop

        # 1. LECTURA DE SENSORES (50 muestras con breve delay)
        tension_sum, corriente_sum = 0, 0
        for _ in range(50):
            tension_sum += adc_bat.read()
            corriente_sum += ina.current()
            sleep(0.002) # Dar respiro a los buses
        
        tension_bruta = tension_sum / 50.0
        corriente_cruda = (corriente_sum / 50.0) * 0.8
        
        corriente = abs(corriente_cruda)
        ajuste = -0.0001 if estado_Carga else (0.00015 if estado_Descarga else 0)
        tension = ((abs(tension_bruta) * 12.7) / 4095) + (ajuste * corriente)
        
        temperatura = leer_temperatura()

        # 2. LÓGICA DE SEGURIDAD
        if temperatura > TEMP_MAXIMA_SEGURA and not alerta_termica:
            estado_Carga = False
            estado_Descarga = False
            pin_Carga.duty(0)
            pin_Descarga.duty(0)
            alerta_termica = True
            if mqtt_client:
                mqtt_client.publish(MQTT_TOPIC_ALERTA, f"SOBRETEMPERATURA:{temperatura:.1f}C")

        if alerta_termica and temperatura < (TEMP_MAXIMA_SEGURA - 5.0):
            alerta_termica = False

        # 3. ESCUCHAR A LA PANTALLA (UART2)
        if uart_tft.any():
            raw_tft = uart_tft.read()
            if raw_tft:
                try:
                    buffer_uart_tft += raw_tft.decode('utf-8')
                except UnicodeError:
                    pass
                
                while "\n" in buffer_uart_tft:
                    linea, buffer_uart_tft = buffer_uart_tft.split("\n", 1)
                    comando_raw = linea.strip()
                    if not comando_raw:
                        continue
                    
                    print(f"Recibido TFT: {comando_raw} (Tension: {tension:.2f}V, Corte Desc: {VOLTAJE_CORTE_DESCARGA:.2f}V, Corte Carg: {VOLTAJE_CORTE_CARGA:.2f}V)")
                    if comando_raw == "GET_CONFIG":
                        respuesta = f"VALUE_CONFIG:{WIFI_SSID},{WIFI_PASSWORD},{MQTT_BROKER},{MQTT_USER},{MQTT_PASS}\n"
                        uart_tft.write(respuesta)
                    
                    elif comando_raw == "GET_LIMITS":
                        respuesta = f"VALUE_LIMITS:{perfil_actual},{VOLTAJE_CORTE_CARGA},{VOLTAJE_CORTE_DESCARGA},{CORRIENTE_OBJETIVO}\n"
                        uart_tft.write(respuesta)
                    
                    elif comando_raw.startswith("SET_WIFI:"):
                        datos = comando_raw.replace("SET_WIFI:", "").split(',')
                        if len(datos) >= 3: 
                            WIFI_SSID = datos[0]
                            WIFI_PASSWORD = datos[1]
                            MQTT_BROKER = datos[2]
                            nueva_config = {
                                "ssid": datos[0],
                                "pass": datos[1],
                                "mqtt": datos[2]
                            }
                            if len(datos) >= 5:
                                MQTT_USER = datos[3]
                                MQTT_PASS = datos[4]
                                nueva_config["m_user"] = datos[3]
                                nueva_config["m_pass"] = datos[4]
                            try:
                                with open('config.json', 'w') as f:
                                    json.dump(nueva_config, f)
                                print("Config guardada.")
                                uart_cam.write("RESET\n") # Forzar reinicio de cámara para sincronizar
                            except:
                                pass
                    
                    # --- COMANDOS DE CONECTIVIDAD ON-DEMAND ---
                    elif comando_raw == "WIFI_ON":
                        if conectar_wifi():
                            uart_tft.write("WIFI_STATUS:OK\n")
                        else:
                            uart_tft.write("WIFI_STATUS:FAIL\n")
                    
                    elif comando_raw == "WIFI_OFF":
                        desconectar_wifi()
                        uart_tft.write("WIFI_STATUS:OFF\n")
                    
                    elif comando_raw == "MQTT_ON":
                        if not wifi_conectado:
                            uart_tft.write("MQTT_STATUS:NO_WIFI\n")
                        elif iniciar_mqtt():
                            uart_tft.write("MQTT_STATUS:OK\n")
                        else:
                            uart_tft.write("MQTT_STATUS:FAIL\n")
                    
                    elif comando_raw == "MQTT_OFF":
                        desconectar_mqtt()
                        uart_tft.write("MQTT_STATUS:OFF\n")
                    
                    elif comando_raw.startswith("SET_PROFILE:"):
                        perfil_nombre = comando_raw.replace("SET_PROFILE:", "")
                        perfiles_locales = {
                            "LI-ION-2S": (8.4, 6.0),
                            "LI-ION-3S": (12.6, 9.0),
                            "PB-12V": (14.4, 11.0),
                            "NiMH-7.2V": (9.0, 6.0)
                        }
                        if perfil_nombre in perfiles_locales:
                            perfil_actual = perfil_nombre
                            VOLTAJE_CORTE_CARGA, VOLTAJE_CORTE_DESCARGA = perfiles_locales[perfil_nombre]
                            print(f"Perfil cambiado desde pantalla: {perfil_nombre}")
                    
                    elif comando_raw.startswith("SET_QR:"):
                        qr_leido = comando_raw.replace("SET_QR:", "").strip()
                        if len(qr_leido) > 0:
                            bateria_qr_actual = qr_leido
                            print(f"QR actualizado manualmente desde pantalla: {bateria_qr_actual}")
                            if mqtt_client:
                                try:
                                    mqtt_client.publish(MQTT_TOPIC_QR, bateria_qr_actual)
                                    print(f"QR enviado por MQTT: {bateria_qr_actual}")
                                except Exception as e:
                                    print("Error al publicar QR por MQTT:", e)

                    elif comando_raw.startswith("SET_LIMITS:"):
                        datos = comando_raw.replace("SET_LIMITS:", "").split(',')
                        try:
                            if len(datos) >= 1 and datos[0].strip():
                                VOLTAJE_CORTE_CARGA = float(datos[0])
                            if len(datos) >= 2 and datos[1].strip():
                                VOLTAJE_CORTE_DESCARGA = float(datos[1])
                            if len(datos) >= 3 and datos[2].strip():
                                CORRIENTE_OBJETIVO = int(float(datos[2]))
                            
                            matching_profile = "PERSONALIZADO"
                            perfiles_locales = {
                                "LI-ION-2S": (8.4, 6.0),
                                "LI-ION-3S": (12.6, 9.0),
                                "PB-12V": (14.4, 11.0),
                                "NiMH-7.2V": (9.0, 6.0)
                            }
                            for name, (vc, vd) in perfiles_locales.items():
                                if abs(VOLTAJE_CORTE_CARGA - vc) < 0.05 and abs(VOLTAJE_CORTE_DESCARGA - vd) < 0.05:
                                    matching_profile = name
                                    break
                            perfil_actual = matching_profile
                            print(f"Limites personalizados guardados: Carga={VOLTAJE_CORTE_CARGA}V, Descarga={VOLTAJE_CORTE_DESCARGA}V, Corriente={CORRIENTE_OBJETIVO}mA, Perfil={perfil_actual}")
                        except ValueError as e:
                            print("Error al convertir limites numericos:", e)
                    
                    elif comando_raw == "STOP_ALL":
                        modo_auto = False
                        paso_auto = 0
                        estado_Carga = False
                        estado_Descarga = False
                        pin_Carga.duty(0)
                        pin_Descarga.duty(0)
                    
                    elif not alerta_termica:
                        if comando_raw == "START_C" and tension < VOLTAJE_CORTE_CARGA:
                            estado_Carga = True
                            estado_Descarga = False
                            nivelPWM = 0
                        elif comando_raw == "START_D" and tension > VOLTAJE_CORTE_DESCARGA:
                            estado_Descarga = True
                            estado_Carga = False
                            nivelPWM = 0
                        elif comando_raw == "START_AUTO":
                            modo_auto = True
                            paso_auto = 1 
                            estado_Descarga = True
                            estado_Carga = False
                            nivelPWM = 0
                            print("MODO AUTO INICIADO")

        # 4. ESCUCHAR A LA CÁMARA (UART1)
        if uart_cam.any():
            raw_cam = uart_cam.read()
            if raw_cam:
                try:
                    buffer_uart_cam += raw_cam.decode('utf-8')
                except UnicodeError:
                    pass
                
                while "\n" in buffer_uart_cam:
                    linea, buffer_uart_cam = buffer_uart_cam.split("\n", 1)
                    comando_cam = linea.strip()
                    if not comando_cam:
                        continue
                    
                    print(f"Recibido CAM: {comando_cam}")
                    
                    if comando_cam == "GET_CONFIG":
                        respuesta = f"VALUE_CONFIG:{WIFI_SSID},{WIFI_PASSWORD}\n"
                        uart_cam.write(respuesta)
                        print("Enviada configuracion de red a la ESP32-CAM.")
                        
                    elif comando_cam.startswith("IP:"):
                        ip_cam = comando_cam.replace("IP:", "").strip()
                        # Limpiar caracteres que no pertenezcan a una IP (números y puntos)
                        ip_cam = "".join(c for c in ip_cam if c.isdigit() or c == '.')
                        if len(ip_cam) > 7:
                            print(f"ESP32-CAM reportó IP limpia: {ip_cam}")
                            espcam_ip_actual = ip_cam
                            if mqtt_client:
                                try:
                                    mqtt_client.publish("ESP32/espcam_ip", ip_cam)
                                    print("IP de la cámara publicada por MQTT.")
                                except Exception as e:
                                    print("Error publicando IP de la cámara:", e)

        # 5. CONTROL DE POTENCIA (CARGA)
        if estado_Carga and not alerta_termica:
            pin_Descarga.duty(0)
            if tension <= VOLTAJE_CORTE_CARGA:
                if corriente < (CORRIENTE_OBJETIVO - 50): nivelPWM += 20
                if corriente > (CORRIENTE_OBJETIVO + 50): nivelPWM -= 20
                nivelPWM = max(0, min(1000, nivelPWM)) 
                pin_Carga.duty(nivelPWM)
            else:
                estado_Carga = False
                pin_Carga.duty(0)
        else:
            pin_Carga.duty(0)

        # 6. CONTROL DE POTENCIA (DESCARGA)
        if estado_Descarga and not alerta_termica:
            pin_Carga.duty(0)
            if tension > VOLTAJE_CORTE_DESCARGA:
                if corriente < (CORRIENTE_OBJETIVO - 50): nivelPWM += 20
                if corriente > (CORRIENTE_OBJETIVO + 50): nivelPWM -= 20
                nivelPWM = max(0, min(1000, nivelPWM))
                pin_Descarga.duty(nivelPWM)
            else:
                estado_Descarga = False
                pin_Descarga.duty(0)
        else:
            pin_Descarga.duty(0)

        # 6.5 LÓGICA MÁQUINA DE ESTADOS (MODO AUTO)
        if modo_auto:
            if paso_auto == 1 and not estado_Descarga: # Terminó DESC1
                paso_auto = 2
                inicio_espera = ticks_ms()
                print("PASO 2: ESPERANDO ENFRIAMIENTO...")
            
            elif paso_auto == 2:
                if ticks_diff(ticks_ms(), inicio_espera) > TIEMPO_DESCANSO_MS:
                    paso_auto = 3
                    estado_Carga = True
                    nivelPWM = 0
                    print("PASO 3: INICIANDO CARGA...")
            
            elif paso_auto == 3 and not estado_Carga: # Terminó CARGA
                paso_auto = 4
                inicio_espera = ticks_ms()
                print("PASO 4: ESPERANDO ESTABILIZACIÓN...")
            
            elif paso_auto == 4:
                if ticks_diff(ticks_ms(), inicio_espera) > TIEMPO_DESCANSO_MS:
                    paso_auto = 5
                    estado_Descarga = True
                    nivelPWM = 0
                    print("PASO 5: INICIANDO TEST FINAL...")
            
            elif paso_auto == 5 and not estado_Descarga: # Terminó DESC2
                modo_auto = False
                paso_auto = 0
                print("MODO AUTO COMPLETADO")

        # --- Cálculo de Capacidad (Ah) ---
        activo_actual = estado_Carga or estado_Descarga
        if activo_actual:
            if not estado_anterior_activo:
                capacidad_ah = 0.0
                ultimo_calculo_ah = ticks_ms()
            else:
                t_act = ticks_ms()
                dt = ticks_diff(t_act, ultimo_calculo_ah)
                if dt > 0:
                    # corriente está en mA, integramos en Ah (Amperios-hora)
                    capacidad_ah += (corriente * dt) / 3600000000.0
                    ultimo_calculo_ah = t_act
        else:
            ultimo_calculo_ah = ticks_ms()
        estado_anterior_activo = activo_actual

        # 7. TELEMETRÍA (Pantalla y MQTT)
        estado_str = "ERROR_TEMP" if alerta_termica else ("CARGANDO" if estado_Carga else ("DESCARGANDO" if estado_Descarga else "REPOSO"))
        
        # Enviamos Ah y RI a la pantalla también
        json_tft = json.dumps({
            "v": round(tension, 2),
            "i": round(corriente, 0),
            "t": round(temperatura, 1),
            "st": f"AUTO:{paso_auto}" if modo_auto else (estado_str),
            "qr": bateria_qr_actual,
            "ah": round(capacidad_ah, 3),
            "ri": 0
        })
        uart_tft.write(json_tft + "\n")

        if ticks_diff(ticks_ms(), ultimo_envio_mqtt) > 3000: # Enviamos más seguido (3s)
            mensaje_mqtt = f"v:{tension:.2f},i:{corriente:.0f},t:{temperatura:.1f},s:{estado_str},qr:{bateria_qr_actual},ah:{capacidad_ah:.3f},p:{perfil_actual},vc:{VOLTAJE_CORTE_CARGA:.2f},vd:{VOLTAJE_CORTE_DESCARGA:.2f}"
            if mqtt_client:
                try:
                    mqtt_client.publish(MQTT_TOPIC_TELEMETRY, mensaje_mqtt)
                    print(f"DATOS ENVIADOS -> {mensaje_mqtt}") # <--- ¡EL CHISMOSO!
                    if espcam_ip_actual != "NINGUNA":
                        mqtt_client.publish("ESP32/espcam_ip", espcam_ip_actual)
                except:
                    iniciar_mqtt() # Reconectar si falló
            ultimo_envio_mqtt = ticks_ms()

        sleep(0.1) 
    
    except OSError as e:
        # Si el sensor INA219 o el bus I2C fallan por ruido eléctrico
        print("Fallo de lectura en sensor (Ruido I2C). Ignorando...")
        sleep(0.5) # Pausa breve antes de reintentar
        
    except BaseException as e:
        import sys
        print("--- ERROR CRÍTICO O INTERRUPCIÓN ---")
        sys.print_exception(e)
        pin_Carga.duty(0)
        pin_Descarga.duty(0)
        sleep(2)
        # Opcional: machine.reset() si queres que se recupere solo