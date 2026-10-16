const API = window.location.origin;  // URL base de la página web
let maxPoints = 100;  // Número máximo de puntos de los gráficos en el panel derecho
let chartTendencia = null;  // Objeto que almacena el gráfico de tendencia de SoH
let connOk = true;  // Bandera que indica si hay conexión con el ESP32

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

// Función que se ejecuta cuando cambia el perfil en la lista desplegable del panel izquierdo
async function cambiarPerfil(tipo) {
    if (!tipo) return;  // Si no se selecciona ningún perfil, finalizamos la ejecución de la función
    try {
        // Hacemos una consulta POST a la ruta /perfil para cambiar el perfil, el cual se pasa como parámetro de consulta
        const r = await fetch(`${API}/perfil?tipo=${tipo}`, { method: 'POST' });
        // Convertimos la respuesta a formato JSON
        const d = await r.json();
        console.log('Perfil:', d);
    } catch(e) { console.error(e); }
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

    // Disparamos la orden de escaneo al ESP32 Master vía MQTT a través del endpoint /escanear
    try {
        await fetch(`${API}/escanear`, { method: 'POST' });
    } catch(e) {
        console.error("Error al disparar escaneo:", e);
    }

    // Abrir modal de espera
    // Agregamos una nueva clase al contenedor de la pantalla secundaria, para modificar la propiedad display en el CSS
    modal.classList.add('open');

    // Polling del estado actual cada 800ms para comprobar si el QR fue recibido y decodificado por la ESP32-CAM
    intervalPollQR = setInterval(async () => {
        try {
            // Hacemos una consulta GET a la ruta /estado_actual para obtener el estado actual del sistema
            const r = await fetch(`${API}/estado_actual?_=${Date.now()}`);
            // Convertimos la respuesta a formato JSON
            const d = await r.json();
            if (d.qr_actual && d.qr_actual !== 'NINGUNA' && d.qr_actual !== qrInicial) {
                // Si qr_actual es no nulo, no vale "NINGUNA" y es distinto del QR leído inicialmente de la página,
                // lo actualizamos
                const nuevoQR = d.qr_actual;
                // Cerramos la pantalla del lector de cámara
                cerrarModalCamara();
                // Actualizamos el QR en el panel izquierdo
                document.getElementById('input-qr').value = nuevoQR;
                // Actualizamos el QR en el campo de texto del header
                document.getElementById('val-qr').textContent = nuevoQR;
                alert(`¡QR decodificado con éxito!: ${nuevoQR}`);
            }
        } catch(e) {
            console.error("Error en polling de QR:", e);
        }
    }, 800);
}

