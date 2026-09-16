================================================================================
  SISTEMA INTELIGENTE DE CARGA, DESCARGA Y DIAGNÓSTICO DE BATERÍAS (PROYECTO)
================================================================================

Este proyecto es una plataforma integrada de hardware y software diseñada para 
realizar ciclos de carga, descarga y caracterización de baterías de múltiples 
químicas (Li-Ion, Pb, NiMH). 

El sistema combina el control de potencia en tiempo real (MicroPython), una 
interfaz táctil física (LVGL v8 en CYD), visión por computadora para 
identificación de baterías (ESP32-CAM + pyzbar) y un backend de monitoreo y 
almacenamiento (FastAPI + SQLite + InfluxDB + MQTT) orquestado en Docker.

--------------------------------------------------------------------------------
1. ARQUITECTURA GENERAL Y FLUJO DE DATOS
--------------------------------------------------------------------------------

El flujo de información y control entre los componentes se organiza así:

   [ ESP32-CAM ] -------------( WiFi: Stream MJPEG )-------------> [ FASTAPI ]
         |                                                            |
     ( UART1 )                                                   ( Escribe a DB )
         |                                                            |
         v                                                            v
  [ ESP32 MASTER ]                                            [ SQLITE / INFLUX ]
    |          |
 ( UART2 )   ( I2C / PWM / ADC )
    |          |
    v          v
 [ CYD ]     [ SENSORES (INA219/NTC) y MOSFETS DE POTENCIA ]
 
* Comunicaciones clave:
  - ESP32 Master <-> Cheap Yellow Display (CYD): UART2 a 115200 baudios.
  - ESP32 Master <-> ESP32-CAM: UART1 a 115200 baudios.
  - ESP32 Master -> Sensor de Corriente (INA219): Bus I2C.
  - ESP32 Master -> MOSFETs de potencia: Señal digital PWM.
  - FastAPI (Servidor) <-> Broker MQTT (Mosquitto): Red local TCP.

--------------------------------------------------------------------------------
2. ESTRUCTURA DEL PROYECTO (Workspace)
--------------------------------------------------------------------------------

El repositorio se organiza de la siguiente manera:

