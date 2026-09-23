import time
import json  # Módulo para trabajar con archivos JSON
from datetime import datetime  # Módulo para trabajar con fecha y hora
import requests  # Biblioteca para realizar peticiones HTTP
import csv  # Biblioteca para trabajar con archivos CSV
# Importamos paho.mqtt, que es una biblioteca para trabajar con MQTT. Permite publicar mensajes 
# y suscribirse a tópicos MQTT
import paho.mqtt.client as mqtt
# Importamos componentes de SQLAlchemy, la cual es un kit de herramientas SQL para interacción con base de datos
from sqlalchemy import create_engine, Column, Integer, String, Float, DateTime
from sqlalchemy.orm import declarative_base, sessionmaker
# Importamos el cliente de InfluxDB para escribir datos en InfluxDB
from influxdb import InfluxDBClient
# Importamos componentes propios de FastAPI (framework para construir APIs con Python)
from fastapi import FastAPI, Response
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware

# --- CONFIGURACIÓN DE BASES DE DATOS ---
# 1. SQL (SQLite local por simplicidad, fácil de migrar a Postgres luego)
"""Esta base de datos se usa para llevar un registro de las baterías así como también de los ciclos de carga y descarga realizados. Los datos aquí guardados
son persistentes y atemporales, a diferencia de Influx DB """
# Creamos un motor, el cual es una fábrica que puede crear nuevas conexiones a la base de datos por nosotros
# Este motor se vincula al archivo de base de datos que está guardado en la carpeta backend
engine = create_engine('sqlite:///./baterias.db', connect_args={"check_same_thread": False})
# Creamos un objeto sesión (Session), el cual establece todas las conversaciones con la base de datos y representa
# una "zona de retención" para todos los objetos que han sido cargados o asociados con ella durante su ciclo de vida
# Asociamos la sesión con el motor creado anteriormente
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
# Creamos una clase que será utilizada como base para crear las clases asociadas a cada tabla
Base = declarative_base()

# Tablas SQL
# Cada tabla de la base de datos será una subclase de la clase Base. Estas clases actúan como construcciones modulares que forman las estructuras
# que consultaremos de la base de datos

# Tabla para almacenar los datos de cada batería
class Inventario(Base):
    __tablename__ = "baterias_inventario"  # Nombre de la tabla
    # Creamos las columnas de la tabla
    qr_id = Column(String, primary_key=True, index=True)  # QR de la batería (tipo String, clave primaria)
    fecha_registro = Column(DateTime, default=datetime.utcnow)  # Fecha de registro (tipo DateTime, valor por defecto = fecha y hora actual)
    estado_general = Column(String, default="OPERATIVO")  # Estado general (tipo String, valor por defecto = "OPERATIVO")

# Tabla para almacenar el historial de ciclos de carga y descarga
class HistorialCiclos(Base):
    __tablename__ = "historial_ciclos"  # Nombre de la tabla
    # Columnas de la tabla
    id = Column(Integer, primary_key=True, index=True)  # ID para identificar el número de ciclo (tipo Entero, clave primaria)
    qr_id = Column(String)  # QR de la batería (string)
    tipo_ciclo = Column(String)  # Tipo de ciclo (string)
    fecha_inicio = Column(DateTime, default=datetime.utcnow)  # Fecha y hora de inicio del ciclo (DateTime, por defecto fecha y hora actual)
    fecha_fin = Column(DateTime, nullable=True)  # Fecha y hora de fin del ciclo (DateTime)
    voltaje_final = Column(Float, nullable=True)  # Tensión de la batería al finalizar el ciclo (float)
    temp_maxima = Column(Float, default=0.0)  # Temperatura máxima de la batería (float, valor por defecto 0.0)
    capacidad_ah = Column(Float, default=0.0)  # Capacidad en Ah (float, valor por defecto 0.0)
    soh_porcentaje = Column(Float, default=0.0)  # SoH (estado de salud) de la batería, expresado como porcentaje (float, valor por defecto 0.0)
    resistencia_interna = Column(Float, default=0.0) # Resistencia interna en mOhm (float, valor por defecto 0.0)

# Creamos todas las tablas declaradas anteriormente, pasando como argumento el motor para acceder a la base de datos
Base.metadata.create_all(bind=engine)

# 2. InfluxDB (Telemetría en vivo)
"""Esta BD se usa para guardar los datos de la batería mientras se está cargando y descargando. Los datos guardados son temporales, y
cada uno de ellos tiene asociado una marca de tiempo (timestamp) """
try:
    # Creamos una instancia de InfluxDBClient con información sobre el servidor al que queremos acceder
    # El host InfluxDB correrá en el puerto 8086
    influx_client = InfluxDBClient(host='influxdb', port=8086)
    # Intentamos crear la DB, si falla (porque ya existe o no está listo) lo manejamos
    try:
        # Creamos una BD llamada "baterias_db"
        influx_client.create_database('baterias_db')
    except:
        pass  # ==== Falta el código para el manejo de la excepción ====
    # Configuramos el cliente para usar la base de datos
    influx_client.switch_database('baterias_db')
    print("Conectado a InfluxDB correctamente.")
