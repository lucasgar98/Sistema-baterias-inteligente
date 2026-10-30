const API = window.location.origin;  // URL base de la página web
let maxPoints = 100;  // Número máximo de puntos de los gráficos en el panel derecho
let chartTendencia = null;  // Objeto que almacena el gráfico de tendencia de SoH
let connOk = true;  // Bandera que indica si hay conexión con el ESP32

// Mapa que contiene los umbrales de tensión para cada uno de los perfiles
const PERFILES = new Map([
    ["LI-ION-2S", {vCorteCarga: 8.4, vCorteDescarga: 6.0}],
    ["LI-ION-3S", {vCorteCarga: 12.6, vCorteDescarga: 9.0}],
    ["PB-12V", {vCorteCarga: 14.4, vCorteDescarga: 11.0}],
    ["NiMH-7.2V", {vCorteCarga: 9.0, vCorteDescarga: 6.0}]
]);

// --- CHART SETUP ---
// ====== CONFIGURACIÓN DE LOS GRÁFICOS ======
/* Opciones comunes a todos los gráficos */
const optCommon = {
    responsive: true,  // Diseño responsive
    maintainAspectRatio: false,
    animation: false,  // Sin animaciones
    interaction: { mode: 'index', intersect: false },
    plugins: {
        // Leyenda
        legend: {
            labels: { color: '#9ca3af', font: { family: 'Inter', size: 10 }, boxWidth: 10, padding: 4 },
            position: 'top'  // Posición de la leyenda
        }
    },
    scales: {
        // Escala horizontal
        x: {
            ticks: { color: '#6b7280', font: { size: 9 }, maxRotation: 0, maxTicksLimit: 10 },
            grid: { color: '#1f2233' }  // Color de la grilla
        },
        // Escala vertical
        y: {
            type: 'linear',  // Escala lineal
            ticks: { color: '#9ca3af', font: { size: 9 } },
            grid: { color: '#1f2233' }  // Color de la grilla
        }
    }
};
// Obtenemos el elemento HTML correspondiente al gráfico de tensión
const ctxV = document.getElementById('chartVoltaje').getContext('2d');
// Creamos el gráfico de tensión
const chartV = new Chart(ctxV, {
    type: 'line',  // Tipo de gráfico (línea)
    data: {
        labels: [],  // Etiquetas del eje horizontal
        datasets: [{
            label: 'Voltaje (V)',  // Etiqueta de referencia
            borderColor: '#22d3ee',  // Color de borde
            backgroundColor: 'rgba(34,211,238,0.05)',  // Color de fondo
            data: [],  // Datos a insertar en el gráfico
            borderWidth: 1.5,  // Ancho de borde
            tension: 0.3, 
            fill: true,
            pointRadius: 0  // Radio de los puntos
        }]
    },
    options: {
        ...optCommon,  // Opciones comunes
        scales: {
            ...optCommon.scales,  // Escalas comunes
            // Establecemos una escala vertical de 0 a 16 V
            y: { ...optCommon.scales.y, min: 0, max: 16 }
        }
    }
});

// Obtenemos el elemento HTML correspondiente al gráfico de corriente
const ctxI = document.getElementById('chartCorriente').getContext('2d');
// Creamos el gráfico de corriente
const chartI = new Chart(ctxI, {
    type: 'line',  // Tipo de gráfico (línea)
    data: {
        labels: [],  // Etiquetas de eje horizontal
        datasets: [{
            label: 'Corriente (mA)',  // Etiqueta de la leyenda
            borderColor: '#a78bfa',  // Color de borde
            backgroundColor: 'rgba(167,139,250,0.05)',  // Color de fondo
            data: [],  // Datos a insertar en el gráfico
            borderWidth: 1.5,  // Ancho de borde
            tension: 0.3,
            fill: true,
            pointRadius: 0  // Radio de los puntos
        }]
    },
    options: {
        ...optCommon,  // Opciones comunes
        scales: {
            ...optCommon.scales,  // Escalas comunes
            // Establecemos una escala vertical de 0 a 2000 mA
            y: { ...optCommon.scales.y, min: 0, max: 2000 }
        }
    }
});

// Obtenemos el elemento HTML correspondiente al gráfico de temperatura
const ctxT = document.getElementById('chartTemperatura').getContext('2d');
// Creamos el gráfico de temperatura
const chartT = new Chart(ctxT, {
    type: 'line',  // Tipo de gráfico (línea)
    data: {
        labels: [],  // Etiquetas de eje horizontal
        datasets: [{
            label: 'Temperatura (°C)',  // Etiqueta de la leyenda
            borderColor: '#f87171',  // Color de borde
            backgroundColor: 'rgba(248,113,113,0.05)',  // Color de fondo
            data: [],  // Datos a insertar en el gráfico
            borderWidth: 1.5,  // Ancho de borde
            tension: 0.3,
            fill: true,
            pointRadius: 0  // Radio de los puntos
        }]
    },
    options: {
        ...optCommon,  // Opciones comunes
        scales: {
            ...optCommon.scales,  // Escalas comunes
            // Establecemos una escala vertical de 0 a 60°C
            y: { ...optCommon.scales.y, min: 0, max: 60 }
        }
    }
});

// --- CHART CONTROLS FUNCTIONS ---
// ===== FUNCIONES PARA EL CONTROL DE LOS GRÁFICOS ======
// Función callback que se ejecuta cuando se presiona el botón "Controles"
function toggleChartCtrls() {
    // Obtenemos el elemento correspondiente al panel de opciones del gráfico (que está oculto por defecto)
    const p = document.getElementById('chart-controls-panel');
    // Mostramos el panel de opciones como un contenedor flex
    p.style.display = p.style.display === 'none' ? 'flex' : 'none';
}

// Callback que se ejecuta cuando se presiona el botón "Exportar"
function toggleExportMenu() {
    // Obtenemos el elemento correspondiente al panel de exportación (que está oculto por defecto)
    const p = document.getElementById('export-menu-panel');
    // Mostramos el panel de exportación como un contenedor flex
    p.style.display = p.style.display === 'none' ? 'flex' : 'none';
}

// Función callback que se ejecuta al cambiar la opción de la lista desplegable para seleccionar la base de tiempos de los gráficos
function actualizarTimebase(val) {
    // Obtenemos el número máximo de puntos a partir de la opción seleccionada
    maxPoints = parseInt(val) || 100;
    // Construimos un array formado por los objetos que representan los tres gráficos, y recorremos ese
    // array para modificar el número de puntos
    [chartV, chartI, chartT].forEach(c => {
        while (c.data.labels.length > maxPoints) {
            c.data.labels.shift();
            c.data.datasets[0].data.shift();
        }
        c.update();
    });
}

