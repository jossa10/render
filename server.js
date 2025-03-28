// server.js - Servidor puente para el sistema de puerta inteligente
const express = require('express');
const mqtt = require('mqtt');
const cors = require('cors');
const bodyParser = require('body-parser');
const dotenv = require('dotenv');

// Configuración de variables de entorno
dotenv.config();

// Crear la aplicación Express
const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(bodyParser.json());

// Variables para almacenar el estado actual
let doorStatus = 'CERRADA';
let motionStatus = 'NO';
let alarmActive = false;
let lastAlarmTime = null;
let lastAccessTime = null;
let accessHistory = [];

// Configuración de conexión MQTT desde variables de entorno
const MQTT_HOST = process.env.MQTT_HOST;
const MQTT_PORT = parseInt(process.env.MQTT_PORT);
const MQTT_USERNAME = process.env.MQTT_USERNAME;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD;
const MQTT_CLIENT_ID = 'render_server_' + Math.random().toString(16).substring(2, 8);

const mqttOptions = {
  host: MQTT_HOST,
  port: MQTT_PORT,
  protocol: 'mqtts',
  username: MQTT_USERNAME,
  password: MQTT_PASSWORD,
  clientId: MQTT_CLIENT_ID,
  rejectUnauthorized: false // Solo para pruebas, establecer a true en producción
};

// Temas MQTT
const TOPIC_ESTADO = 'puerta/estado';
const TOPIC_MOVIMIENTO = 'puerta/movimiento';
const TOPIC_ALARMA = 'puerta/alarma';
const TOPIC_CODIGO_TEMPORAL = 'puerta/codigo_temporal';
const TOPIC_COMANDO = 'puerta/comando';

// Conectar al broker MQTT
const client = mqtt.connect(mqttOptions);

// Registrar historia de accesos
function logAccess(type, details) {
  const timestamp = new Date();
  const accessLog = { type, details, timestamp };
  accessHistory.unshift(accessLog); // Añadir al inicio del array
  
  // Mantener solo los últimos 50 registros
  if(accessHistory.length > 50) {
    accessHistory.pop();
  }
  
  lastAccessTime = timestamp;
  return accessLog;
}

// Manejar conexión MQTT
client.on('connect', () => {
  console.log('Conectado al broker MQTT');
  
  // Suscribirse a temas
  client.subscribe(TOPIC_ESTADO, (err) => {
    if (!err) console.log('Suscrito a', TOPIC_ESTADO);
    else console.error('Error al suscribirse a', TOPIC_ESTADO, err);
  });
  
  client.subscribe(TOPIC_MOVIMIENTO, (err) => {
    if (!err) console.log('Suscrito a', TOPIC_MOVIMIENTO);
    else console.error('Error al suscribirse a', TOPIC_MOVIMIENTO, err);
  });
  
  client.subscribe(TOPIC_ALARMA, (err) => {
    if (!err) console.log('Suscrito a', TOPIC_ALARMA);
    else console.error('Error al suscribirse a', TOPIC_ALARMA, err);
  });
  
  client.subscribe(TOPIC_CODIGO_TEMPORAL, (err) => {
    if (!err) console.log('Suscrito a', TOPIC_CODIGO_TEMPORAL);
    else console.error('Error al suscribirse a', TOPIC_CODIGO_TEMPORAL, err);
  });
  
  // Publicar mensaje de inicio
  client.publish(TOPIC_ESTADO, 'Servidor API conectado');
  
  // Registrar en historial
  logAccess('system', 'Servidor API conectado al broker MQTT');
});