except Exception as e:
    print(f"Aviso: No se pudo conectar a InfluxDB ({e}). Se omitirá el guardado histórico vivo.")
    influx_client = None

# --- ESTADO GLOBAL (Memoria temporal para el ciclo activo) ---
# Diccionario que almacena las variables que determinan el estado del sistema, así como también algunos parámetros
ciclo_activo = {
    "qr_actual": "NINGUNA",  # Código QR
    "estado_anterior": "REPOSO",  # Estado anterior
    "id_ciclo_sql": None,  # ID del ciclo de carga/descarga
    "temp_maxima": 0.0,  # Temperatura máxima
    "v_actual": 0.0,  # Tensión actual
    "i_actual": 0.0,  # Corriente actual
    "t_actual": 0.0,  # Temperatura actual
    "ah_acumulado": 0.0,  # Capacidad acumulada
    "ultima_lectura": None,  # Instante de tiempo de la última lectura
    "v_inicial": 0.0,  # Tensión inicial
    "i_inicial": 0.0,  # Corriente inicial
    "ri_calculada": 0.0,  # Resistencia interna
    "perfil_actual": "NiMH-7.2V",  # Perfil actual
    "cap_nominal": 6.5,  # Capacidad nominal
    "v_corte_carga": 9.0,  # Tensión de corte de carga
    "v_corte_descarga": 6.0,  # Tensión de corte de descarga
    "esp32_online": False  # Bandera que indica si se recibieron datos del ESP32
}

# --- TELEGRAM BOT (Placeholder) ---
import requests
TELEGRAM_TOKEN = "TU_TOKEN_AQUÍ"
TELEGRAM_CHAT_ID = "TU_CHAT_ID_AQUÍ"

def enviar_telegram(mensaje):
    print(f"TELEGRAM: {mensaje}")
    # url = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"
    # payload = {"chat_id": TELEGRAM_CHAT_ID, "text": mensaje}
    # requests.post(url, json=payload)

# --- LÓGICA MQTT ---
# Función callback que se ejecuta cuando el broker responde a nuestro pedido de conexión
# Recibe como parámetros la instancia de cliente, los datos de usuario, las banderas de conexión, y el código de razón de conexión
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        # Si el reason code (código de razón de conexión) es 0, significa que los datos fueron recibidos correctamente
        print(">>> CONECTADO A MQTT BROKER (Mosquitto) <<<")
        # Suscribimos el servidor a varios tópicos para recibir datos del ESP32
        client.subscribe("ESP32/telemetria")  # Telemetría (variables que representan el estado actual de la batería)
        client.subscribe("ESP32/bateria_qr")  # QR de la batería
    else:
        print(f">>> ERROR DE CONEXIÓN MQTT: Código {rc} <<<")