// Función que cierra la pantalla secundaria del lector de la cámara. Se ejecuta al presionar cancelar o cuando se decodifica
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
        if (d.qr_actual && d.qr_actual !== 'NINGUNA') {
            document.getElementById('input-qr').placeholder = d.qr_actual;
        }

        // Update profile selector value to match the active profile
        // Actualizamos el perfil en la lista desplegable del panel izquierdo para que coincida con el perfil actual
        const selectPerfil = document.getElementById('select-perfil');
        if (d.perfil_actual && selectPerfil.value !== d.perfil_actual) {
            // Verificamos que el perfil obtenido del servidor coincida con alguno de los perfiles que se
            // encuentran como opciones en la lista desplegable
            if ([...selectPerfil.options].some(opt => opt.value === d.perfil_actual)) {
                selectPerfil.value = d.perfil_actual;
            }
        }

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
        // Hacemos una consulta GET a la ruta /baterias y obtenemos la respuesta en formato JSON
        const r = await fetch(`${API}/baterias`);
        const bats = await r.json();
        // Obtenemos el cuerpo de la tabla de inventario y la lista desplegable que permite filtrar por QR
        // que se encuentra por encima de la tabla de historial de ciclos
        const tbody = document.getElementById('tbody-inventario');
        const filtro = document.getElementById('filtro-qr');
        const filtroActual = filtro.value;

        tbody.innerHTML = '';  // Quitamos temporalmente el contenido de la tabla
        // Update filter options
        // Recorremos el array de baterías recibido y extraemos los QRs
        const qrs = bats.map(b => b.qr_id);
        // Actualizamos la lista desplegable agregando todas las baterías recibidas
        filtro.innerHTML = '<option value="">Todas</option>' + qrs.map(q =>
            `<option value="${q}" ${q === filtroActual ? 'selected' : ''}>${q}</option>`
        ).join('');
        // Recorremos el array de baterías recibido y actualizamos la tabla de inventario
        for (const bat of bats) {
            const tr = document.createElement('tr');  // Creamos una fila
            tr.className = 'clickable';  // Agregamos a la fila la clase "clickable" para agregar un estilo de CSS
            // Al hacer click sobre alguna de las filas, se muestra una ventana con el detalle de esa batería
            tr.onclick = () => verDetalle(bat.qr_id);
            // Agregamos los valores recibidos a la tabla
            tr.innerHTML = `
                <td class="td-qr">${bat.qr_id}</td>
                <td class="td-mono">${bat.fecha_registro ? new Date(bat.fecha_registro).toLocaleDateString('es-AR') : '—'}</td>
                <td class="td-mono">${bat.ciclos_count !== undefined ? bat.ciclos_count : '—'}</td>
                <td><button class="btn-sm" onclick="event.stopPropagation(); verDetalle('${bat.qr_id}')">Ver</button></td>
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
        const url = filtroQR ? `${API}/historial/${encodeURIComponent(filtroQR)}` : `${API}/historial`;
        // Hacemos la petición a la ruta y convertimos la respuesta a JSON
        const r = await fetch(url);
        const hist = await r.json();
        // Quitamos momentáneamente el contenido de la tabla
        const tbody = document.getElementById('tbody-historial');
        tbody.innerHTML = '';
        // Iteramos sobre el array de ciclos recibido y actualizamos la tabla
        hist.forEach(c => {
            const tr = document.createElement('tr');  // Creamos una fila de la tabla
            // Redondeamos el porcentaje de SoH a un decimal
            const soh = c.soh_porcentaje > 0 ? c.soh_porcentaje.toFixed(1) + '%' : '—';
            
            // Agregamos una etiqueta a la columna TIPO en función del estado
            let tipoLabel = '❓ Desconocido';
            if (c.tipo_ciclo.includes('CARGANDO')) {
                tipoLabel = '⚡ Carga';
            } else if (c.tipo_ciclo.includes('DESCARGANDO')) {
                tipoLabel = '🔋 Descarga';
            }
            if (c.tipo_ciclo.includes('Interrumpido')) {
                tipoLabel += ' <span style="color:var(--accent-orange);font-size:10px;font-weight:600;">⚠️ Interr</span>';
            }
            // Agregamos los valores recibidos a la tabla
            tr.innerHTML = `
                <td class="td-qr">${c.qr_id}</td>
                <td class="td-mono">${tipoLabel}</td>
                <td class="td-mono">${c.capacidad_ah.toFixed(3)}</td>
                <td class="td-soh">${soh}</td>
                <td class="td-mono">${Math.round(c.resistencia_interna)}</td>
                <td class="td-mono" style="color:var(--accent-red);">${c.temp_maxima.toFixed(1)}°</td>
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
        // Hacemos la consulta a la ruta /bateria/{qr} y convertimos la respuesta a JSON
        const r = await fetch(`${API}/bateria/${encodeURIComponent(qr)}`);
        const data = await r.json();

        if (data.status === 'Error') {
            // Si hubo un error, cambiamos el indicador de "Cargando..." a "No encontrada" y finalizamos la
            // ejecución de la función
            document.getElementById('modal-stats').innerHTML = '<div class="modal-stat"><div class="metric-value small">No encontrada</div></div>';
            return;
        }
        // Obtenemos los valores recibidos
        const ciclos = data.ciclos || [];  // Array con el detalle de los ciclos
        const descargas = ciclos.filter(c => c.tipo_ciclo === 'DESCARGANDO');  // Array que guarda los ciclos de descarga
        // Último SoH y resistencia interna (en caso que haya habido una descarga)
        const ultimoSoH = descargas.length > 0 ? descargas[0].soh_porcentaje : 0;
        const ultimaRI = descargas.length > 0 ? descargas[0].resistencia_interna : 0;

        // Stats
        // Actualizamos las tarjetas ubicadas en la parte superior de la ventana que muestra el detalle de la batería
        document.getElementById('modal-stats').innerHTML = `
            <div class="modal-stat">
                <div class="metric-label">Total ciclos</div>
                <div class="metric-value" style="color:var(--accent-cyan);">${data.total_ciclos}</div>
            </div>
            <div class="modal-stat">
                <div class="metric-label">Último SoH</div>
                <div class="metric-value" style="color:var(--accent-green);">${ultimoSoH > 0 ? ultimoSoH.toFixed(1) + '%' : '—'}</div>
            </div>
            <div class="modal-stat">
                <div class="metric-label">Última RI</div>
                <div class="metric-value" style="color:var(--accent-orange);">${ultimaRI > 0 ? Math.round(ultimaRI) + ' mΩ' : '—'}</div>
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
                    data: descOrden.map(c => c.soh_porcentaje),  // Datos del gráfico (se obtienen del array de descargas invertido)
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
            const soh = c.soh_porcentaje > 0 ? c.soh_porcentaje.toFixed(1) + '%' : '—';
            
            // Bandera que indica si hubo una carga
            const esCarga = c.tipo_ciclo.includes('CARGANDO');  
            // Bandera que indica si hubo una interrupción en la carga o descarga
            const esInterrumpido = c.tipo_ciclo.includes('Interrumpido');
            // Ícono para representar el estado de carga
            const icon = esCarga ? '⚡' : '🔋';
            // Ícono para indicar si la carga fue interrumpida
            const statusIndicator = esInterrumpido ? ' ⚠️' : '';

            // Agregamos los valores recibidos a la tabla
            tr.innerHTML = `
                <td class="td-mono">${icon}${statusIndicator}</td>
                <td class="td-mono">${fecha}</td>
                <td class="td-mono">${c.capacidad_ah.toFixed(3)}</td>
                <td class="td-soh">${soh}</td>
                <td class="td-mono">${Math.round(c.resistencia_interna)}</td>
                <td class="td-mono" style="color:var(--accent-cyan);">${c.voltaje_final ? c.voltaje_final.toFixed(2) : '—'}</td>
                <td class="td-mono" style="color:var(--accent-red);">${c.temp_maxima.toFixed(1)}°</td>
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

// Close modal on overlay click
// Cerramos la ventana de detalle de batería al hacer click en cualquier punto fuera de la misma
document.getElementById('modal-detalle').addEventListener('click', function(e) {
    if (e.target === this) cerrarModal();
});
// Cerramos la ventana de la cámara al hacer click en cualquier punto fuera de la misma
document.getElementById('modal-camara').addEventListener('click', function(e) {
    if (e.target === this) cerrarModalCamara();
});

// Close modal on Escape
// Cerramos las dos ventanas al presionar la tecla escape del teclado
document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') {
        cerrarModal();
        cerrarModalCamara();
    }
});

// --- INIT ---
// Cada 2 segundos actualizamos el estado del sistema
setInterval(actualizarEstado, 2000);
// Actualizamos las tablas de inventario e historial de ciclos
cargarInventario();
cargarHistorial();