/ (Raíz del proyecto)
|-- docker-compose.yml           <- Orquestador de servicios (Mosquitto, InfluxDB, API)
|-- README.txt                   <- Esta documentación principal
|
|-- backend/                     <- Servidor backend y panel de control web
|   |-- main.py                  <- API FastAPI, cliente MQTT, base de datos y lector QR
|   |-- dockerfile               <- Archivo de construcción de imagen Docker
|   |-- baterias.db              <- Base de datos SQLite (se genera sola al arrancar)
|   `-- frontend/                <- Código del dashboard web para PC
|       `-- index.html           <- Panel de monitoreo web interactivo
|
|-- firmware/                    <- Código fuente para los microcontroladores
|   |-- esp32_master/            <- Firmware del ESP32 Principal (MicroPython)
|   |   |-- main.py              # Lógica de potencia, control PWM y lectura de sensores
|   |   |-- config.json.template # Plantilla para configurar datos de red local
|   |   `-- legacy/              # Histórico: código viejo con botones analógicos
|   |       `-- legacy_main_buttons.py
|   |-- pantalla_cyd/            <- Interfaz gráfica táctil (ESP32 CYD con Arduino C++)
|   |   |-- pantalla_cyd.ino     # Inicialización, drivers de pantalla y lazo táctil
|   |   |-- ui.cpp               # Creación manual de pantallas, sliders y callbacks
|   |   `-- ui.h                 # Definición de variables de pantalla
|   |-- espcam/                  <- Cámara de lectura QR (ESP32-CAM con Arduino C++)
|   |   |-- espcam.ino           # Servidor web local de streaming MJPEG
|   |   `-- legacy/              # Histórico: lector de QR local serial
|   |       `-- legacy_serial_qr_reader.txt
|   `-- test_pantalla/           <- Diagnóstico de hardware táctil y colores
|       `-- test_pantalla.ino
|
|-- hardware/                    <- Archivos de diseño físico y circuital
|   |-- pcb_cargador/            <- Proyecto completo de la placa en KiCad
|   |-- ui_design/               <- Proyecto de prototipado original en EEZ Studio
|   |   |-- Lector Bateria.eez-project
|   |   `-- src/                 # Código LVGL exportado originalmente por EEZ
|   `-- 3d_print/                <- Modelos 3D de la carcasa (.3mf)
|
`-- documentacion/               <- Archivos de soporte y estudio
    |-- datasheets/              <- Fichas técnicas en PDF (MOSFETs, integrados)
    |-- BOM/                     <- Listas de materiales (formatos Excel y Word)
    |-- informes/                <- Reportes académicos y bitácora de cambios (PDF)
    `-- videos/                  <- Videos de demostración del circuito (MP4)

--------------------------------------------------------------------------------
3. EXPLICACIÓN DE COMPONENTES
--------------------------------------------------------------------------------

[ ESP32 Master ]
* Lazo de Corriente Constante: Genera PWM a 500 Hz para excitar los gates de los 
  MOSFETs. Aumenta o disminuye el ciclo de trabajo (+/- 20 pasos sobre 1000) 
  dependiendo de si la lectura de corriente está por debajo o por encima del 
  objetivo deseado (ej. 900 mA).
* Corrección de Tensión: Compensa dinámicamente las lecturas del voltaje en 
  función de la caída de tensión introducida por shunts y cables durante la 
  carga (-0.0001V por mA) y descarga (+0.00015V por mA).
* Filtro de Temperatura (EMA): Suaviza las muestras analógicas del NTC usando
  un promedio móvil exponencial con factor alpha = 0.08:
     T_filtrada = 0.08 * T_actual + 0.92 * T_anterior
* Seguridad Térmica: Si la temperatura supera los 45°C, detiene de inmediato 
  las salidas PWM y bloquea la operación. Se restablece automáticamente cuando 
  desciende por debajo de los 40°C.
* Lógica AUTO (Máquina de Estados): Corre la secuencia:
  Descarga inicial -> Espera enfriamiento -> Carga -> Espera -> Descarga de test.

[ Pantalla CYD (Cheap Yellow Display) ]
* Interfaz LVGL v8: Cuenta con navegación deslizable por pestañas (TileView):
  1. Principal: Mediciones en vivo, botones de acción rápida, QR activo.
  2. Parámetros: Selección de químicas y límites de voltaje/corriente.
  3. Red: Conexión WiFi/MQTT on-demand y teclado virtual para configuración.
* Integración UART: La pantalla no toma decisiones de potencia; le envía comandos 
  al Master (ej: "START_C", "SET_LIMITS:c,d,i") y recibe de éste cadenas JSON 
  con la telemetría en tiempo real.

[ ESP32-CAM ]
* Emisor de Video: Expone el stream MJPEG en la ruta http://[IP]/stream a 10 FPS 
  en resolución SVGA (800x600). Solicita datos WiFi al Master por Serial al iniciar.

[ Backend en Servidor (Docker FastAPI) ]
* Servidor y API REST: FastAPI orquesta la comunicación de todo el sistema. Expone 
  los endpoints para la interfaz web, el control remoto y la base de datos SQL.
* Decodificador QR en Servidor: El endpoint "/video_feed" capta el stream de la 
  cámara y aplica un pipeline de mejora de imagen (rotación, escala de grises, 
  contraste 2.0, umbrales de binarización y reescalado 2x) para decodificar el QR 
  con la librería "pyzbar" de forma robusta, previniendo crashes en el ESP32-CAM.
* Guardado de Telemetría: Consume las lecturas vía MQTT y las escribe tanto en 
  InfluxDB (series de tiempo en vivo) como en SQLite (ciclos cerrados y estadísticas).

--------------------------------------------------------------------------------
3.5 DETALLE DE SERVICIOS EN DOCKER Y PERSISTENCIA
--------------------------------------------------------------------------------

La infraestructura del backend se levanta en un entorno virtual aislado mediante 
Docker Compose, configurado en el archivo "docker-compose.yml". Se compone de 
tres servicios clave que cooperan en red:

1. Broker MQTT (Servicio "mosquitto")
   * Imagen: "eclipse-mosquitto:latest"
   * Puertos expuestos:
     - 1884 (externo, para conexión local TCP/IP) -> 1883 (interno de la red Docker)
     - 9001 (interno/externo, para WebSockets en caso de dashboards web directos)
   * Configuración: Carga el archivo "./mosquitto/config/mosquitto.conf". Está 
     configurado para permitir conexiones anónimas ("allow_anonymous true") para 
     simplificar el desarrollo y conexión de los microcontroladores.
   * Temas (Topics) MQTT del sistema:
     - "ESP32/telemetria": Cadenas de texto plano enviadas por el Master cada 3s 
       con formato "v:val,i:val,t:val,s:estado,qr:id,ah:val,p:perfil,vc:val,vd:val".
     - "ESP32/bateria_qr": Intercambio bidireccional del QR de la batería activa.
     - "ESP32/config_perfil": Consignas de límites de carga y descarga ("V_C:x.x,V_D:y.y").
     - "ESP32/comandos": Órdenes de control ("START_C", "START_D", "STOP_ALL").
     - "ESP32/espcam_ip": La cámara publica su IP para descubrimiento automático.
     - "ESP32/alerta_critica": Mensajes de error térmico ("SOBRETEMPERATURA:xxC").
   * Persistencia: Carpeta local "./mosquitto/data" montada en "/mosquitto/data".

2. Base de Datos Temporal (Servicio "influxdb")
   * Imagen: "influxdb:1.8" (versión 1.8 seleccionada por compatibilidad sencilla 
     y consultas HTTP directas sin autenticación por tokens complejos).
   * Puerto expuesto: 8086 (API de consulta y escritura).
   * Base de datos interna: "baterias_db".
   * Estructura de datos: Almacena la medición "telemetria_modulo".
     - Tags (indexados): "qr" (identificador de la batería).
     - Fields: "voltaje" (Float), "corriente" (Float), "temperatura" (Float).
   * Persistencia: Volumen local "./influxdb/data" mapeado a "/var/lib/influxdb" 
     para conservar las mediciones entre reinicios de los contenedores.

3. Servidor de Control y Base de Datos Relacional (Servicio "backend_baterias")
   * Imagen: Construida localmente con el "./backend/dockerfile". Usa Python 3.10-slim 
     e instala las librerías del sistema ("libzbar0" para lectura de QR) y de Python 
     ("fastapi", "uvicorn", "paho-mqtt", "sqlalchemy", "influxdb", "pyzbar", "pillow").
   * Puerto expuesto: 8000 (acceso a la interfaz web y endpoints).
   * Persistencia:
     - Mapea "./backend:/app" para permitir edición de código en vivo sin 
       reconstruir el contenedor.
     - Guarda el archivo de base de datos SQLite en "./backend/baterias.db".
   * Esquema SQLite (ORM SQLAlchemy):
     - Tabla "baterias_inventario" (Registro único de cada batería escaneada).
       Campos: "qr_id" (Texto, clave primaria), "fecha_registro" (Fecha/Hora), 
       "estado_general" (Texto).
     - Tabla "historial_ciclos" (Registro completo de cada carga/descarga terminada).
       Campos: "id" (Entero, auto-incremental), "qr_id" (Texto), "tipo_ciclo" (Texto), 
       "fecha_inicio" (Fecha/Hora), "fecha_fin" (Fecha/Hora), "voltaje_final" (Float), 
       "temp_maxima" (Float), "capacidad_ah" (Float), "soh_porcentaje" (Float), 
       "resistencia_interna" (Float).
   * Cliente MQTT Integrado: Al arrancar el backend en FastAPI, se inicia un hilo 
     secundario con el cliente MQTT ("paho-mqtt") que se suscribe a los tópicos del 
     Master. Éste procesa las tramas, calcula en tiempo real la capacidad acumulada 
     (integración por trapecios respecto al tiempo real) e identifica transiciones 
     de estado (ej. de REPOSO a CARGANDO) para abrir y cerrar ciclos en SQLite.
   * Servidor de Interfaz Web: Monta la carpeta "./backend/frontend" en la ruta 
     HTTP "/web" y sirve la web interactiva [index.html](file:///c:/Gonza/Carrera/Plan%202023/3er%20Año/Digitales%20IV/Proyecto%20bateria/backend/frontend/index.html) en la ruta raíz "/".

--------------------------------------------------------------------------------
4. MAPA DE CONEXIÓN DE PINES (PINOUT)
--------------------------------------------------------------------------------

+--------------------+---------------+---------------------+---------------+-----------------------------------+
| Dispositivo Origen | Pin de Salida | Dispositivo Destino | Pin de Entrada| Descripción / Protocolo           |
+--------------------+---------------+---------------------+---------------+-----------------------------------+
| ESP32 Master       | GPIO 18 (PWM) | MOSFET Canal N      | Gate          | Control de corriente de carga     |
| ESP32 Master       | GPIO 19 (PWM) | MOSFET Canal P      | Gate          | Control de corriente de descarga   |
| ESP32 Master       | GPIO 33 (ADC) | Divisor de Voltaje  | V_BAT         | Medición de voltaje de batería    |
| ESP32 Master       | GPIO 34 (ADC) | Divisor NTC         | Temperatura   | Medición de temperatura NTC       |
| ESP32 Master       | GPIO 21       | Sensor INA219       | SDA           | Bus I2C - Datos de corriente      |
| ESP32 Master       | GPIO 22       | Sensor INA219       | SCL           | Bus I2C - Reloj de corriente      |
| ESP32 Master       | GPIO 27 (TX1) | ESP32-CAM           | RX (U0R)      | UART1 - Envío configuración WiFi  |
| ESP32 Master       | GPIO 26 (RX1) | ESP32-CAM           | TX (U0T)      | UART1 - Recepción IP de cámara    |
| ESP32 Master       | GPIO 17 (TX2) | Pantalla CYD (CN1)  | RX (GPIO 22)  | UART2 - Transmisión de telemetría |
| ESP32 Master       | GPIO 16 (RX2) | Pantalla CYD (CN1)  | TX (GPIO 27)  | UART2 - Recepción de comandos     |
+--------------------+---------------+---------------------+---------------+-----------------------------------+

--------------------------------------------------------------------------------
5. GUÍA DE DESPLIEGUE Y PUESTA EN MARCHA
--------------------------------------------------------------------------------

A. Iniciar Infraestructura de Servidor (PC de laboratorio con Docker instalado):
   1. Colocarse en la raíz del proyecto (donde está "docker-compose.yml").
   2. Ejecutar el comando en la terminal:
         docker compose up --build -d
   3. Ingresar en el navegador web a: http://localhost:8000 para abrir la app.

B. Preparar ESP32 Master:
   1. Flashear la placa con MicroPython estable (v1.20 o superior).
   2. Subir el archivo de control "firmware/esp32_master/main.py".
   3. Copiar el archivo "config.json.template", renombrarlo como "config.json", 
      completar las credenciales WiFi y de MQTT de tu red y subirlo.
   4. Subir las librerías "ina219.py" y "umqtt/simple.py" a la placa.

C. Cargar Firmware de CYD y Cámara (Arduino IDE):
   * Pantalla CYD: Abrir "firmware/pantalla_cyd/pantalla_cyd.ino". 
     Librerías necesarias: "lvgl" (v8.x), "TFT_eSPI" (configurada con driver 
     y pines para la CYD), "XPT2046_Touchscreen" y "ArduinoJson".
   * ESP32-CAM: Abrir "firmware/espcam/espcam.ino". 
     Flashear seleccionando placa "AI Thinker ESP32-CAM".

--------------------------------------------------------------------------------
6. RESUMEN DE CAMBIOS REALIZADOS
--------------------------------------------------------------------------------

* Migración de Control Físico a Digital: Se removieron botones cableados en favor 
  de comandos por puerto serie desde la pantalla táctil.
* Procesamiento de QR en Servidor: Lector de QR se delegó al backend FastAPI 
  para evitar sobrecalentamientos y crashes en el ESP32-CAM.
* Módulos de Cálculo: Se añadió la estimación de Resistencia Interna (RI) 
  y el Porcentaje de Salud (SoH).
* Organización Limpia: Se clasificaron todos los archivos del repositorio en 
  firmware, hardware y documentación, dejando la raíz despejada y ordenada.
================================================================================