// Callback que se ejecuta cuando cambian los valores de los campos de texto del panel de control del gráfico,
// el cual permite cambiar las escalas de los gráficos
function actualizarEscalas() {
    // Obtenemos los valores de los campos de texto y los convertimos a flotante
    const vMin = parseFloat(document.getElementById('scale-v-min').value);
    const vMax = parseFloat(document.getElementById('scale-v-max').value);
    const iMin = parseFloat(document.getElementById('scale-i-min').value);
    const iMax = parseFloat(document.getElementById('scale-i-max').value);
    const tMin = parseFloat(document.getElementById('scale-t-min').value);
    const tMax = parseFloat(document.getElementById('scale-t-max').value);
    // Actualizamos las escalas en el gráfico de tensión
    if (!isNaN(vMin)) chartV.options.scales.y.min = vMin;  // Límite inferior
    if (!isNaN(vMax)) chartV.options.scales.y.max = vMax;  // Límite superior
    chartV.update();
    // Actualizamos las escalas en el gráfico de corriente
    if (!isNaN(iMin)) chartI.options.scales.y.min = iMin;
    if (!isNaN(iMax)) chartI.options.scales.y.max = iMax;
    chartI.update();
    // Actualizamos las escalas en el gráfico de temperatura
    if (!isNaN(tMin)) chartT.options.scales.y.min = tMin;
    if (!isNaN(tMax)) chartT.options.scales.y.max = tMax;
    chartT.update();
}

// Callback que se ejecuta al presionar el botón "Default", el cual permite reestablecer las escalas por defecto
function restablecerDefaults() {
    // Cambiamos el valor de la base de tiempos a 100
    document.getElementById('select-timebase').value = "100";
    maxPoints = 100;
    // Cambiamos las escalas de los gráficos en el panel de control del gráfico
    // Gráfico de tensión
    document.getElementById('scale-v-min').value = 0;  // Límite inferior
    document.getElementById('scale-v-max').value = 16;  // Límite superior
    // Gráfico de corriente
    document.getElementById('scale-i-min').value = 0;  // Límite inferior
    document.getElementById('scale-i-max').value = 2000;  // Límite superior
    // Gráfico de temperatura
    document.getElementById('scale-t-min').value = 0;  // Límite inferior
    document.getElementById('scale-t-max').value = 60;  // Límite superior
    // Cambiamos las escalas en el gráfico
    actualizarEscalas();
    // Cambiamos la base de tiempos en el gráfico
    actualizarTimebase(100);
}

// Callback que se ejecuta cuando se presionan los botones del panel de exportación (oculto por defecto)
// para exportar los gráficos de cada variable en formato imagen (PNG)
function exportarImagen(param) {
    // Función para descargar una imagen
    const downloadImg = (chartObj, filename) => {
        // Creamos un enlace (etiqueta <a>)
        const link = document.createElement('a');
        // Nombre del archivo que se desea descargar
        link.download = filename;
        // Convertimos el gráfico en imagen codificada en base 64 y cambiamos el URL del enlace
        link.href = chartObj.toBase64Image();
        // Hacemos click en el enlace para descargar la imagen
        link.click();
    };
    // Descargamos el gráfico en formato imagen según el botón presionado
    if (param === 'voltaje') {  
        downloadImg(chartV, 'grafico_voltaje.png');  // Gráfico de tensión
    } else if (param === 'corriente') {
        downloadImg(chartI, 'grafico_corriente.png');  // Gráfico de corriente
    } else if (param === 'temperatura') {
        downloadImg(chartT, 'grafico_temperatura.png');  // Gráfico de temperatura
    } else if (param === 'todos') {
        // Descargamos todos los gráficos de a uno
        downloadImg(chartV, 'grafico_voltaje.png');
        // Descargamos el gráfico de corriente después de 200 ms
        setTimeout(() => downloadImg(chartI, 'grafico_corriente.png'), 200);
        // Descargamos el gráfico de temperatura después de 400 ms
        setTimeout(() => downloadImg(chartT, 'grafico_temperatura.png'), 400);
    }
}

// Callback que se ejecuta cuando se presionan los botones del panel de exportación para exportar los datos
// de cada variable en formato CSV
function exportarDatosSesion(param) {
    // Etiquetas del eje horizontal del gráfico de tensión
    const labels = chartV.data.labels;
    // Datos de los tres gráficos
    const voltajes = chartV.data.datasets[0].data;  // Tensión
    const corrientes = chartI.data.datasets[0].data;  // Corriente
    const temperaturas = chartT.data.datasets[0].data;  // Temperatura
    // URI que embebe contenido CSV directamente en una URL, especificando que los datos están en codificación UTF-8
    let csvContent = "data:text/csv;charset=utf-8,";
    // Agregamos contenido al CSV según el botón presionado
    if (param === 'voltaje') {  // Datos de tensión
        // Encabezado del archivo
        csvContent += "Tiempo,Voltaje (V)\n";
        // Recorremos el array de etiquetas de eje horizontal y vamos guardando las etiquetas (marcas de tiempo)
        // y los valores en el archivo
        labels.forEach((l, i) => {
            csvContent += `${l},${voltajes[i] || 0}\n`;
        });
    } else if (param === 'corriente') {  // Datos de corriente
        csvContent += "Tiempo,Corriente (mA)\n";  // Encabezado del archivo
        // Recorremos el array de etiquetas de eje horizontal y vamos guardando las etiquetas (marcas de tiempo)
        // y los valores en el archivo
        labels.forEach((l, i) => {
            csvContent += `${l},${corrientes[i] || 0}\n`;
        });
    } else if (param === 'temperatura') {  // Datos de temperatura
        csvContent += "Tiempo,Temperatura (C)\n";  // Encabezado del archivo
        // Recorremos el array de etiquetas de eje horizontal y vamos guardando las etiquetas (marcas de tiempo)
        // y los valores en el archivo
        labels.forEach((l, i) => {
            csvContent += `${l},${temperaturas[i] || 0}\n`;
        });
    } else {  // Todos los datos
        csvContent += "Tiempo,Voltaje (V),Corriente (mA),Temperatura (C)\n";  // Encabezado del archivo
        // Recorremos el array de etiquetas de eje horizontal y vamos guardando las etiquetas (marcas de tiempo)
        // y los valores en el archivo
        labels.forEach((l, i) => {
            csvContent += `${l},${voltajes[i] || 0},${corrientes[i] || 0},${temperaturas[i] || 0}\n`;
        });
    }
    // Codificamos el URI (identificador de recursos uniforme)
    const encodedUri = encodeURI(csvContent);
    // Creamos un enlace (etiqueta <a>)
    const link = document.createElement("a");
    // Cambiamos la propiedad href, asignándole el URI
    link.setAttribute("href", encodedUri);
    // Cambiamos la propiedad download, asignándole el nombre del archivo que deseamos descargar
    link.setAttribute("download", `datos_sesion_${param}.csv`);
    // Agregamos el enlace como elemento hijo del body
    document.body.appendChild(link);
    // Hacemos click en el enlace para descargar la imagen
    link.click();
    // Quitamos el enlace (elemento hijo del body)
    document.body.removeChild(link);
}

