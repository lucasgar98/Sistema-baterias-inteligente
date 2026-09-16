import json
from datetime import datetime
from fastapi import FastAPI, Response
from fastapi.responses import StreamingResponse
import paho.mqtt.client as mqtt
import requests
import io
from PIL import Image
from pyzbar.pyzbar import decode
from sqlalchemy import create_engine, Column, Integer, String, Float, DateTime
from sqlalchemy.orm import declarative_base, sessionmaker
from influxdb import InfluxDBClient
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware

# --- CONFIGURACIÓN DE BASES DE DATOS ---
# 1. SQL (SQLite local por simplicidad, fácil de migrar a Postgres luego)
engine = create_engine('sqlite:///./baterias.db', connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()

# Tablas SQL
class Inventario(Base):
    __tablename__ = "baterias_inventario"
    qr_id = Column(String, primary_key=True, index=True)
    fecha_registro = Column(DateTime, default=datetime.utcnow)
    estado_general = Column(String, default="OPERATIVO")

class HistorialCiclos(Base):
    __tablename__ = "historial_ciclos"
    id = Column(Integer, primary_key=True, index=True)
    qr_id = Column(String)
    tipo_ciclo = Column(String)
    fecha_inicio = Column(DateTime, default=datetime.utcnow)
    fecha_fin = Column(DateTime, nullable=True)
    voltaje_final = Column(Float, nullable=True)
    temp_maxima = Column(Float, default=0.0)
    capacidad_ah = Column(Float, default=0.0)  # Ah acumulados
    soh_porcentaje = Column(Float, default=0.0)
    resistencia_interna = Column(Float, default=0.0) # mOhm

Base.metadata.create_all(bind=engine)

# 2. InfluxDB (Telemetría en vivo)
try:
    influx_client = InfluxDBClient(host='influxdb', port=8086)
    # Intentamos crear la DB, si falla (porque ya existe o no está listo) lo manejamos
    try:
        influx_client.create_database('baterias_db')
    except:
        pass
    influx_client.switch_database('baterias_db')
    print("Conectado a InfluxDB correctamente.")
except Exception as e:
    print(f"Aviso: No se pudo conectar a InfluxDB ({e}). Se omitirá el guardado histórico vivo.")
    influx_client = None

# --- ESTADO GLOBAL (Memoria temporal para el ciclo activo) ---
ciclo_activo = {
    "qr_actual": "NINGUNA",
    "estado_anterior": "REPOSO",
    "id_ciclo_sql": None,
    "temp_maxima": 0.0,
    "v_actual": 0.0,
    "i_actual": 0.0,
    "t_actual": 0.0,
    "ah_acumulado": 0.0,
    "ultima_lectura": None,
    "v_inicial": 0.0,
    "i_inicial": 0.0,
    "ri_calculada": 0.0,
    "perfil_actual": "NiMH-7.2V",
    "cap_nominal": 6.5,
    "v_corte_carga": 9.0,
    "v_corte_descarga": 6.0,
    "esp32_online": False,
    "espcam_ip": None
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
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(">>> CONECTADO A MQTT BROKER (Mosquitto) <<<")
        client.subscribe("ESP32/telemetria")
        client.subscribe("ESP32/bateria_qr")
        client.subscribe("ESP32/espcam_ip")
    else:
        print(f">>> ERROR DE CONEXIÓN MQTT: Código {rc} <<<")

def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        print(f"MQTT RECIBIDO [{topic}]: {payload}")
        db = SessionLocal()

        if topic == "ESP32/bateria_qr":
            qr = payload.strip()
            if qr and qr != "NINGUNA":
                ciclo_activo["qr_actual"] = qr
                # Si el QR es nuevo, lo registramos en el inventario
                bateria = db.query(Inventario).filter(Inventario.qr_id == qr).first()
                if not bateria:
                    nueva_bat = Inventario(qr_id=qr)
                    db.add(nueva_bat)
                    db.commit()
                    print(f"Nueva batería registrada: {qr}")
        
        elif topic == "ESP32/espcam_ip":
            ip = payload.strip()
            if ip:
                ciclo_activo["espcam_ip"] = ip
                print(f"IP de ESP32-CAM registrada: {ip}")

        elif topic == "ESP32/telemetria":
            # Parseo rústico del mensaje (v:7.30,i:900,t:25.4,s:CARGANDO,qr:XYZ)
            try:
                datos = dict(item.split(":") for item in payload.split(","))
                v, i, t, estado = float(datos['v']), float(datos['i']), float(datos['t']), datos['s']
            except Exception as e:
                print(f"Error parseando telemetría: {e}")
                db.close()
                return

            ciclo_activo["v_actual"] = v
            ciclo_activo["i_actual"] = i
            ciclo_activo["t_actual"] = t
            ciclo_activo["esp32_online"] = True
            if 'p' in datos:
                ciclo_activo["perfil_actual"] = datos['p']
            if 'vc' in datos:
                ciclo_activo["v_corte_carga"] = float(datos['vc'])
            if 'vd' in datos:
                ciclo_activo["v_corte_descarga"] = float(datos['vd'])

            ahora = datetime.utcnow()
            # 1. Guardar siempre en InfluxDB
            if influx_client:
                try:
                    punto_influx = [{
                        "measurement": "telemetria_modulo",
                        "tags": {"qr": ciclo_activo["qr_actual"]},
                        "fields": {"voltaje": v, "corriente": i, "temperatura": t}
                    }]
                    influx_client.write_points(punto_influx)
                except Exception as e:
                    print(f"Error escribiendo en InfluxDB (Omitiendo): {e}")

            # 2. Lógica de Integración Ah (Trapecio simple)
            if estado != "REPOSO":
                if ciclo_activo["ultima_lectura"]:
                    dt_horas = (ahora - ciclo_activo["ultima_lectura"]).total_seconds() / 3600.0
                    ciclo_activo["ah_acumulado"] += (abs(i) / 1000.0) * dt_horas
            ciclo_activo["ultima_lectura"] = ahora

            if t > ciclo_activo["temp_maxima"]:
                ciclo_activo["temp_maxima"] = t

            # Transición: Inicio de Ciclo
            if estado != "REPOSO" and ciclo_activo["estado_anterior"] == "REPOSO":
                nuevo_ciclo = HistorialCiclos(
                    qr_id=ciclo_activo["qr_actual"],
                    tipo_ciclo=estado,
                    fecha_inicio=ahora
                )
                db.add(nuevo_ciclo)
                db.commit()
                db.refresh(nuevo_ciclo)
                ciclo_activo["id_ciclo_sql"] = nuevo_ciclo.id
                ciclo_activo["temp_maxima"] = t
                ciclo_activo["ah_acumulado"] = 0.0
                ciclo_activo["v_inicial"] = v
                ciclo_activo["i_inicial"] = i
                ciclo_activo["ri_calculada"] = 0.0 # Se calcula en el siguiente paso
                print(f"Iniciado ciclo {estado} en SQL.")

            # Cálculo de Resistencia Interna (Primeros segundos)
            elif estado != "REPOSO" and ciclo_activo["ri_calculada"] == 0.0:
                di = abs(i - ciclo_activo["i_inicial"])
                dv = abs(v - ciclo_activo["v_inicial"])
                if di > 100: # Necesitamos un delta de corriente significativo (>100mA)
                    ciclo_activo["ri_calculada"] = (dv / (di / 1000.0)) * 1000.0 # mOhm
                    print(f"RI Calculada: {ciclo_activo['ri_calculada']:.1f} mOhm")

            # Transición: Fin de Ciclo
            elif estado == "REPOSO" and ciclo_activo["estado_anterior"] != "REPOSO":
                if ciclo_activo["id_ciclo_sql"]:
                    ciclo = db.query(HistorialCiclos).filter(HistorialCiclos.id == ciclo_activo["id_ciclo_sql"]).first()
                    if ciclo:
                        ciclo.fecha_fin = ahora
                        ciclo.voltaje_final = v
                        ciclo.temp_maxima = ciclo_activo["temp_maxima"]
                        ciclo.capacidad_ah = ciclo_activo["ah_acumulado"]
                        ciclo.resistencia_interna = ciclo_activo["ri_calculada"]
                        
                        # Determinar si fue interrumpido o completado normalmente
                        v_corte_carga = ciclo_activo.get("v_corte_carga", 9.0)
                        v_corte_descarga = ciclo_activo.get("v_corte_descarga", 6.0)
                        tipo_anterior = ciclo_activo["estado_anterior"]
                        
                        fue_interrumpido = False
                        if tipo_anterior == "CARGANDO":
                            if v < (v_corte_carga - 0.1):
                                fue_interrumpido = True
                        elif tipo_anterior == "DESCARGANDO":
                            if v > (v_corte_descarga + 0.1):
                                fue_interrumpido = True
                                
                        if fue_interrumpido:
                            ciclo.tipo_ciclo = f"{tipo_anterior} (Interrumpido)"
                            ciclo.soh_porcentaje = 0.0
                        else:
                            # SoH Mejorado (Capacidad + Resistencia) se calcula sólo para descargas completadas normalmente
                            if tipo_anterior == "DESCARGANDO":
                                cap_nominal = ciclo_activo.get("cap_nominal", 2.5)
                                ri_last = ciclo_activo.get("ri_calculada", 0.0)
                                r_cap = min(1.0, ciclo.capacidad_ah / cap_nominal)
                                r_ri = 1.0
                                if ri_last > 0:
                                    umbral_ri = 80.0 # mOhm donde empezamos a preocuparnos en NiMH
                                    r_ri = 1.0 / (1.0 + max(0, (ri_last - 20) / umbral_ri))
                                ciclo.soh_porcentaje = ((0.4 * r_cap) + (0.6 * r_ri)) * 100
                            else:
                                ciclo.soh_porcentaje = 0.0
                        
                        db.commit()
                        msg_telegram = f"Ciclo {ciclo.tipo_ciclo} Finalizado para {ciclo.qr_id}."
                        if not fue_interrumpido and tipo_anterior == "DESCARGANDO":
                            msg_telegram += f" Capacidad: {ciclo.capacidad_ah:.3f}Ah. SoH: {ciclo.soh_porcentaje:.1f}%"
                        enviar_telegram(msg_telegram)
                ciclo_activo["id_ciclo_sql"] = None
                ciclo_activo["ah_acumulado"] = 0.0
                ciclo_activo["temp_maxima"] = 0.0

            ciclo_activo["estado_anterior"] = estado

        db.close()
    except Exception as e:
        print(f"Error crítico en on_message: {e}")


# Intentamos configurar para paho-mqtt 2.x si está disponible, sino 1.x
try:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except AttributeError:
    mqtt_client = mqtt.Client()

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set("admin", "baterias2026")

# Conexión con reintentos para evitar caídas al inicio de Docker
import time
def conectar_mqtt():
    reintentos = 0
    while reintentos < 5:
        try:
            mqtt_client.connect("mosquitto", 1883, 60)
            mqtt_client.loop_start()
            print("Hilo MQTT iniciado.")
            break
        except Exception as e:
            reintentos += 1
            print(f"Error conectando a MQTT (reintento {reintentos}/5): {e}")
            time.sleep(3)

conectar_mqtt()

# --- API WEB (Para controlar desde la PC a futuro) ---
app = FastAPI(title="Controlador de Baterías")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.post("/comando/{accion}")
def enviar_comando_esp32(accion: str):
    """Acciones permitidas: START_C, STOP_C, START_D, STOP_D"""
    mqtt_client.publish("ESP32/comandos", accion)
    return {"status": "Comando enviado", "accion": accion}

@app.post("/set_qr")
def set_qr_manual(qr: str):
    """Asigna manualmente el QR de la batería activa"""
    qr = qr.strip()
    if not qr or len(qr) < 2:
        return {"status": "Error", "msg": "QR inválido"}
    ciclo_activo["qr_actual"] = qr
    # Notificar al ESP32 y registrar en inventario
    mqtt_client.publish("ESP32/bateria_qr", qr)
    db = SessionLocal()
    bateria = db.query(Inventario).filter(Inventario.qr_id == qr).first()
    if not bateria:
        nueva_bat = Inventario(qr_id=qr)
        db.add(nueva_bat)
        db.commit()
    db.close()
    return {"status": "QR asignado", "qr": qr}

def intentar_decodificar(img):
    from PIL import ImageOps, ImageEnhance
    
    # 1. Imagen original
    decoded = decode(img)
    if decoded:
        return decoded
        
    # 2. Rotada 90 grados (por si el sensor tiene desenfoque asimétrico vertical u horizontal)
    try:
        img_rot = img.rotate(90, expand=True)
        decoded = decode(img_rot)
        if decoded:
            return decoded
    except:
        img_rot = None

    # 3. Escala de grises + Contraste 2.0
    try:
        gray = ImageOps.grayscale(img)
        enhancer = ImageEnhance.Contrast(gray)
        img_contrast = enhancer.enhance(2.0)
        decoded = decode(img_contrast)
        if decoded:
            return decoded
    except:
        gray = None

    # 4. Escala de grises + Contraste 2.0 rotado 90
    try:
        if img_rot:
            gray_rot = ImageOps.grayscale(img_rot)
            enhancer_rot = ImageEnhance.Contrast(gray_rot)
            img_contrast_rot = enhancer_rot.enhance(2.0)
            decoded = decode(img_contrast_rot)
            if decoded:
                return decoded
    except:
        pass

    # 5. Umbrales binarizados en escala de grises original
    try:
        if gray:
            for threshold in [127, 90, 160]:
                img_bin = gray.point(lambda p: 255 if p > threshold else 0)
                decoded = decode(img_bin)
                if decoded:
                    return decoded
    except:
        pass

    # 6. Redimensionar a 2x (Upscale con resample de calidad para desenfoques)
    try:
        try:
            resample_filter = Image.Resampling.LANCZOS
        except AttributeError:
            resample_filter = Image.LANCZOS
            
        img_large = img.resize((img.width * 2, img.height * 2), resample_filter)
        decoded = decode(img_large)
        if decoded:
            return decoded
            
        # 2x Grayscale + Contraste
        gray_large = ImageOps.grayscale(img_large)
        enhancer_large = ImageEnhance.Contrast(gray_large)
        img_contrast_large = enhancer_large.enhance(2.0)
        decoded = decode(img_contrast_large)
        if decoded:
            return decoded
            
        # 2x Binarizado (127, 90, 160)
        for threshold in [127, 90, 160]:
            img_bin_large = gray_large.point(lambda p: 255 if p > threshold else 0)
            decoded = decode(img_bin_large)
            if decoded:
                return decoded
    except:
        pass

    return None

@app.get("/video_feed")
def video_feed():
    """Proxy de streaming de video MJPEG que decodifica códigos QR sobre la marcha"""
    ip = ciclo_activo.get("espcam_ip")
    if not ip:
        return Response(status_code=404, content="Cámara no detectada")
        
    # Limpiar cualquier mensaje de depuración basura del puerto serial (ej. DMA overflow)
    ip = "".join(c for c in ip if c.isdigit() or c == '.')
    if len(ip) < 7:
        return Response(status_code=404, content="IP de cámara inválida")
        
    url = f"http://{ip}/stream"
    
    def generate():
        try:
            r = requests.get(url, stream=True, timeout=5.0)
            if r.status_code != 200:
                return
                
            buffer = b""
            for chunk in r.iter_content(chunk_size=4096):
                buffer += chunk
                while True:
                    start = buffer.find(b"\xff\xd8")
                    if start == -1:
                        if len(buffer) > 0:
                            buffer = buffer[-1:]
                        break
                        
                    end = buffer.find(b"\xff\xd9", start)
                    if end == -1:
                        break
                        
                    jpg = buffer[start:end+2]
                    buffer = buffer[end+2:]
                    
                    # Decodificar QR en este frame con super-pipeline
                    try:
                        img = Image.open(io.BytesIO(jpg))
                        decoded = intentar_decodificar(img)
                                        
                        if decoded:
                            qr_text = decoded[0].data.decode('utf-8').strip()
                            if qr_text and len(qr_text) > 2:
                                # ¡Encontrado!
                                ciclo_activo["qr_actual"] = qr_text
                                mqtt_client.publish("ESP32/bateria_qr", qr_text)
                                
                                # Registrar en base de datos
                                db = SessionLocal()
                                bateria = db.query(Inventario).filter(Inventario.qr_id == qr_text).first()
                                if not bateria:
                                    nueva_bat = Inventario(qr_id=qr_text)
                                    db.add(nueva_bat)
                                    db.commit()
                                db.close()
                                
                                # Yield el último frame de éxito
                                yield (b'--frame\r\n'
                                       b'Content-Type: image/jpeg\r\n\r\n' + jpg + b'\r\n')
                                return # Terminar la transmisión
                    except Exception as e:
                        pass
                        
                    # Yield el frame normal
                    yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + jpg + b'\r\n')
        except Exception as e:
            print(f"Error en stream de video: {e}")
            
    return StreamingResponse(generate(), media_type="multipart/x-mixed-replace; boundary=frame")

@app.get("/bateria/{qr_id}")
def detalle_bateria(qr_id: str):
    """Resumen completo de una batería: inventario + historial de ciclos"""
    db = SessionLocal()
    bateria = db.query(Inventario).filter(Inventario.qr_id == qr_id).first()
    if not bateria:
        db.close()
        return {"status": "Error", "msg": "Batería no encontrada"}
    ciclos = db.query(HistorialCiclos).filter(
        HistorialCiclos.qr_id == qr_id
    ).order_by(HistorialCiclos.fecha_inicio.desc()).all()
    db.close()
    return {
        "qr_id": bateria.qr_id,
        "fecha_registro": bateria.fecha_registro,
        "estado_general": bateria.estado_general,
        "total_ciclos": len(ciclos),
        "ciclos": [
            {
                "id": c.id,
                "tipo_ciclo": c.tipo_ciclo,
                "fecha_inicio": c.fecha_inicio,
                "fecha_fin": c.fecha_fin,
                "voltaje_final": c.voltaje_final,
                "temp_maxima": c.temp_maxima,
                "capacidad_ah": c.capacidad_ah,
                "soh_porcentaje": c.soh_porcentaje,
                "resistencia_interna": c.resistencia_interna
            } for c in ciclos
        ]
    }

@app.get("/baterias")
def listar_baterias():
    db = SessionLocal()
    baterias = db.query(Inventario).all()
    resultado = []
    for b in baterias:
        count = db.query(HistorialCiclos).filter(HistorialCiclos.qr_id == b.qr_id).count()
        resultado.append({
            "qr_id": b.qr_id,
            "fecha_registro": b.fecha_registro,
            "estado_general": b.estado_general,
            "ciclos_count": count
        })
    db.close()
    return resultado

@app.get("/historial")
def listar_historial():
    db = SessionLocal()
    historial = db.query(HistorialCiclos).order_by(HistorialCiclos.fecha_inicio.desc()).limit(50).all()
    db.close()
    return historial

@app.get("/historial/{qr_id}")
def historial_por_qr(qr_id: str):
    db = SessionLocal()
    historial = db.query(HistorialCiclos).filter(HistorialCiclos.qr_id == qr_id).order_by(HistorialCiclos.fecha_inicio.desc()).all()
    db.close()
    return historial

@app.post("/perfil")
def set_perfil(tipo: str):
    """Perfiles: LI-ION-2S, LI-ION-3S, PB-12V"""
    config = {
        "LI-ION-2S": {"v_c": 8.4, "v_d": 6.0},
        "LI-ION-3S": {"v_c": 12.6, "v_d": 9.0},
        "PB-12V": {"v_c": 14.4, "v_d": 11.0},
        "NiMH-7.2V": {"v_c": 9.0, "v_d": 6.0} # Carga máx ~1.5V/celda, Descarga ~1.0V/celda (6 celdas)
    }
    if tipo in config:
        ciclo_activo["perfil_actual"] = tipo
        payload = f"V_C:{config[tipo]['v_c']},V_D:{config[tipo]['v_d']}"
        mqtt_client.publish("ESP32/config_perfil", payload)
        return {"status": "Perfil enviado", "perfil": tipo}
    return {"status": "Error", "msg": "Perfil no encontrado"}

@app.post("/set_nominal")
def set_capacidad_nominal(valor: float):
    ciclo_activo["cap_nominal"] = valor
    return {"status": "Capacidad nominal actualizada", "valor": valor}

@app.get("/exportar")
def exportar_csv():
    import csv
    from io import StringIO
    from fastapi.responses import StreamingResponse
    
    db = SessionLocal()
    historial = db.query(HistorialCiclos).all()
    db.close()

    output = StringIO()
    writer = csv.writer(output)
    writer.writerow(["ID", "QR", "Tipo", "Inicio", "Fin", "V Final", "Temp Max", "Capacidad Ah", "SoH %", "RI mOhm"])
    for c in historial:
        writer.writerow([c.id, c.qr_id, c.tipo_ciclo, c.fecha_inicio, c.fecha_fin, c.voltaje_final, c.temp_maxima, c.capacidad_ah, c.soh_porcentaje, c.resistencia_interna])
    
    output.seek(0)
    return StreamingResponse(output, media_type="text/csv", headers={"Content-Disposition": "attachment; filename=reporte_baterias.csv"})
	
app.mount("/web", StaticFiles(directory="frontend"), name="frontend")

@app.get("/")
def serve_home():
    return FileResponse("frontend/index.html")

@app.get("/estado_actual")
def obtener_estado(response: Response):
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    # Si hace más de 7 segundos que no recibimos telemetría, ponemos valores en 0
    if ciclo_activo["ultima_lectura"]:
        delta = (datetime.utcnow() - ciclo_activo["ultima_lectura"]).total_seconds()
        if delta > 7.0: # 7 segundos de gracia
            ciclo_activo["v_actual"] = 0.0
            ciclo_activo["i_actual"] = 0.0
            ciclo_activo["t_actual"] = 0.0
            ciclo_activo["esp32_online"] = False
            # Si estaba cargando o descargando, al perder conexión pasa a REPOSO
            if ciclo_activo["estado_anterior"] != "REPOSO":
                db = SessionLocal()
                if ciclo_activo["id_ciclo_sql"]:
                    ciclo = db.query(HistorialCiclos).filter(HistorialCiclos.id == ciclo_activo["id_ciclo_sql"]).first()
                    if ciclo:
                        ciclo.fecha_fin = datetime.utcnow()
                        ciclo.voltaje_final = 0.0
                        ciclo.temp_maxima = ciclo_activo["temp_maxima"]
                        ciclo.capacidad_ah = ciclo_activo["ah_acumulado"]
                        ciclo.resistencia_interna = ciclo_activo["ri_calculada"]
                        ciclo.tipo_ciclo = f"{ciclo_activo['estado_anterior']} (Desconectado)"
                        ciclo.soh_porcentaje = 0.0
                        db.commit()
                        enviar_telegram(f"Ciclo {ciclo.tipo_ciclo} Finalizado por Desconexión para {ciclo.qr_id}.")
                db.close()
                ciclo_activo["id_ciclo_sql"] = None
                ciclo_activo["estado_anterior"] = "REPOSO"
    else:
        ciclo_activo["esp32_online"] = False
    return ciclo_activo