// ======================
// ===== ESP32 NORMAL ====
// ======================

#define RXD2 16   // RX del ESP32 normal conectado al TX (15) de la ESP32-CAM
#define TXD2 17   // TX del ESP32 normal conectado al RX (14) de la ESP32-CAM

#define LASER_PIN 4     // Láser KY-008
#define LED_PIN 2       // LED depuración
#define LED_SENSOR_PIN 23 // LED que indica impacto

// LDRs
#define LDR_PIN1 32
#define LDR_PIN2 33
#define LDR_PIN3 34
#define LDR_PIN4 35
#define LDR_PIN5 22
#define LDR_PIN6 25

// Pines L298N
#define M1_IN1 12
#define M1_IN2 13
#define M1_EN  21

#define M2_IN1 14
#define M2_IN2 27
#define M2_EN  26

#define MOTOR_PWM_FREQ 1000
#define MOTOR_PWM_RES 8
#define MOTOR_CH_A 1
#define MOTOR_CH_B 2

// Disparos
#define MAX_SHOTS 40
int shotsRemaining = MAX_SHOTS;

// NUEVO: ahora contamos impactos (tiros recibidos)
int shotsReceived = 0;

const float VREF = 3.3;
const int ADC_RES = 4095;

unsigned long lastLifeReductionTime = 0;
const unsigned long lifeReductionInterval = 500;


// =========================
// ======= MOTORES =========
// =========================
void motorSet(int ch, int in1, int in2, int speed) {
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
    ledcWrite(ch, speed);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    ledcWrite(ch, -speed);
  }

  if (ch == MOTOR_CH_A) Serial2.println("M1:OK");
  else Serial2.println("M2:OK");

  Serial2.flush();
}


// =========================
// ======== SETUP ==========
// =========================
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // PWM para láser
  ledcSetup(0, 5000, 8);
  ledcAttachPin(LASER_PIN, 0);
  ledcWrite(0, 0);

  // PWM motores
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

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(LED_SENSOR_PIN, OUTPUT);
  digitalWrite(LED_SENSOR_PIN, LOW);

  pinMode(LDR_PIN1, INPUT);
  pinMode(LDR_PIN2, INPUT);
  pinMode(LDR_PIN3, INPUT);
  pinMode(LDR_PIN4, INPUT);
  pinMode(LDR_PIN5, INPUT);
  pinMode(LDR_PIN6, INPUT);

  Serial.println("ESP32 lista (Láser + Sensores + Motores)");
}


// =========================
// ========= LOOP ==========
// =========================
void loop() {

  // ------------------------
  // RECIBIR COMANDOS CAM
  // ------------------------
  if (Serial2.available()) {

    String received = Serial2.readStringUntil('\n');
    received.trim();

    // ---- Disparo ----
    if (received == "FIRE") {

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
      }
    }

    // ---- Movimiento motores ----
    else if (received.startsWith("M1:") || received.startsWith("M2:")) {

      int v1 = 0, v2 = 0;

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

      motorSet(MOTOR_CH_A, M1_IN1, M1_IN2, v1);
      motorSet(MOTOR_CH_B, M2_IN1, M2_IN2, v2);

      if ((v1 > 0 && v2 > 0) || (v1 < 0 && v2 < 0)) {
        delay(300);
        motorSet(MOTOR_CH_A, M1_IN1, M1_IN2, 0);
        motorSet(MOTOR_CH_B, M2_IN1, M2_IN2, 0);
      }
    }
  }


  // -----------------------------------
  // LECTURA DE LOS 6 LDR (impactos)
  // -----------------------------------
  const int LDR_PINS[6] = { LDR_PIN1, LDR_PIN2, LDR_PIN3, LDR_PIN4, LDR_PIN5, LDR_PIN6 };

  unsigned long now = millis();

  for (int i = 0; i < 6; i++) {

    int raw = analogRead(LDR_PINS[i]);
    float voltage = (raw * VREF) / ADC_RES;

    Serial.print("Voltaje LDR ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(voltage, 3);

    // ------- IMPACTO -------
    if (voltage > 1) {

      if (now - lastLifeReductionTime >= lifeReductionInterval) {

        shotsReceived++;   // SUMA TIRO RECIBIDO

        lastLifeReductionTime = now;

        // flash LED
        digitalWrite(LED_SENSOR_PIN, HIGH);
        delay(100);
        digitalWrite(LED_SENSOR_PIN, LOW);

        // Enviar actualización
        Serial2.print("SHOTS_TAKEN:");
        Serial2.println(shotsReceived);
        Serial2.flush();
      }
    }
  }

  delay(100);
}