// --- API CALLS ---
// ===== CONSULTAS A LA API REST / SERVIDOR =====
/* Las funciones que realizan consultas a la API son asincrónicas ya que esperan recibir una respuesta del
servidor (el resultado de una promesa). Para esperar el resultado de una promesa se usa la palabra await.
En todos los casos hay un bloque try/catch, lo cual nos permite capturar errores en la petición HTTP */
// Función que se ejecuta cuando se presionan los botones del panel lateral izquierdo (carga, descarga,
// test auto y parada)
async function enviarComando(accion) {
    try {
        // Hacemos una consulta POST a la ruta /comando/{accion} para realizar una acción
        await fetch(`${API}/comando/${accion}`, { method: 'POST' });
    } catch(e) { console.error('Error comando:', e); }
}

// Función para modificar la cantidad de ciclos del Test Auto mediante los botones del stepper (+ / -)
function modificarCiclosAuto(delta) {
    const input = document.getElementById('input-ciclos-auto');
    if (!input) return;
    let actual = parseInt(input.value) || 1;
    let nuevo = actual + delta;
    if (nuevo < 1) nuevo = 1;
    if (nuevo > 10) nuevo = 10;
    input.value = nuevo;
}

// Función que inicia el ciclo automático configurable con la cantidad de repeticiones especificada
function iniciarTestAuto() {
    const inputCiclos = document.getElementById('input-ciclos-auto');
    const n = inputCiclos ? parseInt(inputCiclos.value) || 1 : 1;
    enviarComando(`START_AUTO:${n}`);
}

// Función que se ejecuta al presionar el botón OK que se encuentra a la derecha del campo de texto para ingresar
// la capacidad nominal
async function configurarNominal() {
    // Obtenemos el valor del campo de texto
    const val = document.getElementById('input-cap').value;
    try {
        // Hacemos una petición POST a la ruta /set_nominal para cambiar la capacidad nominal, la cual se pasa
        // como parámetro de consulta
        await fetch(`${API}/set_nominal?valor=${val}`, { method: 'POST' });
    } catch(e) { console.error(e); }
}

// Función que se ejecuta al presionar el botón "Asignar" del encabezado de la página
async function asignarQR() {
    // Obtenemos el valor del campo de texto para ingresar el QR y eliminamos los espacios en blanco al comienzo y al final
    const qr = document.getElementById('input-qr').value.trim();
    // Si no se ingresó QR o su longitud es menor a 2, finalizamos la ejecución de la función
    if (!qr || qr.length < 2) return;
    try {
        // Realizamos una consulta POST a la ruta /set_qr para publicar el QR, el cual se pasa como parámetro
        // de consulta. El qr es un componente URI que debe estar codificado
        const r = await fetch(`${API}/set_qr?qr=${encodeURIComponent(qr)}`, { method: 'POST' });
        // Convertimos la respuesta a formato JSON
        const d = await r.json();
        if (d.status === 'QR asignado') {
            // Si el QR fue asignado correctamente, actualizamos el QR en el panel lateral izquierdo
            document.getElementById('val-qr').textContent = qr;
        }
    } catch(e) { console.error(e); }
}
// ID del timer para hacer un encuestado (polling) del QR cada 1 segundo
let intervalPollQR = null;

// Función que se ejecuta cuando se presiona el botón "Escanear" del encabezado de la página, lo que permite
// activar el lector por hardware en la ESP32-CAM y esperar el código QR decodificado
async function escanearConCamara() {
    // Obtenemos el elemento correspondiente a la pantalla secundaria de la cámara
    const modal = document.getElementById('modal-camara');
    // Obtenemos el valor del QR
    const qrInicial = document.getElementById('val-qr').textContent;
    // Contador para cerrar la ventana en caso que haya pasado el timeout de 30 segundos para el escaneo
    let contTimeout = 0;

    // Disparamos la orden de escaneo al ESP32 Master vía MQTT a través del endpoint /escanear
    try {
        await fetch(`${API}/escanear`, { method: 'POST' });
    } catch(e) {
        console.error("Error al disparar escaneo:", e);
    }

    // Abrir modal de espera
    // Agregamos una nueva clase al contenedor de la pantalla secundaria, para modificar la propiedad display en el CSS
    modal.classList.add('open');

    // Polling del estado actual cada 1 s para comprobar si el QR fue recibido y decodificado por la ESP32-CAM
    intervalPollQR = setInterval(async () => {
        try {
            // Hacemos una consulta GET a la ruta /estado_actual para obtener el estado actual del sistema
            const r = await fetch(`${API}/estado_actual?_=${Date.now()}`);
            // Convertimos la respuesta a formato JSON
            const d = await r.json();
            contTimeout += 1;
            if (d.qr_actual && d.qr_actual !== 'NINGUNA' && d.qr_actual !== qrInicial) {
                // Si qr_actual es no nulo, no vale "NINGUNA" y es distinto del QR leído inicialmente de la página,
                // lo actualizamos
                const nuevoQR = d.qr_actual;
                // Cerramos la pantalla del lector de cámara
                cerrarModalCamara();
                // Actualizamos el QR en el campo de texto del header
                //document.getElementById('input-qr').value = nuevoQR;
                // Actualizamos el QR en el panel izquierdo
                document.getElementById('val-qr').textContent = nuevoQR;
                alert(`¡QR decodificado con éxito!: ${nuevoQR}`);
            }
            else if(contTimeout == 30){
                // Cerramos la pantalla del lector de cámara
                cerrarModalCamara();
                alert("No se detectó ningún QR");
            }
        } catch(e) {
            console.error("Error en polling de QR:", e);
        }
    }, 1000);
}

// Función que se ejecuta cuando se presiona el botón "Cancelar" de la ventana de escaneo,
// para enviar a la ESP-CAM una orden para cancelar el escaneo de la batería
async function cancelarEscaneo() {
    // Disparamos la orden de cancelación de escaneo al ESP32 Master vía MQTT a través del endpoint /cancelar
    try {
        await fetch(`${API}/cancelar`, { method: 'POST' });
        // Cerramos la pantalla de escaneo
        const modal = document.getElementById('modal-camara');
        modal.classList.remove('open');
    } catch(e) {
        console.error("Error al cancelar escaneo:", e);
    }
}

// Función que se ejecuta cuando se presiona el botón "Cambiar perfil"
function cambiarPerfil() {
    // Obtenemos el elemento correspondiente a la pantalla secundaria de cambiar perfil
    const modal = document.getElementById('modal-cambiar-perfil');
    // Abrir modal de espera
    // Agregamos una nueva clase al contenedor de la pantalla secundaria, para modificar la propiedad display en el CSS
    modal.classList.add('open');
    // Borramos el mensaje de error que se muestra debajo del botón de Actualizar
    document.getElementById("error-msg-perfil").textContent = "";
    // Mostramos en los campos de texto (inputs) los valores correspondientes al tipo seleccionado
    const perfil = document.getElementById('select-perfil').value;
    if(perfil != "PERSONALIZADO"){
        // Si el perfil seleccionado es alguno de los perfiles que está en la lista desplegable, mostramos
        // los umbrales de tensión
        document.getElementById("input-v-carga").value = PERFILES.get(perfil).vCorteCarga;
        document.getElementById("input-v-desc").value = PERFILES.get(perfil).vCorteDescarga;
        // Ponemos la corriente objetivo en un valor por defecto de 900 mA
        document.getElementById("input-i-obj").value = 900;
    }
    else {
        // Si el perfil seleccionado es personalizado, borramos todos los umbrales
        document.getElementById("input-v-carga").value = "";
        document.getElementById("input-v-desc").value = "";
        document.getElementById("input-i-obj").value = "";
    }
}