# Callback que se ejecuta cuando se recibe un mensaje en uno de los tópicos a los cuales se ha suscrito el cliente
# Recibe como parámetros la instancia del cliente, los datos del usuario y el mensaje
def on_message(client, userdata, msg):
    try:
        # Guardamos el tópico del mensaje y el contenido del mismo (payload)
        topic = msg.topic
        payload = msg.payload.decode('utf-8')  # Codificación utf-8
        print(f"MQTT RECIBIDO [{topic}]: {payload}")
        # Abrimos una sesión de la base de datos SQL
        db = SessionLocal()
        # Procesamos los datos de cada tópico
        if topic == "ESP32/bateria_qr":
            qr = payload.strip()  # Quitamos los espacios al comienzo y al final de la cadena
            if qr and qr != "NINGUNA":
                # Guardamos el código QR recibido en el diccionario
                ciclo_activo["qr_actual"] = qr
                # Verificamos si el QR ya está presente en la base de datos
                # Para hacer la consulta primero retornamos un objeto Query, luego realizamos un filtro mediante un criterio (en este caso qr_id == qr)
                # y devolvemos el primer resultado de la consulta
                bateria = db.query(Inventario).filter(Inventario.qr_id == qr).first()
                if not bateria:
                    # Si el QR es nuevo, lo registramos en el inventario
                    # Creamos una nueva instancia de la clase Inventario, para insertar un nuevo registro en esa tabla. En este registro,
                    # sólo guardamos el QR de la batería
                    nueva_bat = Inventario(qr_id=qr)
                    # Agregamos el registro a la tabla
                    db.add(nueva_bat)
                    # Método opcional, permite que los datos sean persistentes
                    db.commit()
                    print(f"Nueva batería registrada: {qr}")
        


        elif topic == "ESP32/telemetria":
            # Parseo rústico del mensaje (v:7.30,i:900,t:25.4,s:CARGANDO,qr:XYZ)
            try:
                # Guardamos los datos recibidos en un diccionario, en el cual las claves son los símbolos provenientes del mensaje
                # Primero separamos cada uno de los campos del payload, y luego recorremos los campos y construimos un diccionario en el
                # cual las claves serán los identificadores de los campos y los valores serán los valores de los campos
                datos = dict(item.split(":") for item in payload.split(","))
                # Extraemos del diccionario la corriente, la tensión, la temperatura y el estado actual
                v, i, t, estado = float(datos['v']), float(datos['i']), float(datos['t']), datos['s']
            except Exception as e:
                # Si hubo un error, cerramos la sesión de la base de datos
                print(f"Error parseando telemetría: {e}")
                db.close()
                return
            # Guardamos los valores recibidos en el diccionario
            ciclo_activo["v_actual"] = v
            ciclo_activo["i_actual"] = i
            ciclo_activo["t_actual"] = t
            # Actualizamos la bandera para indicar que recibimos datos del ESP32
            ciclo_activo["esp32_online"] = True
            # Si se recibieron el perfil y los umbrales de tensión, los guardamos en el diccionario
            if 'p' in datos:
                ciclo_activo["perfil_actual"] = datos['p']
            if 'vc' in datos:
                ciclo_activo["v_corte_carga"] = float(datos['vc'])
            if 'vd' in datos:
                ciclo_activo["v_corte_descarga"] = float(datos['vd'])

            ahora = datetime.utcnow()  # Obtenemos la fecha y hora actual (en formato UTC)
            # 1. Guardar siempre en InfluxDB
            if influx_client:
                try:
                    # Creamos una lista de diccionarios que contiene los puntos que serán escritos en la base de datos de InfluxDB
                    punto_influx = [{
                        "measurement": "telemetria_modulo",  # Medición
                        "tags": {"qr": ciclo_activo["qr_actual"]},  # Etiquetas (QR actual)
                        "fields": {"voltaje": v, "corriente": i, "temperatura": t}  # Campos (tensión, corriente y temperatura)
                    }]
                    # Insertamos datos en la BD de InfluxDB
                    influx_client.write_points(punto_influx)
                except Exception as e:
                    print(f"Error escribiendo en InfluxDB (Omitiendo): {e}")

            # 2. Lógica de Integración Ah (Trapecio simple)
            if estado != "REPOSO":
                # Si el estado es distinto de reposo, realizamos el cálculo de la capacidad mediante una integral
                if ciclo_activo["ultima_lectura"]:
                    # Calculamos el diferencial de tiempo entre el instante de tiempo actual y el instante de tiempo de la última lectura
                    dt_horas = (ahora - ciclo_activo["ultima_lectura"]).total_seconds() / 3600.0
                    # Realizamos el producto de la corriente instantánea por el diferencial de tiempo y vamos sumando esos productos
                    ciclo_activo["ah_acumulado"] += (abs(i) / 1000.0) * dt_horas
            ciclo_activo["ultima_lectura"] = ahora  # Actualizamos el instante de tiempo de la última lectura

            if t > ciclo_activo["temp_maxima"]:
                ciclo_activo["temp_maxima"] = t  # Actualizamos la temperatura máxima

            # Transición: Inicio de Ciclo
            if estado != "REPOSO" and ciclo_activo["estado_anterior"] == "REPOSO":
                # Si el sistema cambió de estado partiendo del reposo, debemos registrar un nuevo ciclo
                # Creamos una instancia de la clase HistorialCiclos, para insertar un nuevo registro en esa tabla
                # Este nuevo registro tendrá solamente 3 campos, ya que los restantes campos se agregan al finalizar el ciclo
                nuevo_ciclo = HistorialCiclos(
                    qr_id=ciclo_activo["qr_actual"],
                    tipo_ciclo=estado,
                    fecha_inicio=ahora
                )
                # Agregamos el registro a la tabla
                db.add(nuevo_ciclo)
                # Hacemos una confirmación de la transacción actual para que los datos persistan en la BD
                db.commit()
                # Recargamos los atributos del objeto de la base de datos, asegurando que el objeto refleje el estado más actual
                # Este método realiza una consulta SELECT para recargar los atributos del objeto
                db.refresh(nuevo_ciclo)
                # Actualizamos algunas variables que representan el estado del sistema
                ciclo_activo["id_ciclo_sql"] = nuevo_ciclo.id
                ciclo_activo["temp_maxima"] = t
                ciclo_activo["ah_acumulado"] = 0.0
                ciclo_activo["v_inicial"] = v
                ciclo_activo["i_inicial"] = i
                ciclo_activo["ri_calculada"] = 0.0 # Se calcula en el siguiente paso
                print(f"Iniciado ciclo {estado} en SQL.")

            # Cálculo de Resistencia Interna (Primeros segundos)
            # Se calcula la resistencia interna instantánea como la razón entre un diferencial de tensión (dv) y un diferencial de corriente (di)
            elif estado != "REPOSO" and ciclo_activo["ri_calculada"] == 0.0:
                di = abs(i - ciclo_activo["i_inicial"])  # Diferencial de tensión
                dv = abs(v - ciclo_activo["v_inicial"])  # Diferencial de corriente
                if di > 100: # Necesitamos un delta de corriente significativo (>100mA)
                    ciclo_activo["ri_calculada"] = (dv / (di / 1000.0)) * 1000.0 # mOhm
                    print(f"RI Calculada: {ciclo_activo['ri_calculada']:.1f} mOhm")

            # Transición: Fin de Ciclo
            elif estado == "REPOSO" and ciclo_activo["estado_anterior"] != "REPOSO":
                # Si el sistema pasó al estado de reposo, guardamos los parámetros y las variables de la batería en la BD de SQL
                if ciclo_activo["id_ciclo_sql"]:
                    # Hacemos una consulta a la tabla HistorialCiclos, filtramos mediante un criterio (el id debe coincidir con el ID del ciclo actual)
                    # y devolvemos el primer resultado de la consulta
                    ciclo = db.query(HistorialCiclos).filter(HistorialCiclos.id == ciclo_activo["id_ciclo_sql"]).first()
                    if ciclo:
                        # Agregamos al registro de la tabla los campos faltantes
                        ciclo.fecha_fin = ahora  # Fecha y hora de finalización del ciclo
                        ciclo.voltaje_final = v  # Tensión de la batería al finalizar el ciclo
                        ciclo.temp_maxima = ciclo_activo["temp_maxima"]  # Temperatura máxima
                        ciclo.capacidad_ah = ciclo_activo["ah_acumulado"]  # Capacidad acumulada
                        ciclo.resistencia_interna = ciclo_activo["ri_calculada"]  # Resistencia interna
                        
                        # Determinar si fue interrumpido o completado normalmente
                        v_corte_carga = ciclo_activo.get("v_corte_carga", 9.0)  # Valor por defecto de 9.0 V
                        v_corte_descarga = ciclo_activo.get("v_corte_descarga", 6.0)  # Valor por defecto de 6.0 V
                        tipo_anterior = ciclo_activo["estado_anterior"]
                        
                        fue_interrumpido = False  # Bandera que indica si el ciclo fue interrumpido o completado normalmente
                        if tipo_anterior == "CARGANDO":
                            # Si la tensión de la batería no alcanzó el umbral de corte de carga, quiere decir que el ciclo de carga fue interrumpido
                            if v < (v_corte_carga - 0.1):
                                fue_interrumpido = True
                        elif tipo_anterior == "DESCARGANDO":
                            # Si la tensión de la batería no llegó al umbral de corte de descarga, quiere decir que el ciclo de descarga fue interrumpido
                            if v > (v_corte_descarga + 0.1):
                                fue_interrumpido = True
                                
                        if fue_interrumpido:
                            # Si el ciclo fue interrumpido, guardamos el valor "Interrumpido" en lugar del tipo de ciclo
                            ciclo.tipo_ciclo = f"{tipo_anterior} (Interrumpido)"
                            ciclo.soh_porcentaje = 0.0  # El estado de salud (SoH) vale 0 ya que no se completó el ciclo
                        else:
                            # SoH Mejorado (Capacidad + Resistencia) se calcula sólo para descargas completadas normalmente
                            if tipo_anterior == "DESCARGANDO":
                                cap_nominal = ciclo_activo.get("cap_nominal", 2.5)  # Capacidad nominal (valor por defecto de 2.5 Ah)
                                ri_last = ciclo_activo.get("ri_calculada", 0.0)  # Último valor de resistencia interna (valor por defecto de 0.0)
                                # Calculamos la razón entre la capacidad acumulada y la capacidad nominal
                                r_cap = min(1.0, ciclo.capacidad_ah / cap_nominal)
                                r_ri = 1.0
                                if ri_last > 0:
                                    umbral_ri = 80.0 # mOhm donde empezamos a preocuparnos en NiMH
                                    r_ri = 1.0 / (1.0 + max(0, (ri_last - 20) / umbral_ri))
                                # Calculamos el SoH como un porcentaje
                                ciclo.soh_porcentaje = ((0.4 * r_cap) + (0.6 * r_ri)) * 100
                            else:
                                ciclo.soh_porcentaje = 0.0
                        # Hacemos una confirmación de la transacción actual para que los datos persistan en la BD
                        db.commit()
                        msg_telegram = f"Ciclo {ciclo.tipo_ciclo} Finalizado para {ciclo.qr_id}."
                        if not fue_interrumpido and tipo_anterior == "DESCARGANDO":
                            msg_telegram += f" Capacidad: {ciclo.capacidad_ah:.3f}Ah. SoH: {ciclo.soh_porcentaje:.1f}%"
                        enviar_telegram(msg_telegram)
                # Reestablecemos algunas variables de estado del sistema
                ciclo_activo["id_ciclo_sql"] = None  # ID del ciclo en la BD
                ciclo_activo["ah_acumulado"] = 0.0  # Capacidad acumulada
                ciclo_activo["temp_maxima"] = 0.0  # Temperatura máxima

            ciclo_activo["estado_anterior"] = estado
        # Cerramos la sesión de la base de datos
        db.close()
    except Exception as e:
        print(f"Error crítico en on_message: {e}")


