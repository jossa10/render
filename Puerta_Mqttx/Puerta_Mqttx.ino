#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Definición de pines
#define BUZZER_RELAY_PIN 4
#define LOCK_RELAY_PIN 2
#define PIR_PIN 22
#define MAGNETIC_SENSOR_PIN 5  // Asumiendo pin 5 para el sensor magnético
#define RFID_SS_PIN 21         // Asumiendo pin 21 para RFID SS
#define RFID_RST_PIN 15        // Asumiendo pin 15 para RFID RST

// Definición para el teclado 4x4
#define ROWS 4
#define COLS 4
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 12, 13};

// Configuración WiFi
const char* ssid = "Gil";
const char* password = "123456789";

// Configuración MQTT
const char* mqtt_server = "ea4d47f0.ala.us-east-1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "puerta";
const char* mqtt_password = "puerta";
const char* clientID = "mqttx_e00a6245";

// Temas MQTT
const char* topic_estado = "puerta/estado";
const char* topic_movimiento = "puerta/movimiento";
const char* topic_alarma = "puerta/alarma";
const char* topic_codigo_temporal = "puerta/codigo_temporal";
const char* topic_comando = "puerta/comando";

// Variables adicionales para control de tiempos con millis()
unsigned long tiempo_mensaje_lcd = 0;
unsigned long tiempo_mensaje_clave = 0;
bool mostrar_mensaje_clave = false;
bool mostrar_mensaje_lcd = false;
const unsigned long TIEMPO_MENSAJE = 2000; // 2 segundos para mensajes

// Variables para millis de apertura de puerta
unsigned long tiempoInicioCerradura = 0;
unsigned long tiempoInicioBuzzer = 0;
bool cerraduraActiva = false;
bool buzzerAdvertenciaActivo = false;
const unsigned long TIEMPO_CERRADURA = 7000; // 7 segundos
const unsigned long TIEMPO_BUZZER = 2000;    // 2 segundos

// Inicialización de objetos
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Dirección I2C, 16 columnas, 2 filas
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Variables para el sistema
String password_correta = "1234";
String input_password = "";
bool puerta_abierta = false;
bool alarma_activada = false;
String codigo_temporal = "";
bool codigo_temporal_usado = false;

// Variables para controlar estado previo
bool estado_previo_puerta = false;
bool primer_ciclo = true;

// UIDs de tarjetas RFID autorizadas (ejemplo)
byte authorized_card[4] = {0x12, 0x34, 0x56, 0x78}; // Reemplazar con tu UID

// Configuración NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000; // GMT-5 (ajustar según zona horaria)
const int daylightOffset_sec = 3600;

