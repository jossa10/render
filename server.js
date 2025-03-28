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
});

// Manejar mensajes MQTT
client.on('message', (topic, message) => {
  const messageStr = message.toString();
  console.log(`Mensaje recibido en ${topic}: ${messageStr}`);
  
  switch (topic) {
    case TOPIC_ESTADO:
      doorStatus = messageStr;
      break;
    case TOPIC_MOVIMIENTO:
      motionStatus = messageStr;
      break;
    case TOPIC_ALARMA:
      if (messageStr.includes('ALERTA')) {
        alarmActive = true;
        lastAlarmTime = new Date();
      } else if (messageStr.includes('desactivada')) {
        alarmActive = false;
      }
      break;
    default:
      break;
  }
});

// Manejar errores de MQTT
client.on('error', (err) => {
  console.error('Error en la conexión MQTT:', err);
});

// Rutas API

// Ruta para obtener el estado actual
app.get('/status', (req, res) => {
  res.json({
    door_status: doorStatus,
    motion_status: motionStatus,
    alarm_active: alarmActive,
    last_alarm_time: lastAlarmTime
  });
});

// Ruta para abrir la puerta
app.post('/open-door', (req, res) => {
  try {
    client.publish(TOPIC_COMANDO, 'ABRIR');
    console.log('Comando enviado: ABRIR');
    res.status(200).json({ success: true, message: 'Comando enviado para abrir la puerta' });
  } catch (error) {
    console.error('Error al enviar comando de apertura:', error);
    res.status(500).json({ success: false, message: 'Error al enviar comando' });
  }
});

// Ruta para establecer código temporal
app.post('/set-temp-code', (req, res) => {
  try {
    const { code } = req.body;
    
    if (!code || code.length !== 4 || isNaN(parseInt(code))) {
      return res.status(400).json({ 
        success: false, 
        message: 'El código debe ser un número de 4 dígitos' 
      });
    }
    
    client.publish(TOPIC_CODIGO_TEMPORAL, code);
    console.log('Código temporal establecido:', code);
    
    res.status(200).json({ 
      success: true, 
      message: 'Código temporal establecido correctamente' 
    });
  } catch (error) {
    console.error('Error al establecer código temporal:', error);
    res.status(500).json({ 
      success: false, 
      message: 'Error al establecer código temporal' 
    });
  }
});

// Ruta de verificación (health check)
app.get('/', (req, res) => {
  res.json({ 
    status: 'online', 
    mqtt_connected: client.connected,
    uptime: process.uptime()
  });
});

// Iniciar el servidor
app.listen(PORT, () => {
  console.log(`Servidor puente escuchando en puerto ${PORT}`);
});