# Intentamos configurar para paho-mqtt 2.x si está disponible, sino 1.x
# Creamos una instancia del cliente MQTT
try:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)  # Versión 2.0 de la API para callbacks
except AttributeError:
    mqtt_client = mqtt.Client()  # Versión 1.0 por defecto
# Establecemos los callbacks que se ejecutarán al establecer la conexión con el broker y al recibir mensajes
mqtt_client.on_connect = on_connect 
mqtt_client.on_message = on_message
# Establecemos un nombre de usuario y una contraseña para la autenticación con el broker
mqtt_client.username_pw_set("admin", "baterias2026")

# Conexión con reintentos para evitar caídas al inicio de Docker
# Función para establecer la conexión con el broker MQTT
def conectar_mqtt():
    reintentos = 0  # Número de reintentos de conexión al broker
    # Número máximo de 5 reintentos
    while reintentos < 5:
        try:
            # Establecemos la conexión con el broker Mosquitto, cuyo puerto es 1883. El máximo período entre comunicaciones
            # con el broker será de 60 segundos
            mqtt_client.connect("mosquitto", 1883, 60)
            # Iniciamos un hilo MQTT para mantener un flujo de tráfico de red con el broker
            # Es una llamada bloqueante que procesa el tráfico de red, despacha callbacks y maneja la reconexión 
            mqtt_client.loop_start()
            print("Hilo MQTT iniciado.")
            break
        except Exception as e:
            reintentos += 1
            print(f"Error conectando a MQTT (reintento {reintentos}/5): {e}")
            time.sleep(3)  # Esperamos 3 segundos para hacer un nuevo reintento

