from machine import Pin, I2C, PWM, ADC
from ina219 import INA219
from time import sleep
import network
from umqtt.simple import MQTTClient

# ========================
# CONFIGURACIÓN
# ========================
WIFI_SSID = 'manuadmin'
WIFI_PASSWORD = 'hola12345'
MQTT_BROKER = '192.168.0.181'
MQTT_PORT = 1884
MQTT_CLIENT_ID = 'Manu'
MQTT_TOPIC = 'ESP32'


# Configuración del bus I2C (ajustá los pines si usás otros)
i2c = I2C(0, scl=Pin(21), sda=Pin(22))
SHUNT_OHMS = 0.1  # valor típico del resistor shunt
ina = INA219(SHUNT_OHMS, i2c)
ina.configure()


# Pines de PWM
pin_Carga = Pin(19, Pin.OUT)  # MOSFET N: GPIO18
pin_Descarga = Pin(18, Pin.OUT)  # MOSFET P: GPIO19

# Inicialización segura en modo digital
pin_Carga.value(0)  # Gate del N-MOS apagado
pin_Descarga.value(0)  # Gate del P-MOS apagado
sleep(0.01)  # pequeña espera para estabilizar

# Convertir a PWM
# Frecuencia de 20 kHz, resolución de 8 bits (0-1023 en MicroPython)
pin_Carga = PWM(pin_Carga, freq=500, duty=0)
pin_Descarga = PWM(pin_Descarga, freq=500, duty=0)

# Pines para los botones (entrada con pull-up)
boton_Carga = Pin(17, Pin.IN, Pin.PULL_UP)
boton_Descarga = Pin(16, Pin.IN, Pin.PULL_UP)

# Estado actual (apagado al inicio)
estado_Carga = False
ultimo_estado_boton_Carga = 1  # Inicialmente no presionado (por pull-up)
estado_Descarga = False
ultimo_estado_boton_Descarga = 1  # Inicialmente no presionado (por pull-up)

pin_adc = ADC(Pin(33))

# Configura el ancho de resolución 12 bits (0-4095)
pin_adc.width(ADC.WIDTH_12BIT)
# Configura el rango de voltaje de referencia 
pin_adc.atten(ADC.ATTN_11DB)  # Para medir hasta 3.3V.  6.0v -> 1.26v y 9.6v -> 2.0v ; Valor máximo(4096) 16v->3.3v
                 
                 
# ========================
# CONECTAR A WIFI
# ========================
def conectar_wifi(ssid, password):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(ssid, password)
    print('Conectando a WiFi...', end='')
    while not wlan.isconnected():
        print('.', end='')
        sleep(1)
    print('\nConectado a WiFi:', wlan.ifconfig())


# ========================
# ENVIAR MENSAJE A MQTT
# ========================
def enviar_mqtt():
    client = MQTTClient(MQTT_CLIENT_ID, MQTT_BROKER, port=MQTT_PORT)
    client.connect()
    #print('Conectado al broker MQTT')
    client.publish(MQTT_TOPIC, MENSAJE)
    #print(f'Mensaje enviado: {MENSAJE}')
    client.disconnect()
    
    
    
    
    
# ===================================================    
# PROGRAMA PRINCIPAL
# ===================================================


conectar_wifi(WIFI_SSID, WIFI_PASSWORD)
contador = 0     # Contador para mandar por MQTT
nivelPWM = 0     # Porcentaje de trabajo del PWM
valor = 900      # Limitador de Corriente (en mA)
ajuste = 0       # Valor para ajustar la lectura de la tension
tensionPromedio = 0
corrientePromedio = 0