// Función que cierra la pantalla secundaria del lector de la cámara. Se ejecuta cuando se recibe el QR
function cerrarModalCamara() {
    // Obtenemos el elemento correspondiente a la pantalla secundaria de la cámara
    const modal = document.getElementById('modal-camara');
    // Quitamos la clase open que agregamos inicialmente al contenedor de la pantalla secundaria
    modal.classList.remove('open');
    // Finalizamos el encuestado del estado actual si la función fue invocada por escanearConCamara()
    if (intervalPollQR) {
        // Limpiamos la variable que almacena el timer
        clearInterval(intervalPollQR);
        intervalPollQR = null;
    }
}

// --- ESTADO POLLING ---
let ultimoEstado = 'REPOSO';  // Variable que almacena el último estado del sistema
// Función que se ejecuta indefinidamente cada 2 segundos para actualizar el estado del sistema
async function actualizarEstado() {
    try {
        // Hacemos una consulta GET a la ruta /estado_actual
        const r = await fetch(`${API}/estado_actual?_=${Date.now()}`);
        // Convertimos la respuesta a formato JSON
        const d = await r.json();

        // Connection
        // Verificamos si el servidor se conectó al ESP32
        if (d.esp32_online) {
            connOk = true;  // Bandera para indicar que la conexión con el ESP32 fue exitosa
            // Quitamos la clase off añadida al puntito
            document.getElementById('conn-dot').classList.remove('off');
            // Actualizamos el indicador de conexión ubicado en la parte derecha del encabezado de la página
            document.getElementById('conn-text').textContent = 'Conectado';
        } else {
            // Agregamos una clase llamada "off" al puntito que se encuentra a la izquierda del indicador de conexión
            // del header, para modificar sus propiedades de CSS
            document.getElementById('conn-dot').classList.add('off');
            // Actualizamos el indicador de conexión ubicado en la parte derecha del encabezado de la página
            document.getElementById('conn-text').textContent = 'ESP32 Offline';
        }

        // Values
        // Actualizamos los elementos que muestran los valores de las variables en el panel izquierdo
        // con los valores recibidos del servidor
        document.getElementById('val-qr').textContent = d.qr_actual || 'NINGUNA';  // QR de la batería
        // Tensión (la expresamos con dos decimales)
        document.getElementById('val-v').innerHTML = d.v_actual.toFixed(2);
        // Corriente (la redondeamos a entero)
        document.getElementById('val-i').innerHTML = Math.round(d.i_actual);
        document.getElementById('val-t').textContent = d.t_actual.toFixed(1);  // Temperatura (la expresamos con un decimal)
        document.getElementById('val-tmax').textContent = d.temp_maxima.toFixed(1);  // Temperatura máxima (un decimal)
        // Capacidad (la expresamos con tres decimales)
        document.getElementById('val-ah').innerHTML = d.ah_acumulado.toFixed(3);
        // Resistencia interna (la redondeamos a entero)
        document.getElementById('val-ri').innerHTML = Math.round(d.ri_calculada);

        // Chart values
        // Actualizamos los valores que se muestran a la derecha de los gráficos
        document.getElementById('chart-val-v').textContent = d.v_actual.toFixed(2) + ' V';
        document.getElementById('chart-val-i').textContent = Math.round(d.i_actual) + ' mA';
        document.getElementById('chart-val-t').textContent = d.t_actual.toFixed(1) + ' °C';

        // Update QR input placeholder
        if (d.qr_actual) {
            document.getElementById('input-qr').placeholder = d.qr_actual;
        }

        // Actualizamos el perfil de la batería
        document.getElementById("perfil").textContent = d.perfil_actual;
        // Actualizamos los umbrales de tensión y corriente del perfil seleccionado
        document.getElementById("val-v-carga").textContent = d.v_corte_carga;
        document.getElementById("val-v-desc").textContent = d.v_corte_descarga;
        document.getElementById("val-i-obj").textContent = d.i_objetivo;

        // Estado badge
        // Actualizamos el estado del sistema en el panel izquierdo
        const badge = document.getElementById('badge-estado');
        const est = d.estado_anterior || 'REPOSO';
        badge.textContent = est;
        // Agregamos una clase a la etiqueta <span> que muestra el estado según el valor recibido, para poder 
        // aplicarle nuevas propiedades de CSS
        badge.className = 'estado-badge ' + (
            est === 'CARGANDO' ? 'estado-cargando' :
            est === 'DESCARGANDO' ? 'estado-descargando' :
            est === 'ERROR_TEMP' ? 'estado-error' : 'estado-reposo'
        );

        // Chart
        // Obtenemos la hora actual y la convertimos a string
        const ahora = new Date().toLocaleTimeString('es-AR', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
        
        // Actualizamos el gráfico de tensión
        chartV.data.labels.push(ahora);  // Agregamos etiquetas al eje horizontal (marcas de tiempo)
        chartV.data.datasets[0].data.push(d.v_actual);  // Agregamos valores al gráfico
        if (chartV.data.labels.length > maxPoints) {
            // Desplazamos las marcas de tiempo y los datos para mantener el número de puntos seleccionado
            // en el panel de opciones de gráfico (que se abre al presionar el botón "Controles")
            chartV.data.labels.shift();
            chartV.data.datasets[0].data.shift();
        }
        chartV.update();

        // Actualizamos el gráfico de corriente
        chartI.data.labels.push(ahora);  // Agregamos etiquetas al eje horizontal (marcas de tiempo)
        chartI.data.datasets[0].data.push(d.i_actual);  // Agregamos valores al gráfico
        if (chartI.data.labels.length > maxPoints) {
            // Desplazamos las marcas de tiempo y los datos para mantener el número de puntos seleccionado
            // en el panel de opciones de gráfico (que se abre al presionar el botón "Controles")
            chartI.data.labels.shift();
            chartI.data.datasets[0].data.shift();
        }
        chartI.update();

        // Actualizamos el gráfico de temperatura
        chartT.data.labels.push(ahora);  // Agregamos etiquetas al eje horizontal (marcas de tiempo)
        chartT.data.datasets[0].data.push(d.t_actual);  // Agregamos valores al gráfico
        if (chartT.data.labels.length > maxPoints) {
            // Desplazamos las marcas de tiempo y los datos para mantener el número de puntos seleccionado
            // en el panel de opciones de gráfico (que se abre al presionar el botón "Controles")
            chartT.data.labels.shift();
            chartT.data.datasets[0].data.shift();
        }
        chartT.update();

        // Auto-refresh tables on state change
        if (est === 'REPOSO' && ultimoEstado !== 'REPOSO') {
            // Si el sistema pasa al estado de reposo, actualizamos las tablas de inventario e historial de ciclos
            cargarInventario();
            cargarHistorial();
        }
        ultimoEstado = est;  // Actualizamos el estado

    } catch(e) {
        if (connOk) {
            // Si hubo un error, modificamos el indicador de conexión para indicar que se perdió la conexión
            connOk = false;
            document.getElementById('conn-dot').classList.add('off');
            document.getElementById('conn-text').textContent = 'Sin conexión';
        }
    }
}

// --- INVENTARIO ---
// Función que actualiza la tabla inferior que muestra el inventario de baterías. Se ejecuta al presionar el
// botón "Refrescar" y también cuando el sistema pasa al estado de reposo
async function cargarInventario() {
    try {
        // Hacemos una consulta GET a la ruta /baterias y obtenemos la respuesta en formato JSON (evitando caché)
        const r = await fetch(`${API}/baterias?_=${Date.now()}`);
        const bats = await r.json();
        // Obtenemos el cuerpo de la tabla de inventario y la lista desplegable que permite filtrar por QR
        // que se encuentra por encima de la tabla de historial de ciclos
        const tbody = document.getElementById('tbody-inventario');
        const filtro = document.getElementById('filtro-qr');
        const filtroActual = filtro.value;

        tbody.innerHTML = '';  // Quitamos temporalmente el contenido de la tabla
        // Update filter options
        // Recorremos el array de baterías recibido y extraemos los QRs
        const qrs = (bats || []).map(b => b.qr_id);
        // Actualizamos la lista desplegable agregando todas las baterías recibidas
        filtro.innerHTML = '<option value="">Todas</option>' + qrs.map(q =>
            `<option value="${q}" ${q === filtroActual ? 'selected' : ''}>${q}</option>`
        ).join('');
        // Recorremos el array de baterías recibido y actualizamos la tabla de inventario
        for (const bat of (bats || [])) {
            const tr = document.createElement('tr');  // Creamos una fila
            tr.className = 'clickable';  // Agregamos a la fila la clase "clickable" para agregar un estilo de CSS
            // Al hacer click sobre alguna de las filas, se muestra una ventana con el detalle de esa batería
            tr.onclick = () => verDetalle(bat.qr_id);
            const estInicial = bat.estado_inicial || 'Sin especificar';
            const estGeneral = bat.estado_general || 'OPERATIVO';
            // Agregamos los valores recibidos a la tabla con acciones Ver, Editar y Borrar
            tr.innerHTML = `
                <td class="td-qr">${bat.qr_id}</td>
                <td class="td-mono" style="font-size:11px; color:var(--text-secondary); max-width:140px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;" title="${estInicial}">${estInicial}</td>
                <td class="td-mono">${bat.fecha_registro ? new Date(bat.fecha_registro).toLocaleDateString('es-AR') : '—'}</td>
                <td class="td-mono">${bat.ciclos_count !== undefined ? bat.ciclos_count : '—'}</td>
                <td style="text-align: right;">
                    <div style="display: flex; gap: 4px; justify-content: flex-end;">
                        <button class="btn-sm" title="Ver detalle e historial" onclick="event.stopPropagation(); verDetalle('${bat.qr_id}')">Ver</button>
                        <button class="btn-sm" title="Editar código o estado inicial" onclick="event.stopPropagation(); abrirModalEditar('${bat.qr_id}', '${estInicial.replace(/'/g, "\\'")}', '${estGeneral}')" style="color:var(--accent-yellow); border-color: rgba(251,191,36,0.3);">✏️</button>
                        <button class="btn-sm" title="Eliminar batería y ciclos asociados" onclick="event.stopPropagation(); confirmarBorrado('${bat.qr_id}')" style="color:var(--accent-red); border-color: rgba(248,113,113,0.3);">🗑️</button>
                    </div>
                </td>
            `;
            // Agregamos la fila como elemento hijo del cuerpo de la tabla
            tbody.appendChild(tr);
        }
    } catch(e) { console.error(e); }
}

// --- HISTORIAL ---
// Función que actualiza la tabla inferior que muestra el historial de ciclos de carga/descarga. Se ejecuta
// al presionar el botón "Refrescar" y también cuando el sistema pasa al estado de reposo
async function cargarHistorial() {
    try {
        // Obtenemos el QR de la lista desplegable ubicada en la parte superior de la tabla
        const filtroQR = document.getElementById('filtro-qr').value;
        // Construimos el URL para hacer la petición a la ruta. Si se seleccionó un QR, hacemos una petición
        // a la ruta /historial/{filtroQR} para obtener el hisotrial de una sola batería, mientras que si
        // no se seleccionó ningún QR, hacemos una petición a la ruta /historial para obtener el historial de
        // ciclos de todas las baterías
        const baseUrl = filtroQR ? `${API}/historial/${encodeURIComponent(filtroQR)}` : `${API}/historial`;
        const url = `${baseUrl}${baseUrl.includes('?') ? '&' : '?'} _=${Date.now()}`;
        // Hacemos la petición a la ruta y convertimos la respuesta a JSON
        const r = await fetch(url);
        const hist = await r.json();
        // Quitamos momentáneamente el contenido de la tabla
        const tbody = document.getElementById('tbody-historial');
        tbody.innerHTML = '';
        // Iteramos sobre el array de ciclos recibido y actualizamos la tabla
        (hist || []).forEach(c => {
            const tr = document.createElement('tr');  // Creamos una fila de la tabla
            // Redondeamos el porcentaje de SoH a un decimal
            const sohVal = typeof c.soh_porcentaje === 'number' ? c.soh_porcentaje : 0;
            const soh = sohVal > 0 ? sohVal.toFixed(1) + '%' : '—';
            
            // Agregamos una etiqueta a la columna TIPO en función del estado
            const tipoStr = c.tipo_ciclo || '';
            let tipoLabel = '❓ Desconocido';
            if(tipoStr.includes('DESCARGANDO') || tipoStr.includes('DESCARGA FINAL')) {
                tipoLabel = '🔋 Descarga';
            }
            else if(tipoStr.includes('CARGANDO')) {
                tipoLabel = '⚡ Carga';
            }
            if (tipoStr.includes('Interrumpido')) {
                tipoLabel += ' <span style="color:var(--accent-orange);font-size:10px;font-weight:600;">⚠️ Interr</span>';
            }
            const capVal = typeof c.capacidad_ah === 'number' ? c.capacidad_ah : 0;
            const riVal = typeof c.resistencia_interna === 'number' ? c.resistencia_interna : 0;
            const tempVal = typeof c.temp_maxima === 'number' ? c.temp_maxima : 0;

            // Agregamos los valores recibidos a la tabla
            tr.innerHTML = `
                <td class="td-qr">${c.qr_id || '—'}</td>
                <td class="td-mono">${tipoLabel}</td>
                <td class="td-mono">${capVal.toFixed(3)}</td>
                <td class="td-soh">${soh}</td>
                <td class="td-mono">${Math.round(riVal)}</td>
                <td class="td-mono" style="color:var(--accent-red);">${tempVal.toFixed(1)}°</td>
            `;
            // Agregamos la fila como elemento hijo del cuerpo de la tabla
            tbody.appendChild(tr);
        });
    } catch(e) { console.error(e); }
}

// --- MODAL DETALLE ---
// Función que se ejecuta cuando se presiona el botón "Ver" ubicado a la derecha de las filas de la tabla de
// inventario, o cuando se presiona alguna fila de esa tabla
async function verDetalle(qr) {
    // Agregamos a la ventana la clase "open" para modificar sus propiedades al abrirla
    document.getElementById('modal-detalle').classList.add('open');
    // Modificamos el título de la ventana que muestra el detalle de la batería
    document.getElementById('modal-titulo').textContent = `Batería: ${qr}`;
    // Agregamos temporalmente un indicador de "Cargando..." al contenedor que agrupa las tarjetas que muestran
    // las variables de la batería
    document.getElementById('modal-stats').innerHTML = '<div class="modal-stat"><div class="metric-label">Cargando...</div></div>';
    // Quitamos temporalmente el contenido de la tabla
    document.getElementById('tbody-modal-ciclos').innerHTML = '';

    try {
        // Hacemos la consulta a la ruta /bateria/{qr} y convertimos la respuesta a JSON (evitando caché)
        const r = await fetch(`${API}/bateria/${encodeURIComponent(qr)}?_=${Date.now()}`);
        const data = await r.json();

        if (data.status === 'Error') {
            // Si hubo un error, cambiamos el indicador de "Cargando..." a "No encontrada" y finalizamos la
            // ejecución de la función
            document.getElementById('modal-stats').innerHTML = '<div class="modal-stat"><div class="metric-value small">No encontrada</div></div>';
            return;
        }
        // Obtenemos los valores recibidos
        const ciclos = data.ciclos || [];  // Array con el detalle de los ciclos
        const descargas = ciclos.filter(c => (c.tipo_ciclo || '').includes('DESC'));  // Array que guarda los ciclos de descarga
        // Último SoH y resistencia interna (en caso que haya habido una descarga)
        const ultimoSoH = descargas.length > 0 && typeof descargas[0].soh_porcentaje === 'number' ? descargas[0].soh_porcentaje : 0;
        const ultimaRI = descargas.length > 0 && typeof descargas[0].resistencia_interna === 'number' ? descargas[0].resistencia_interna : 0;

        // Stats
        // Actualizamos las tarjetas ubicadas en la parte superior de la ventana que muestra el detalle de la batería
        document.getElementById('modal-stats').innerHTML = `
            <div class="modal-stat">
                <div class="metric-label">Total ciclos</div>
                <div class="metric-value" style="color:var(--accent-cyan);">${data.total_ciclos || ciclos.length}</div>
            </div>
            <div class="modal-stat">
                <div class="metric-label">Último SoH</div>
                <div class="metric-value" style="color:var(--accent-green);">${ultimoSoH > 0 ? ultimoSoH.toFixed(1) + '%' : '—'}</div>
            </div>
            <div class="modal-stat">
                <div class="metric-label">Última RI</div>
                <div class="metric-value" style="color:var(--accent-orange);">${ultimaRI > 0 ? Math.round(ultimaRI) + ' mΩ' : '—'}</div>
            </div>
            <div class="modal-stat">
                <div class="metric-label">Estado Inicial (Pack)</div>
                <div class="metric-value small" style="color:var(--accent-yellow); font-size:12px; text-overflow:ellipsis; overflow:hidden; white-space:nowrap;" title="${data.estado_inicial || 'Sin especificar'}">${data.estado_inicial || 'Sin especificar'}</div>
            </div>
        `;

        // SoH Chart
        // Actualizamos el gráfico de SoH
        const ctxT = document.getElementById('chartTendencia').getContext('2d');
        if (chartTendencia) chartTendencia.destroy();  // Quitamos momentáneamente el gráfico
        // Invertimos los valores del array de descargas para mostrar el valor de SoH del ciclo más reciente a la derecha del gráfico
        const descOrden = [...descargas].reverse();
        // Creamos el gráfico de SoH
        chartTendencia = new Chart(ctxT, {
            type: 'line',  // Tipo de gráfico (línea)
            data: {
                labels: descOrden.map((_, i) => `Ciclo ${i + 1}`),  // Etiquetas del eje horizontal (ciclos)
                datasets: [{
                    label: 'SoH %',
                    data: descOrden.map(c => typeof c.soh_porcentaje === 'number' ? c.soh_porcentaje : 0),  // Datos del gráfico (se obtienen del array de descargas invertido)
                    borderColor: '#4ade80',  // Color del borde
                    backgroundColor: 'rgba(74,222,128,0.05)',  // Color de fondo
                    fill: true,
                    tension: 0.3,
                    borderWidth: 1.5,  // Ancho de borde
                    pointRadius: 3,  // Radio del punto del gráfico
                    pointBackgroundColor: '#4ade80'  // Color de fondo de los puntos del gráfico
                }]
            },
            options: {
                responsive: true,  // Diseño responsivo
                maintainAspectRatio: false,  // No mantenemos la razón de aspecto
                animation: false,  // Sin animación 
                plugins: { legend: { display: false } },  // Ocultamos la leyenda
                scales: {
                    // Escala horizontal
                    x: { ticks: { color: '#6b7280', font: { size: 10 } }, grid: { color: '#1f2233' } },
                    // Escala vertical de 0 a 100%
                    y: { min: 0, max: 100, ticks: { color: '#6b7280', font: { size: 10 } }, grid: { color: '#1f2233' } }
                }
            }
        });

        // Cycles table
        // Actualizamos la tabla de ciclos que se encuentra por debajo del gráfico de SoH en la ventana 
        // que muestra el detalle de baterías
        const tbody = document.getElementById('tbody-modal-ciclos');
        tbody.innerHTML = '';  // Quitamos momentáneamente el contenido de la tabla
        // Recorremos el array de ciclos y actualizamos la tabla
        ciclos.forEach(c => {
            const tr = document.createElement('tr');  // Creamos una fila
            // Convertimos la fecha y hora de inicio recibida a string
            const fecha = c.fecha_inicio ? new Date(c.fecha_inicio).toLocaleString('es-AR', { day:'2-digit', month:'2-digit', hour:'2-digit', minute:'2-digit' }) : '—';
            // Expresamos el SoH como un porcentaje con un solo decimal
            const sohVal = typeof c.soh_porcentaje === 'number' ? c.soh_porcentaje : 0;
            const soh = sohVal > 0 ? sohVal.toFixed(1) + '%' : '—';
            
            // Bandera que indica si hubo una carga
            const tipoStr = c.tipo_ciclo || '';
            const esCarga = tipoStr.includes('CARGANDO');  
            // Bandera que indica si hubo una interrupción en la carga o descarga
            const esInterrumpido = tipoStr.includes('Interrumpido');
            // Ícono para representar el estado de carga
            const icon = esCarga ? '⚡' : '🔋';
            // Ícono para indicar si la carga fue interrumpida
            const statusIndicator = esInterrumpido ? ' ⚠️' : '';

            const capVal = typeof c.capacidad_ah === 'number' ? c.capacidad_ah : 0;
            const riVal = typeof c.resistencia_interna === 'number' ? c.resistencia_interna : 0;
            const vFinVal = typeof c.voltaje_final === 'number' ? c.voltaje_final.toFixed(2) : '—';
            const tempVal = typeof c.temp_maxima === 'number' ? c.temp_maxima.toFixed(1) : '—';

            // Agregamos los valores recibidos a la tabla
            tr.innerHTML = `
                <td class="td-mono">${icon}${statusIndicator}</td>
                <td class="td-mono">${fecha}</td>
                <td class="td-mono">${capVal.toFixed(3)}</td>
                <td class="td-soh">${soh}</td>
                <td class="td-mono">${Math.round(riVal)}</td>
                <td class="td-mono" style="color:var(--accent-cyan);">${vFinVal}</td>
                <td class="td-mono" style="color:var(--accent-red);">${tempVal}°</td>
            `;
            // Agregamos la fila como elemento hijo del cuerpo de la tabla
            tbody.appendChild(tr);
        });
    } catch(e) { console.error(e); }
}
// Función que se ejecuta al cerrar la ventana que muestra el detalle de la batería
function cerrarModal() {
    // Quitamos la clase "open" de la pantalla de detalle de la batería
    document.getElementById('modal-detalle').classList.remove('open');
}

// --- MODALES: EDICIÓN, IMPORTACIÓN, CONFIRMACIÓN Y EXPORTACIÓN ---

// Función para descargar en formato JSON el inventario completo y los ciclos históricos
function exportarJSON() {
    window.location.href = `${API}/exportar_json`;
}

// Modal Editar Batería
function abrirModalEditar(qr, estadoInicial, estadoGeneral) {
    document.getElementById('edit-qr-original').value = qr;
    document.getElementById('edit-qr-nuevo').value = qr;
    document.getElementById('edit-estado-inicial').value = (estadoInicial && estadoInicial !== 'Sin especificar') ? estadoInicial : '';
    document.getElementById('edit-estado-general').value = estadoGeneral || 'OPERATIVO';
    document.getElementById('modal-editar').classList.add('open');
}

function cerrarModalEditar() {
    document.getElementById('modal-editar').classList.remove('open');
}

async function guardarEdicionBateria(e) {
    if (e) e.preventDefault();
    const originalQR = document.getElementById('edit-qr-original').value;
    const nuevoQR = document.getElementById('edit-qr-nuevo').value.trim();
    const estadoInicial = document.getElementById('edit-estado-inicial').value.trim();
    const estadoGeneral = document.getElementById('edit-estado-general').value;

    if (!nuevoQR) {
        alert("El código QR no puede estar vacío");
        return;
    }

    try {
        const payload = {
            nuevo_qr_id: nuevoQR,
            estado_inicial: estadoInicial || "Sin especificar",
            estado_general: estadoGeneral || "OPERATIVO"
        };
        const res = await fetch(`${API}/bateria/${encodeURIComponent(originalQR)}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        if (data.status === 'Éxito') {
            cerrarModalEditar();
            cargarInventario();
            cargarHistorial();
        } else {
            alert(`Error al guardar: ${data.msg || 'No se pudo actualizar'}`);
        }
    } catch(err) {
        console.error("Error al actualizar batería:", err);
        alert("Error de conexión al intentar actualizar");
    }
}

// Modal Confirmación Genérica
let accionConfirmar = null;

function confirmarBorrado(qr) {
    document.getElementById('modal-confirm-titulo').textContent = `Eliminar Batería ${qr}`;
    document.getElementById('modal-confirm-mensaje').textContent = `¿Estás seguro de que deseás eliminar la batería "${qr}" y todos sus ciclos históricos asociados? Esta acción no se puede deshacer.`;
    accionConfirmar = async () => {
        try {
            const res = await fetch(`${API}/bateria/${encodeURIComponent(qr)}?_=${Date.now()}`, { method: 'DELETE' });
            const data = await res.json();
            if (data.status === 'Éxito') {
                cerrarModalConfirmar();
                await cargarInventario();
                await cargarHistorial();
            } else {
                alert(`Error al eliminar: ${data.msg}`);
            }
        } catch(err) { console.error(err); }
    };
    document.getElementById('btn-confirm-action').onclick = accionConfirmar;
    document.getElementById('modal-confirmar').classList.add('open');
}

function confirmarLimpieza(tipo) {
    const titulo = tipo === 'ciclos' ? 'Limpiar Historial de Ciclos' : 'Limpiar Todo el Inventario';
    const mensaje = tipo === 'ciclos' 
        ? '¿Estás seguro de que deseás borrar todo el historial de ciclos? Los datos de las baterías registradas se conservarán.'
        : '¿Estás seguro de que deseás borrar todas las baterías del inventario y todo el historial de ciclos? La base de datos quedará totalmente en blanco.';
    
    document.getElementById('modal-confirm-titulo').textContent = titulo;
    document.getElementById('modal-confirm-mensaje').textContent = mensaje;
    accionConfirmar = async () => {
        try {
            const res = await fetch(`${API}/limpiar_datos?tipo=${tipo}&_=${Date.now()}`, { method: 'DELETE' });
            const data = await res.json();
            if (data.status === 'Éxito') {
                cerrarModalConfirmar();
                await cargarInventario();
                await cargarHistorial();
            } else {
                alert(`Error al limpiar: ${data.msg}`);
            }
        } catch(err) { console.error(err); }
    };
    document.getElementById('btn-confirm-action').onclick = accionConfirmar;
    document.getElementById('modal-confirmar').classList.add('open');
}

function cerrarModalConfirmar() {
    document.getElementById('modal-confirmar').classList.remove('open');
    accionConfirmar = null;
}

// Modal Importar JSON
function abrirModalImportar() {
    document.getElementById('textarea-importar-json').value = '';
    const feedback = document.getElementById('import-msg-feedback');
    feedback.style.display = 'none';
    feedback.textContent = '';
    const fileInput = document.getElementById('file-importar-json');
    if (fileInput) fileInput.value = '';
    document.getElementById('modal-importar').classList.add('open');
}

function cerrarModalImportar() {
    document.getElementById('modal-importar').classList.remove('open');
}

function procesarArchivoImportar(e) {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = function(evt) {
        document.getElementById('textarea-importar-json').value = evt.target.result;
    };
    reader.readAsText(file);
}

async function ejecutarImportacion() {
    const texto = document.getElementById('textarea-importar-json').value.trim();
    const feedback = document.getElementById('import-msg-feedback');
    feedback.style.display = 'block';

    if (!texto) {
        feedback.style.color = '#ef4444';
        feedback.textContent = 'Pegá o seleccioná un archivo JSON primero.';
        return;
    }

    let parsed;
    try {
        parsed = JSON.parse(texto);
    } catch(err) {
        feedback.style.color = '#ef4444';
        feedback.textContent = 'JSON inválido. Revisá la sintaxis del archivo.';
        return;
    }

    // Normalizar a formato lista de objetos [{qr_id, estado_inicial, estado_general}]
    let lista = [];
    if (Array.isArray(parsed)) {
        lista = parsed.map(item => {
            if (typeof item === 'string') {
                return { qr_id: item.trim(), estado_inicial: 'Sin especificar', estado_general: 'OPERATIVO' };
            } else if (item && typeof item === 'object') {
                return {
                    qr_id: (item.qr_id || item.qr || item.id || '').trim(),
                    estado_inicial: (item.estado_inicial || item.estado || 'Sin especificar').trim(),
                    estado_general: (item.estado_general || 'OPERATIVO').trim()
                };
            }
            return null;
        }).filter(item => item && item.qr_id);
    } else if (parsed && typeof parsed === 'object') {
        const arr = parsed.inventario || parsed.baterias || [];
        if (Array.isArray(arr)) {
            lista = arr.map(item => ({
                qr_id: (item.qr_id || item.qr || item.id || '').trim(),
                estado_inicial: (item.estado_inicial || item.estado || 'Sin especificar').trim(),
                estado_general: (item.estado_general || 'OPERATIVO').trim()
            })).filter(item => item.qr_id);
        }
    }

    if (lista.length === 0) {
        feedback.style.color = '#ef4444';
        feedback.textContent = 'No se encontraron baterías válidas en el JSON proporcionado.';
        return;
    }

    try {
        feedback.style.color = '#38bdf8';
        feedback.textContent = `Importando ${lista.length} baterías...`;
        const res = await fetch(`${API}/importar_inventario`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(lista)
        });
        const data = await res.json();
        if (data.status === 'Éxito') {
            feedback.style.color = '#4ade80';
            feedback.textContent = `¡Éxito! Insertadas: ${data.insertadas}, Actualizadas: ${data.actualizadas}.`;
            cargarInventario();
            setTimeout(cerrarModalImportar, 1400);
        } else {
            feedback.style.color = '#ef4444';
            feedback.textContent = `Error: ${data.msg || 'No se pudo procesar la importación'}`;
        }
    } catch(err) {
        feedback.style.color = '#ef4444';
        feedback.textContent = 'Error de conexión con el servidor.';
    }
}

// Función que se ejecuta al seleccionar un perfil en la lista desplegable de la pantalla que se abre al presionar el botón "Cambiar perfil"
function seleccionarPerfil(tipo) {
    // Borramos el mensaje de error que se muestra debajo del botón de Actualizar
    document.getElementById("error-msg-perfil").textContent = "";
    if(tipo != "PERSONALIZADO") {
        // Si se seleccionó alguno de los perfiles de batería de la lista, mostramos los
        // umbrales de ese perfil en los campos de texto
        document.getElementById("input-v-carga").value = PERFILES.get(tipo).vCorteCarga;
        document.getElementById("input-v-desc").value = PERFILES.get(tipo).vCorteDescarga;
        // Ponemos la corriente objetivo en un valor por defecto de 900 mA
        document.getElementById("input-i-obj").value = 900;
        //iObjetivo = 900;
    }
    else {
        // Si se seleccionó la opción Personalizado, borramos los valores de los inputs
        document.getElementById("input-v-carga").value = "";
        document.getElementById("input-v-desc").value = "";
        document.getElementById("input-i-obj").value = "";
    }
}

// Función que se ejecuta cuando hay algún cambio en los campos del formulario
function cambioValor() {
    // Borramos el mensaje de error que se muestra debajo del botón de Actualizar
    document.getElementById("error-msg-perfil").textContent = "";
    // Si cambiamos alguno de los umbrales de tensión o corriente, cambiamos el valor de la lista desplegable a "PERSONALIZADO"
    document.getElementById("select-perfil").value = "PERSONALIZADO";
}

// Función que se ejecuta al presionar el botón "Actualizar" de la pantalla para cambiar el perfil
document.getElementById("form-perfil").addEventListener("submit", async function(event) {
    // Evitamos que el navegador realice la acción por defecto al enviar el formulario (o sea, evitamos
    // que el navegador recargue toda la página)
    event.preventDefault();
    // Obtenemos el tipo de perfil accediendo al valor de la lista desplegable
    const tipo = document.getElementById("select-perfil").value;
    // Obtenemos los valores de los campos del formulario
    const vCorteCarga = document.getElementById("input-v-carga").value;
    const vCorteDesc = document.getElementById("input-v-desc").value;
    const iObjetivo = document.getElementById("input-i-obj").value;
    // Vefificamos que los valores no estén vacíos
    if(vCorteCarga == "" || vCorteDesc == "" || iObjetivo == ""){
        document.getElementById("error-msg-perfil").textContent = "Faltan valores de tensión y/o corriente";
        return;
    }
    else {
        document.getElementById("error-msg-perfil").textContent = "";
    }

    // Para cambiar el perfil, realizamos una petición tipo POST a la ruta /perfil
    try {
        // Hacemos una consulta POST a la ruta /perfil para cambiar el perfil, el cual se pasa como parámetro de consulta
        // Enviamos los umbrales de tensión y corriente como parámetros de consulta
        const r = await fetch(`${API}/perfil?tipo=${tipo}&v_c=${vCorteCarga}&v_d=${vCorteDesc}&i_obj=${iObjetivo}`, { method: 'POST' });
        // Convertimos la respuesta a formato JSON
        const d = await r.json();
        console.log("Respuesta: ", d);
        // Cerramos la ventana para cambiar el perfil
        document.getElementById('modal-cambiar-perfil').classList.remove('open');
        // Actualizamos el perfil en el panel izquierdo
        document.getElementById("perfil").textContent = tipo;
    } 
    catch(e) { console.error(e); }
});

/* Función que se ejecuta al cerrar la ventana para cambiar el perfil */
function cerrarModalCambiarPerfil() {
    document.getElementById('modal-cambiar-perfil').classList.remove('open');   
}

// Cerramos los modales al hacer click en el fondo oscuro
document.getElementById('modal-detalle').addEventListener('click', function(e) {
    if (e.target === this) cerrarModal();
});
document.getElementById('modal-camara').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalCamara();
});
document.getElementById('modal-editar').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalEditar();
});
document.getElementById('modal-importar').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalImportar();
});
document.getElementById('modal-confirmar').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalConfirmar();
});
document.getElementById('modal-cambiar-perfil').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalCambiarPerfil();
});

// Cerrar cualquier modal abierto al presionar Escape
document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') {
        cerrarModal();
        cerrarModalCamara();
        cerrarModalEditar();
        cerrarModalImportar();
        cerrarModalConfirmar();
        cerrarModalCambiarPerfil();
    }
});

// --- INIT ---
// Cada 2 segundos actualizamos el estado del sistema
setInterval(actualizarEstado, 2000);
// Actualizamos las tablas de inventario e historial de ciclos
cargarInventario();
cargarHistorial();