conectar_mqtt()

# --- API WEB (Para controlar desde la PC a futuro) ---
# Creamos una instancia de FastAPI, con el título Controlador de baterías
app = FastAPI(title="Controlador de Baterías")
# Agregamos el middleware llamado "CORSMiddleware" para crear una lista de orígenes permitidos (es decir, URLs que tendrán permitido acceder al back-end)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Permitimos todos los orígenes
    allow_methods=["*"],  # Permitimos todos los métodos HTTP
    allow_headers=["*"],  # Permitimos todos los encabezados HTTP
)
"""Para crear las rutas, utilizamos el denominado "decorador" (@), el cual es una función que toma como entrada otra función y retorna otra función
En este caso, las funciones decoradoras son las que crean las rutas, y toman las funciones que están justo abajo, las cuales se encargan de 
manejar las peticiones a esas rutas"""
# Creamos la ruta /comando/{accion} y manejamos las peticiones POST a esa ruta
# Esta ruta permite realizar alguna acción desde la interfaz web
@app.post("/comando/{accion}")
def enviar_comando_esp32(accion: str):
    """Acciones permitidas: START_C, STOP_C, START_D, STOP_D"""
    # Publicamos el comando en el tópico "ESP32/comandos"
    mqtt_client.publish("ESP32/comandos", accion)
    # Retornamos el estado y el comando
    return {"status": "Comando enviado", "accion": accion}
# Creamos la ruta /set_qr y manejamos las peticiones POST a esa ruta
# Esta ruta permite publicar el QR desde la interfaz web
@app.post("/set_qr")
def set_qr_manual(qr: str):
    """Asigna manualmente el QR de la batería activa"""
    qr = qr.strip()  # Eliminamos los espacios al comienzo y al final de la cadena
    if not qr or len(qr) < 2:
        # Si no se envió el QR o es inválido (menos de 2 caracteres), devolvemos un mensaje de error
        return {"status": "Error", "msg": "QR inválido"}
    # Actualizamos el QR actual
    ciclo_activo["qr_actual"] = qr
    # Notificar al ESP32 y registrar en inventario
    # Publicamos el QR en el tópico correspondiente
    mqtt_client.publish("ESP32/bateria_qr", qr)
    # Abrimos una sesión de la base de datos SQL
    db = SessionLocal()
    # Realizamos una consulta a la tabla Inventario, filtramos el QR que acaba de ser publicado, y devolvemos el primer resultado de la consulta
    bateria = db.query(Inventario).filter(Inventario.qr_id == qr).first()
    # Si la batería no está en la base de datos, la guardamos
    if not bateria:
        # Si el QR es nuevo, lo registramos en el inventario
        # Creamos una nueva instancia de la clase Inventario, para insertar un nuevo registro en esa tabla. En este registro,
        # sólo guardamos el QR de la batería
        nueva_bat = Inventario(qr_id=qr)
        db.add(nueva_bat)  # Insertamos un nuevo registro en la tabla
        db.commit()  # Confirmamos los cambios para que los datos persistan en la BD
    # Cerramos la sesión de la base de datos
    db.close()
    # Retornamos el estado y el código QR
    return {"status": "QR asignado", "qr": qr}