while True:
    try:
        tension = 0
        corriente = 0
        for i in range(100):              #Hacer varias lecturas para sacar el promedio
            tension += pin_adc.read()
            corriente += ina.current()
            
        tension = tension/100             # Hacer el promedio
        corriente = (corriente/100)*0.8   # Hacer el promedio y ajuste
        
        # Sacar los signos negativos ---
        if tension<0:
            tension=-tension
            
        if corriente<0:
            corriente=-corriente
        #-------------------------------
            
        if estado_Carga == False and estado_Descarga == False:
            ajuste=0
            
            
        tension = ((tension*12.7)/4095)+ajuste*corriente  # Hacer el ajuste para la carga o la descarga
        
        #a = filtrado_exponencial(a,tension)

        print(f"Tensión: {tension:.3f} V")
        print(f"Corriente: {corriente:.3f} mA")
        print("-----")
        
        MENSAJE = f"t{tension:.3f}c{corriente:.3f}" # Mensaje con los datos para mandar por MQTT
        
        # Leer el botones
        estado_boton_Carga = boton_Carga.value()
        estado_boton_Descarga = boton_Descarga.value()

        # Detectar boton de Carga--------------------------------------------------
        if ultimo_estado_boton_Carga == 1 and estado_boton_Carga == 0:
            nivelPWM = 0             # Reinicia para empezar de 0
            pin_Descarga.duty(0)     # Apaga la descarga
            estado_Descarga = False  # Apaga la descarga
            ajuste = -0.0001        # Ajuste para la tension durante la carga
            
            estado_Carga = not estado_Carga  # Cambiar el estado
            print("Carga:", "ENCENDIDO" if estado_Carga else "APAGADO")
            sleep(0.2)  # Pequeño retardo para evitar rebotes

        ultimo_estado_boton_Carga = estado_boton_Carga 
        #--------------------------------------------------------------------------
        
        
        # Detectar boton de Descarga----------------------------------------------- 
        if ultimo_estado_boton_Descarga == 1 and estado_boton_Descarga == 0:
            nivelPWM = 0          # Reinicia para empezar de 0
            pin_Carga.duty(0)     # Apaga la carga
            estado_Carga = False  # Apaga la carga
            ajuste = 0.00015      # Ajuste para la tension durante la descarga
            a = tension
            
            estado_Descarga = not estado_Descarga  # Cambiar el estado
            print("Descarga:", "ENCENDIDO" if estado_Descarga else "APAGADO")
            sleep(0.2)  # Pequeño retardo para evitar rebotes

        ultimo_estado_boton_Descarga = estado_boton_Descarga
        #--------------------------------------------------------------------------

        
        # CARGAR--------------------------------------------------------------------
        if estado_Carga == True:
            pin_Descarga.duty(0)       #Apaga la descarga antes de prender la carga
            
            if tension <= 9:           # En caso de haber mas de 9v corta la carga
                
                if corriente<valor-50: # Aumenta la corriente en caso de ser menos que la indicada
                    nivelPWM+=50
                    
                if corriente>valor+50: # Disminuye la corriente en caso de ser mayor que la indicada
                    nivelPWM-=50
                    
                if nivelPWM >1000:     # Limitador máximo
                    nivelPWM = 1000
                
                if nivelPWM <0:        # Limitador Mínimo
                    nivelPWM = 0
            
            else:
                nivelPWM = 0
                estado_Carga = False
                print("Carga completa")
            
            pin_Carga.duty(nivelPWM) # Prende el pin de carga con el porcentaje para tener la corriente indicada                
        else:
            pin_Carga.duty(0) # Apaga el pin en caso de que la carga se apague
        #---------------------------------------------------------------------------
        
        # DESCARGAR-----------------------------------------------------------------
        if estado_Descarga == True:
            pin_Carga.duty(0)          # Apaga la carga antes de prender la descarga
            
            if tension > 6:            # En caso de haber menos de 6v corta la descarga
                
                if corriente<valor-50: # Aumenta la corriente en caso de ser menos que la indicada
                    nivelPWM+=50
                    
                if corriente>valor+50: # Disminuye la corriente en caso de ser mayor que la indicada
                    nivelPWM-=50
                
                if nivelPWM >1000:     # Limitador máximo
                    nivelPWM = 1000
                
                if nivelPWM <0:        # Limitador Mínimo
                    nivelPWM = 0
            
            else:
                nivelPWM = 0
                estado_Descarga = False
                print("Descarga completa")
            
            pin_Descarga.duty(nivelPWM) # Prende el pin de carga con el porcentaje para tener la corriente indicada 
            
        
        else:
            pin_Descarga.duty(0) # Apaga el pin en caso de que la descarga se apague
        #--------------------------------------------------------------------------
        tensionPromedio += tension
        corrientePromedio += corriente
        
        if contador == 20: # Cada 10 segundo (20 ciclos) envia el dato por MQTT
            tensionPromedio = tensionPromedio/20
            corrientePromedio = corrientePromedio/20
            MENSAJE = f"t{tensionPromedio:.3f}c{corrientePromedio:.3f}" # Mensaje con los datos para mandar por MQTT
            enviar_mqtt()
            print("enviar mensaje: ",MENSAJE)
            contador=0
            tensionPromedio = 0
            corrientePromedio = 0
        
        
        contador+=1
        sleep(0.5)
        
    except Exception as e:
        print("Error:", e)
        sleep(2)
        
        
        
        