// Función para conectar a WiFi
void setup_wifi() {
  WiFi.begin(ssid, password);

  unsigned long inicio_conexion = millis();
  unsigned long tiempo_espera_wifi = 30000; // 30 segundos de timeout
  
  while (WiFi.status() != WL_CONNECTED) {
    // Verificar timeout
    if (millis() - inicio_conexion > tiempo_espera_wifi) {
      Serial.println("Fallo en la conexión WiFi. Reiniciando ESP32...");
      ESP.restart();
    }
    
    // Parpadear LCD para indicar intento de conexión
    static unsigned long ultimo_parpadeo = 0;
    if (millis() - ultimo_parpadeo > 500) {
      static bool estado_parpadeo = false;
      estado_parpadeo = !estado_parpadeo;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Conectando WiFi");
      
      if (estado_parpadeo) {
        lcd.setCursor(0, 1);
        lcd.print(ssid);
      }
      
      ultimo_parpadeo = millis();
    }
    
    yield(); // Permitir que la CPU maneje otras tareas
  }

  Serial.println("WiFi conectado");
  Serial.println("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

// Callback para procesar mensajes MQTT recibidos
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("Mensaje recibido [" + String(topic) + "]: " + message);
  
  // Procesar código temporal
  if (String(topic) == topic_codigo_temporal) {
    codigo_temporal = message;
    codigo_temporal_usado = false;
    Serial.println("Nuevo código temporal recibido: " + codigo_temporal);
  }
  
  // Procesar comandos desde la aplicación
  if (String(topic) == topic_comando) {
    if (message == "ABRIR") {
      Serial.println("Comando recibido: Abrir puerta");
      abrirPuerta();
    }
  }
}

// Función para reconectar a MQTT
void reconnect() {
  unsigned long inicio_reconexion = millis();
  unsigned long timeout_reconexion = 10000; // 10 segundos máximo por intento
  
  while (!client.connected()) {
    // Verificar si ha pasado el tiempo máximo de intento
    if (millis() - inicio_reconexion > timeout_reconexion) {
      Serial.println("Timeout en reconexión MQTT. Intentando de nuevo...");
      return;  // Salir y dejar que el próximo ciclo lo intente de nuevo
    }
    
    Serial.print("Intentando conexión MQTT...");
    
    // Intentar conexión
    if (client.connect(clientID, mqtt_user, mqtt_password)) {
      Serial.println("conectado");
      
      // Suscripción a temas
      client.subscribe(topic_codigo_temporal);
      client.subscribe(topic_comando);  // Añadir suscripción al tema de comandos
      
      // Publicar estado inicial
      client.publish(topic_estado, "Sistema iniciado");
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" intentando de nuevo");
      
      // Esperar un poco antes de reintentar, usando un contador no bloqueante
      unsigned long tiempo_espera = millis();
      while (millis() - tiempo_espera < 1000) {
        yield(); // Permitir otras operaciones durante la espera
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Inicialización de pines
  pinMode(BUZZER_RELAY_PIN, OUTPUT);
  pinMode(LOCK_RELAY_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(MAGNETIC_SENSOR_PIN, INPUT_PULLUP);
  
  // Estado inicial de relés (OFF)
  digitalWrite(BUZZER_RELAY_PIN, LOW);
  digitalWrite(LOCK_RELAY_PIN, LOW);
  
  // Inicializar LCD
  Wire.begin(16, 17); // SDA, SCL
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  
  // Inicializar RFID
  SPI.begin();
  rfid.PCD_Init();
  
  // Configurar WiFi
  setup_wifi();
  
  // Configurar tiempo NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Configurar MQTT con TLS
  espClient.setInsecure(); // Para pruebas (en producción usar certificados)
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // Mostrar mensaje inicial
  tiempo_mensaje_lcd = millis();
  mostrar_mensaje_lcd = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Listo");
}

// Función para actualizar LCD con estado de la puerta
void actualizarLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  // Verificar estado de la puerta
  bool estado_puerta_actual = (digitalRead(MAGNETIC_SENSOR_PIN) == HIGH);
  if (estado_puerta_actual) {
    lcd.print("Puerta: ABIERTA");
    puerta_abierta = true;
    // Enviar estado por MQTT
    if (client.connected()) {
      client.publish(topic_estado, "ABIERTA");
    }
  } else {
    lcd.print("Puerta: CERRADA");
    puerta_abierta = false;
    // Enviar estado por MQTT
    if (client.connected()) {
      client.publish(topic_estado, "CERRADA");
    }
  }
  
  // Verificar movimiento
  lcd.setCursor(0, 1);
  bool hay_movimiento = (digitalRead(PIR_PIN) == HIGH);
  if (hay_movimiento) {
    lcd.print("Movimiento: SI");
    if (client.connected()) {
      client.publish(topic_movimiento, "SI");
    }
  } else {
    lcd.print("Movimiento: NO");
    if (client.connected()) {
      client.publish(topic_movimiento, "NO");
    }
  }
}

// Función para abrir la puerta
void abrirPuerta() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Acceso concedido");
  lcd.setCursor(0, 1);
  lcd.print("Abriendo puerta");
  
  digitalWrite(LOCK_RELAY_PIN, HIGH);  // Activar relay de la chapa
  cerraduraActiva = true;
  tiempoInicioCerradura = millis();
  
  if (client.connected()) {
    client.publish(topic_estado, "Acceso concedido");
  }
}

// Función para verificar si una tarjeta RFID está autorizada
bool verificarTarjetaRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return false;
  }
  
  Serial.print("UID de tarjeta: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
  
  // Verificar si coincide con la tarjeta autorizada
  bool authorized = true;
  for (byte i = 0; i < 4; i++) {
    if (rfid.uid.uidByte[i] != authorized_card[i]) {
      authorized = false;
      break;
    }
  }
  
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  return authorized;
}

// Función para manejar entrada de contraseña
void manejarTeclado() {
  char key = keypad.getKey();
  
  if (key) {
    if (key == '#') {
      // Si no hay contraseña en progreso, iniciar entrada
      if (input_password.length() == 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Ingrese clave:");
        lcd.setCursor(0, 1);
      } 
      // Si hay contraseña en progreso, verificarla
      else {
        // Verificar si es la contraseña correcta o el código temporal
        if (input_password == password_correta || 
            (!codigo_temporal_usado && input_password == codigo_temporal)) {
          
          // Si usamos código temporal, marcarlo como usado
          if (input_password == codigo_temporal) {
            codigo_temporal_usado = true;
            if (client.connected()) {
              client.publish(topic_codigo_temporal, "usado");
            }
          }
          
          // Abrir la puerta
          abrirPuerta();
        } else {
          // Mostrar mensaje de clave incorrecta
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Clave incorrecta");
          
          // Usar millis para controlar tiempo de mensaje
          mostrar_mensaje_clave = true;
          tiempo_mensaje_clave = millis();
          
          if (client.connected()) {
            client.publish(topic_estado, "Intento fallido: clave incorrecta");
          }
        }
        // Reiniciar entrada
        input_password = "";
      }
    } 
    // Añadir dígito a la contraseña en progreso
    else if (key >= '0' && key <= '9' && input_password.length() < 10) {
      input_password += key;
      lcd.setCursor(input_password.length() - 1, 1);
      lcd.print("*");
    }
  }
}

// Función para manejar alarma de forzado
void manejarAlarmaForzado() {
  bool estado_actual_puerta = (digitalRead(MAGNETIC_SENSOR_PIN) == HIGH);
  
  // En el primer ciclo, inicializar el estado previo
  if (primer_ciclo) {
    estado_previo_puerta = estado_actual_puerta;
    primer_ciclo = false;
    return;
  }
  
  // Si la puerta está abierta pero no se activó el relay de la chapa
  // y no estaba abierta antes (es decir, detectamos un cambio de cerrada a abierta sin activar la cerradura)
  if (estado_actual_puerta && !cerraduraActiva && !estado_previo_puerta) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ALARMA ACTIVADA");
    lcd.setCursor(0, 1);
    lcd.print("PUERTA FORZADA");
    
    alarma_activada = true;
    digitalWrite(BUZZER_RELAY_PIN, HIGH);
    
    if (client.connected()) {
      client.publish(topic_alarma, "ALERTA: Puerta forzada");
    }
  }
  
  // Si la alarma está activada y la puerta se cerró
  if (alarma_activada && !estado_actual_puerta) {
    // Verificar si hay movimiento afuera con el PIR
    if (digitalRead(PIR_PIN) == LOW) {
      // No hay movimiento afuera, mantener alarma sonando
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ALARMA ACTIVA");
      lcd.setCursor(0, 1);
      lcd.print("INTRUSO DENTRO");
      
      if (client.connected()) {
        client.publish(topic_alarma, "ALERTA: Intruso dentro");
      }
    } else {
      // Hay movimiento afuera, desactivar alarma
      alarma_activada = false;
      digitalWrite(BUZZER_RELAY_PIN, LOW);
      
      if (client.connected()) {
        client.publish(topic_alarma, "Alarma desactivada");
      }
      
      actualizarLCD();
    }
  }
  
  // Actualizar estado previo para el siguiente ciclo
  estado_previo_puerta = estado_actual_puerta;
}