# Endpoint para disparar el escaneo físico de código QR en la ESP32-CAM
@app.post("/escanear")
def disparar_escaneo():
    """Envía la orden de escaneo al ESP32 Master vía MQTT para activar la ESP32-CAM"""
    mqtt_client.publish("ESP32/comandos", "START_SCAN")
    return {"status": "Comando de escaneo enviado a la ESP32-CAM"}

@app.get("/bateria/{qr_id}")
def detalle_bateria(qr_id: str):
    """Resumen completo de una batería: inventario + historial de ciclos"""
    # Abrimos una sesión de la base de datos SQL
    db = SessionLocal()
    # Realizamos una consulta a la tabla Inventario, filtramos el QR que se encuentra en la ruta, y devolvemos el primer resultado de la consulta
    bateria = db.query(Inventario).filter(Inventario.qr_id == qr_id).first()
    if not bateria:
        # Si no se encontró la batería en la BD, cerramos la sesión y devolvemos un mensaje de error
        db.close()
        return {"status": "Error", "msg": "Batería no encontrada"}
    # Realizamos una consulta a la tabla HistorialCiclos, filtramos el QR que se encuentra en la ruta, ordenamos los registros de la tabla por
    # fecha de inicio descendente y devolvemos los resultados como una lista
    ciclos = db.query(HistorialCiclos).filter(
        HistorialCiclos.qr_id == qr_id
    ).order_by(HistorialCiclos.fecha_inicio.desc()).all()
    # Cerramos la sesión de la BD
    db.close()
    # Retornamos un diccionario con los valores obtenidos de la base de datos
    return {
        "qr_id": bateria.qr_id,  # ID del QR
        "fecha_registro": bateria.fecha_registro,  # Fecha de registro de la batería
        "estado_general": bateria.estado_general,  # Estado general de la batería
        "total_ciclos": len(ciclos),  # Número total de ciclos de carga y descarga
        # Por cada ciclo retornamos una lista con las variables que representan el estado de la batería, para lo cual
        # iteramos sobre la lista de ciclos obtenida de la BD
        "ciclos": [
            {
                "id": c.id,  # ID del ciclo
                "tipo_ciclo": c.tipo_ciclo,  # Tipo de ciclo
                "fecha_inicio": c.fecha_inicio,  # Fecha de inicio del ciclo
                "fecha_fin": c.fecha_fin,  # Fecha de finalización del ciclo
                "voltaje_final": c.voltaje_final,  # Tensión final de la batería
                "temp_maxima": c.temp_maxima,  # Temperatura máxima
                "capacidad_ah": c.capacidad_ah,  # Capacidad
                "soh_porcentaje": c.soh_porcentaje,  # Porcentaje de SoH
                "resistencia_interna": c.resistencia_interna  # Resistencia interna
            } for c in ciclos
        ]
    }
# Creamos la ruta /baterias y manejamos las peticiones GET a esa ruta
# Esta ruta permite obtener todas las baterías registradas
@app.get("/baterias")
def listar_baterias():
    # Abrimos una sesión de la base de datos SQL
    db = SessionLocal()
    # Realizamos una consulta a la tabla Inventario para obtener todas las baterías
    baterias = db.query(Inventario).all()
    resultado = []
    # Iteramos sobre la lista de baterías
    for b in baterias:
        # Hacemos una consulta a la tabla HistorialCiclos, filtramos por ID y contamos el número de ciclos
        count = db.query(HistorialCiclos).filter(HistorialCiclos.qr_id == b.qr_id).count()
        # Agregamos un elemento a la lista que luego retornamos
        # Cada elemento de esta lista será un diccionario con parámetros de cada batería
        resultado.append({
            "qr_id": b.qr_id,  # ID del QR
            "fecha_registro": b.fecha_registro,  # Fecha de registro
            "estado_general": b.estado_general,  # Estado general
            "ciclos_count": count  # Número de ciclos de carga y descarga
        })
    # Cerramos la sesión de la BD
    db.close()
    # Retornamos la lista con los parámetros de cada batería
    return resultado

