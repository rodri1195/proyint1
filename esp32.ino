//esp 32 :
#define RXD2 16   // RX del ESP32 normal conectado al TX (15) de la ESP32-CAM
#define TXD2 17   // TX del ESP32 normal conectado al RX (14) de la ESP32-CAM
#define LASER_PIN 4     // Pin conectado al módulo láser KY-008
#define LED_PIN 2       // LED de depuración
#define LED_SENSOR_PIN 23 // LED que indica nivel de voltaje LDR y estado de vidas
#define LDR_PIN 34      // Nodo entre LDR y resistencia fija

// --- Pines y configuración para L298N (dos motores) ---
#define M1_IN1 12
#define M1_IN2 13
#define M1_EN  25

#define M2_IN1 14
#define M2_IN2 27
#define M2_EN  26

#define MOTOR_PWM_FREQ 1000
#define MOTOR_PWM_RES 8
#define MOTOR_CH_A 1
#define MOTOR_CH_B 2

#define MAX_SHOTS 40
int shotsRemaining = MAX_SHOTS;
int lives = 3; // Vidas iniciales

const float VREF = 3.3;
const int ADC_RES = 4095;

unsigned long lastLifeReductionTime = 0;
const unsigned long lifeReductionInterval = 500; // Tiempo mínimo entre reducciones (ms)

// Control simple de motor con L298N. speed en rango -255..255
void motorSet(int ch, int in1, int in2, int speed) {
  // seguridad: limitar rango
  speed = constrain(speed, -255, 255);
  if (speed == 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    ledcWrite(ch, 0);
    return;
  }
  if (speed > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    ledcWrite(ch, constrain(speed, 0, 255));
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    ledcWrite(ch, constrain(-speed, 0, 255));
  }
  // confirmar por Serial2 (opcional, útil para debug/UI)
  if (ch == MOTOR_CH_A) {
    Serial2.println("M1:OK");
  } else if (ch == MOTOR_CH_B) {
    Serial2.println("M2:OK");
  }
  Serial2.flush();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // Configurar PWM para el pin del láser
  ledcSetup(0, 5000, 8); // Canal 0, frecuencia 5kHz, resolución 8 bits
  ledcAttachPin(LASER_PIN, 0); // Asocia el pin del láser al canal 0
  ledcWrite(0, 0); // Apaga el láser inicialmente

  // --- Configurar PWM y pines para motores ---
  ledcSetup(MOTOR_CH_A, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcSetup(MOTOR_CH_B, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(M1_EN, MOTOR_CH_A);
  ledcAttachPin(M2_EN, MOTOR_CH_B);
  ledcWrite(MOTOR_CH_A, 0);
  ledcWrite(MOTOR_CH_B, 0);

  pinMode(M1_IN1, OUTPUT);
  pinMode(M1_IN2, OUTPUT);
  pinMode(M2_IN1, OUTPUT);
  pinMode(M2_IN2, OUTPUT);
  digitalWrite(M1_IN1, LOW);
  digitalWrite(M1_IN2, LOW);
  digitalWrite(M2_IN1, LOW);
  digitalWrite(M2_IN2, LOW);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(LED_SENSOR_PIN, OUTPUT);
  digitalWrite(LED_SENSOR_PIN, LOW);

  pinMode(LDR_PIN, INPUT);

  Serial.println("ESP32 Normal lista (Láser + Sensor LDR + Motores)");
}

void loop() {
  // Comunicación con ESP32-CAM
  if (Serial2.available()) {
    String received = Serial2.readStringUntil('\n');
    received.trim();
    if (received == "FIRE") {
      // Permitir disparar solo si quedan vidas
      if (lives > 0) {
        if (shotsRemaining > 0) {
          ledcWrite(0, 255);
          digitalWrite(LED_PIN, HIGH);
          delay(50);
          ledcWrite(0, 0);
          digitalWrite(LED_PIN, LOW);
          shotsRemaining--;
          Serial2.print("SHOTS:");
          Serial2.println(shotsRemaining);
          Serial2.flush();
        } else {
          Serial2.println("NO_SHOTS");
          Serial2.flush();
        }
      } else {
        // No quedan vidas -> no permitir disparar
        Serial2.print("LIVES:");
        Serial2.println(0);
        Serial2.flush();
      }
    }
    // Comandos para motores: M1:<val> y M2:<val>
    else if (received.startsWith("M1:") || received.startsWith("M2:")) {
  int v1 = 0, v2 = 0;

  // Leer ambos valores de motor (pueden venir separados)
  if (received.startsWith("M1:")) {
    v1 = received.substring(3).toInt();
    while (!Serial2.available()) delay(1);
    String next = Serial2.readStringUntil('\n');
    next.trim();
    if (next.startsWith("M2:")) v2 = next.substring(3).toInt();
  } else {
    v2 = received.substring(3).toInt();
    while (!Serial2.available()) delay(1);
    String next = Serial2.readStringUntil('\n');
    next.trim();
    if (next.startsWith("M1:")) v1 = next.substring(3).toInt();
  }

  // --- Movimiento normal ---
  motorSet(MOTOR_CH_A, M1_IN1, M1_IN2, v1);
  motorSet(MOTOR_CH_B, M2_IN1, M2_IN2, v2);

  // --- Efecto: solo adelante o atrás ---
  if ((v1 > 0 && v2 > 0) || (v1 < 0 && v2 < 0)) {
    // Se mueve adelante o atrás
    delay(300);  // ← tiempo del movimiento (ajustalo a gusto)
    motorSet(MOTOR_CH_A, M1_IN1, M1_IN2, 0);
    motorSet(MOTOR_CH_B, M2_IN1, M2_IN2, 0);
  }
}


  }

  // Medición de voltaje LDR
  int raw = analogRead(LDR_PIN);
  float voltage = (raw * VREF) / ADC_RES;

  Serial.print("Voltaje LDR: ");
  Serial.println(voltage, 3);

  unsigned long now = millis();
  if (voltage > 0.1 && lives > 0) {
    if (now - lastLifeReductionTime >= lifeReductionInterval) {
      // reducir vida
      lives--;
      lastLifeReductionTime = now;
      // indicar impacto con LED_SENSOR
      digitalWrite(LED_SENSOR_PIN, HIGH);
      delay(100);
      digitalWrite(LED_SENSOR_PIN, LOW);
      // enviar estado al ESP32-CAM
      Serial2.print("LIVES:");
      Serial2.println(lives);
      Serial2.flush();

      // si vidas llegan a 0, prender láser permanentemente
      if (lives == 0) {
        ledcWrite(0, 255); // láser encendido permanentemente
        Serial.println("Vidas 0: Láser encendido");
      }
    }
  } else {
    // no hay impacto: mantener LED apagado si aún hay vidas
    if (lives > 0) digitalWrite(LED_SENSOR_PIN, LOW);
  }

  delay(100);
}