// Manejar mensajes MQTT
client.on('message', (topic, message) => {
  const messageStr = message.toString();
  console.log(Mensaje recibido en ${topic}: ${messageStr});
  
  switch (topic) {
    case TOPIC_ESTADO:
      if (messageStr === 'ABIERTA' || messageStr === 'CERRADA') {
        if (doorStatus !== messageStr) {
          doorStatus = messageStr;
          logAccess('door_status', Puerta: ${messageStr});
        }
      } else if (messageStr === 'Acceso concedido') {
        logAccess('access', 'Acceso concedido en el dispositivo');
      } else if (messageStr.includes('Intento fallido')) {
        logAccess('access_failed', messageStr);
      }
      break;
      
    case TOPIC_MOVIMIENTO:
      if (messageStr === 'SI' || messageStr === 'NO') {
        motionStatus = messageStr;
      }
      break;
      
    case TOPIC_ALARMA:
      if (messageStr.includes('ALERTA')) {
        alarmActive = true;
        lastAlarmTime = new Date();
        logAccess('alarm', messageStr);
      } else if (messageStr.includes('desactivada')) {
        alarmActive = false;
        logAccess('alarm_off', 'Alarma desactivada');
      } else if (messageStr.includes('Advertencia')) {
        logAccess('warning', messageStr);
      }
      break;
      
    case TOPIC_CODIGO_TEMPORAL:
      if (messageStr === 'usado') {
        logAccess('temp_code', 'Código temporal utilizado');
      }
      break;
      
    default:
      break;
  }
});

// Manejar errores de MQTT
client.on('error', (err) => {
  console.error('Error en la conexión MQTT:', err);
  logAccess('error', Error en la conexión MQTT: ${err.message});
});

// Rutas API

// Ruta para obtener el estado actual
app.get('/status', (req, res) => {
  res.json({
    door_status: doorStatus,
    motion_status: motionStatus,
    alarm_active: alarmActive,
    last_alarm_time: lastAlarmTime,
    last_access_time: lastAccessTime,
    server_time: new Date(),
    mqtt_connected: client.connected
  });
});

// Ruta para abrir la puerta
app.post('/open-door', (req, res) => {
  try {
    if (!client.connected) {
      return res.status(503).json({ 
        success: false, 
        message: 'No hay conexión con el sistema de puerta' 
      });
    }
    
    client.publish(TOPIC_COMANDO, 'ABRIR');
    console.log('Comando enviado: ABRIR');
    
    // Registrar en historial
    const log = logAccess('remote_open', 'Puerta abierta remotamente desde la API');
    
    res.status(200).json({ 
      success: true, 
      message: 'Comando enviado para abrir la puerta',
      timestamp: log.timestamp
    });
  } catch (error) {
    console.error('Error al enviar comando de apertura:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Error al enviar comando' 
    });
  }
});

// Ruta para establecer código temporal
app.post('/set-temp-code', (req, res) => {
  try {
    if (!client.connected) {
      return res.status(503).json({ 
        success: false, 
        message: 'No hay conexión con el sistema de puerta' 
      });
    }
    
    const { code } = req.body;
    
    if (!code || code.length !== 4 || isNaN(parseInt(code))) {
      return res.status(400).json({ 
        success: false, 
        message: 'El código debe ser un número de 4 dígitos' 
      });
    }
    
    client.publish(TOPIC_CODIGO_TEMPORAL, code);
    console.log('Código temporal establecido:', code);
    
    // Registrar en historial
    const log = logAccess('temp_code_created', Código temporal generado: ${code});
    
    res.status(200).json({ 
      success: true, 
      message: 'Código temporal establecido correctamente',
      code: code,
      timestamp: log.timestamp
    });
  } catch (error) {
    console.error('Error al establecer código temporal:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Error al establecer código temporal' 
    });
  }
});

// Ruta para obtener historial de accesos
app.get('/access-history', (req, res) => {
  // Opcional: permitir limitar el número de registros
  const limit = req.query.limit ? parseInt(req.query.limit) : 50;
  const history = accessHistory.slice(0, limit);
  
  res.json({
    success: true,
    count: history.length,
    history: history
  });
});

// Ruta de verificación (health check)
app.get('/', (req, res) => {
  res.json({ 
    status: 'online', 
    mqtt_connected: client.connected,
    uptime: process.uptime(),
    door_status: doorStatus,
    server_time: new Date()
  });
});

// Iniciar el servidor
app.listen(PORT, () => {
  console.log(Servidor puente escuchando en puerto ${PORT});
});