void loop() {
  // Mantener conexión MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  unsigned long tiempoActual = millis();
  
  // Gestionar mensaje de sistema listo al inicio
  if (mostrar_mensaje_lcd && (tiempoActual - tiempo_mensaje_lcd >= TIEMPO_MENSAJE)) {
    mostrar_mensaje_lcd = false;
    actualizarLCD();
  }
  
  // Gestionar mensaje de clave incorrecta
  if (mostrar_mensaje_clave && (tiempoActual - tiempo_mensaje_clave >= TIEMPO_MENSAJE)) {
    mostrar_mensaje_clave = false;
    actualizarLCD();
  }
  
  // Verificar estado de la puerta y actualizar LCD cada 1 segundo
  static unsigned long lastUpdateTime = 0;
  if (tiempoActual - lastUpdateTime > 1000 && !mostrar_mensaje_lcd && !mostrar_mensaje_clave) {
    actualizarLCD();
    lastUpdateTime = tiempoActual;
  }
  
  // Verificar tarjeta RFID
  if (verificarTarjetaRFID()) {
    Serial.println("Tarjeta autorizada");
    abrirPuerta();
  }
  
  // Manejar entrada de teclado
  manejarTeclado();
  
  // Manejar alarma de forzado
  manejarAlarmaForzado();
  
  // Control de tiempo de cerradura
  if (cerraduraActiva && (tiempoActual - tiempoInicioCerradura >= TIEMPO_CERRADURA)) {
    digitalWrite(LOCK_RELAY_PIN, LOW);  // Desactivar relay de la chapa
    cerraduraActiva = false;
    
    // Verificar si la puerta quedó abierta después de los 7 segundos
    if (digitalRead(MAGNETIC_SENSOR_PIN) == HIGH) {
      // La puerta sigue abierta, activar alarma por 2 segundos
      digitalWrite(BUZZER_RELAY_PIN, HIGH);
      buzzerAdvertenciaActivo = true;
      tiempoInicioBuzzer = tiempoActual;
      
      if (client.connected()) {
        client.publish(topic_alarma, "Advertencia: puerta quedó abierta");
      }
    }
  }
  
  // Control de tiempo del buzzer de advertencia
  if (buzzerAdvertenciaActivo && (tiempoActual - tiempoInicioBuzzer >= TIEMPO_BUZZER)) {
    digitalWrite(BUZZER_RELAY_PIN, LOW);
    buzzerAdvertenciaActivo = false;
    actualizarLCD();
  }
  
  // Enviar estados por MQTT cada 3 segundos
  static unsigned long lastMqttUpdateTime = 0;
  if (tiempoActual - lastMqttUpdateTime > 3000) {
    if (client.connected()) {
      // Enviar estado de puerta
      client.publish(topic_estado, digitalRead(MAGNETIC_SENSOR_PIN) == HIGH ? "ABIERTA" : "CERRADA");
      
      // Enviar estado de movimiento
      client.publish(topic_movimiento, digitalRead(PIR_PIN) == HIGH ? "SI" : "NO");
    }
    lastMqttUpdateTime = tiempoActual;
  }
}