# Creamos la ruta /historial y manejamos las peticiones GET a esa ruta
# Esta ruta permite obtener el historial de todos los ciclos de carga y descarga 
@app.get("/historial")
def listar_historial():
    # Abrimos una sesión de la base de datos SQL
    db = SessionLocal()
    # Realizamos una consulta a la tabla HistorialCiclos, ordenamos los registros de la tabla por fecha de inicio descendente, limitamos el
    # número de registros a 50 y devolvemos los resultados como una lista
    historial = db.query(HistorialCiclos).order_by(HistorialCiclos.fecha_inicio.desc()).limit(50).all()
    # Cerramos la sesión de la BD
    db.close()
    return historial  # Retornamos la lista obtenida de la BD

# Creamos la ruta /historial/{qr_id} y manejamos las peticiones GET a esa ruta
# Esta ruta es similar a la anterior, con la diferencia de que devuelve el historial de ciclos de una sola batería
@app.get("/historial/{qr_id}")
def historial_por_qr(qr_id: str):
    # Abrimos una sesión de la base de datos SQL
    db = SessionLocal()
    # Realizamos una consulta a la tabla HistorialCiclos, filtramos por QR, ordenamos los registros de la tabla por fecha de inicio descendente 
    # y devolvemos los resultados como una lista    
    historial = db.query(HistorialCiclos).filter(HistorialCiclos.qr_id == qr_id).order_by(HistorialCiclos.fecha_inicio.desc()).all()
    # Cerramos la sesión de la BD y retornamos la lista obtenida
    db.close()
    return historial

# Creamos la ruta /perfil y manejamos las peticiones POST a esa ruta
# Esta ruta permite agregar un perfil de batería nuevo
@app.post("/perfil")
def set_perfil(tipo: str):
    """Perfiles: LI-ION-2S, LI-ION-3S, PB-12V"""
    config = {
        "LI-ION-2S": {"v_c": 8.4, "v_d": 6.0},
        "LI-ION-3S": {"v_c": 12.6, "v_d": 9.0},
        "PB-12V": {"v_c": 14.4, "v_d": 11.0},
        "NiMH-7.2V": {"v_c": 9.0, "v_d": 6.0} # Carga máx ~1.5V/celda, Descarga ~1.0V/celda (6 celdas)
    }
    # Buscamos el perfil en la lista de perfiles permitidos
    if tipo in config:
        # Guardamos el tipo de batería
        ciclo_activo["perfil_actual"] = tipo
        # Construimos el payload que se publicará en el tópico "ESP32/config_perfil" para que el ESP32 pueda acceder al nuevo perfil
        # El payload contendrá la tensión de corte de carga y la tensión de corte de descarga
        payload = f"V_C:{config[tipo]['v_c']},V_D:{config[tipo]['v_d']}"
        mqtt_client.publish("ESP32/config_perfil", payload)
        # Devolvemos un mensaje de éxito
        return {"status": "Perfil enviado", "perfil": tipo}
    # Si no se encontró el perfil, devolvemos un mensaje de error
    return {"status": "Error", "msg": "Perfil no encontrado"}

# Ruta para fijar la capacidad nominal de una determinada batería
@app.post("/set_nominal")
def set_capacidad_nominal(valor: float):
    # Actualizamos la capacidad nominal
    ciclo_activo["cap_nominal"] = valor
    return {"status": "Capacidad nominal actualizada", "valor": valor}

# Ruta para exportar un archivo CSV que contenga el historial de ciclos
@app.get("/exportar")
def exportar_csv():
    # Abrimos una sesión de la base de datos SQL y obtenemos todos los registros de la tabla HistorialCiclos
    db = SessionLocal()
    historial = db.query(HistorialCiclos).all()
    # Cerramos la sesión de la BD
    db.close()
    """Creamos un flujo de texto en memoria, es decir, algo que se comporta como un archivo de texto, pero
    que en realidad está en la RAM. Esto evita la necesidad de crear un archivo físico almacenado en el
    disco, ya que en este caso el archivo CSV será solicitado por el usuario para su descarga """
    output = io.StringIO()
    # Retornamos un objeto para escribir un archivo CSV, pasándole a la función csv.writer un objeto al que
    # pueda escribir texto de manera similiar a un archivo (en este caso, el flujo de texto en memoria)
    writer = csv.writer(output)
    # Escribimos los encabezados de la tabla
    writer.writerow(["ID", "QR", "Tipo", "Inicio", "Fin", "V Final", "Temp Max", "Capacidad Ah", "SoH %", "RI mOhm"])
    # Recorremos la tabla obtenida de la BD y escribimos los registros en cada fila del archivo
    for c in historial:
        writer.writerow([c.id, c.qr_id, c.tipo_ciclo, c.fecha_inicio, c.fecha_fin, c.voltaje_final, c.temp_maxima, c.capacidad_ah, c.soh_porcentaje, c.resistencia_interna])
    # Volvemos el cursor al inicio
    output.seek(0)
    # Devolvemos una respuesta de tipo Streaming (flujo) para poder descargar el archivo .csv
    # El encabezado "Content-Disposition" indica si el contenido debe ser mostrado en línea (inline) en el navegador como parte de una página
    # web o debe ser descargado como attachment localmente. En este caso, el contenido debe ser descargado
    return StreamingResponse(output, media_type="text/csv", headers={"Content-Disposition": "attachment; filename=reporte_baterias.csv"})

"""Montamos una subaplicación llamada /web dentro de la aplicación principal. En este caso, esta subaplicación nos permitirá servir 
archivos estáticos mediante StaticFiles (index.html). Esto es útil en proyectos donde FastAPI funciona como backend y al mismo tiempo entrega
el frontend """
app.mount("/web", StaticFiles(directory="frontend"), name="frontend")

# Ruta correspondiente a la página principal 
@app.get("/")
def serve_home():
    # Devolvemos un archivo como respuesta (en este caso, el archivo correspondiente a la página principal)
    return FileResponse("frontend/index.html")

# Ruta para acceder al estado actual del sistema
@app.get("/estado_actual")
def obtener_estado(response: Response):
    """ Fijamos los encabezados de la respuesta """
    # Encabezado que almacena directivas que controlan el caching (almacenamiento de una respuesta asociada a una petición para su
    # posterior reutilización). Las directivas utilizadas en este caso son:
    # - no-cache: Almacena una respuesta que debe ser revalidada antes de reutilizarse
    # - no-store: Indica que cualquier cache de cualquier tipo no deben almacenar esta respuesta
    # - must-revalidate: Indica que la respuesta puede ser guarda en caches y puede ser reutilizada mientras esté "fresca" mientras que debe ser validada si ya expiró
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    # Encabezado que sirve para compatibilidad con caches HTTP/1.0 que no soportan el header Cache-Control de HTTP/1.1
    response.headers["Pragma"] = "no-cache"
    # Encabezado que contiene la fecha y hora después de la cual la respuesta se considera expirada en el contexto de HTTP caching.
    # El valor 0 se usa para representar una fecha en el pasado, indicando que el recurso ya ha expirado
    response.headers["Expires"] = "0"
    # Si hace más de 7 segundos que no recibimos telemetría, ponemos valores en 0
    if ciclo_activo["ultima_lectura"]:
        # Diferencia entre el instante de tiempo actual y el instante de tiempo de la última lectura en segundos
        delta = (datetime.utcnow() - ciclo_activo["ultima_lectura"]).total_seconds()
        if delta > 7.0: # 7 segundos de gracia
            # Ponemos todos los valores en 0
            ciclo_activo["v_actual"] = 0.0
            ciclo_activo["i_actual"] = 0.0
            ciclo_activo["t_actual"] = 0.0
            ciclo_activo["esp32_online"] = False  # Indica que se perdió la conexión con el ESP32
            # Si estaba cargando o descargando, al perder conexión pasa a REPOSO
            if ciclo_activo["estado_anterior"] != "REPOSO":
                # Abrimos una sesión de la base de datos SQL
                db = SessionLocal()
                if ciclo_activo["id_ciclo_sql"]:
                    # Realizamos una consulta a la tabla HistorialCiclos, filtramos por el ID del ciclo actual y devolvemos el primer resultado de la consulta
                    ciclo = db.query(HistorialCiclos).filter(HistorialCiclos.id == ciclo_activo["id_ciclo_sql"]).first()
                    if ciclo:
                        # Actualizamos el registro con los datos del ciclo actual
                        ciclo.fecha_fin = datetime.utcnow()
                        ciclo.voltaje_final = 0.0
                        ciclo.temp_maxima = ciclo_activo["temp_maxima"]
                        ciclo.capacidad_ah = ciclo_activo["ah_acumulado"]
                        ciclo.resistencia_interna = ciclo_activo["ri_calculada"]
                        ciclo.tipo_ciclo = f"{ciclo_activo['estado_anterior']} (Desconectado)"
                        ciclo.soh_porcentaje = 0.0
                        # Confirmamos los cambios para que persistan en la BD
                        db.commit()
                        enviar_telegram(f"Ciclo {ciclo.tipo_ciclo} Finalizado por Desconexión para {ciclo.qr_id}.")
                # Cerramos la conexión con la BD
                db.close()
                # Actualizamos el estado
                ciclo_activo["id_ciclo_sql"] = None
                ciclo_activo["estado_anterior"] = "REPOSO"  # Ponemos al sistema en reposo
    else:
        ciclo_activo["esp32_online"] = False  # Indica que se perdió la conexión con el ESP32
    return ciclo_activo