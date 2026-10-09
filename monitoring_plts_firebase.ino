#include <Servo.h>

// =========================================================================
// SMART CITY & PLTS IOT SENSOR NODE + SMART WASTE SYSTEM
// Komunikasi Serial USB ke Web Monitoring & Firebase
// =========================================================================

// =========================
// PIN SENSOR ANALOG
// =========================
#define WATER_PIN A0  // Sensor Ketinggian Air (Water Level)
#define MQ_PIN    A5  // Sensor Kualitas Udara (MQ Gas Sensor)
#define TDS_PIN   A2  // Sensor Kualitas Air (TDS Sensor)
#define NTC_PIN   A3  // Sensor Suhu (NTC Thermistor)

// =========================
// PIN DIGITAL & AKTUATOR
// =========================
#define BUZZER_PIN          9   // (DIUBAH) Pin Buzzer Pasif (Pin D9)
#define LED_PIN             5   // Lampu LED Miniatur
#define PUMP_PIN            6   // Relay Pompa Air
#define FIRE_PIN            7   // Sensor Api (Flame Sensor, LOW = Api Terdeteksi)
#define TILT_PIN            8   // Sensor Kemiringan / Getaran (Tilt, HIGH = Terguncang)
#define SERVO_ORGANIK_PIN   11  // Servo Tempat Sampah Organik (Pin D10)
#define SERVO_ANORGANIK_PIN 10  // Servo Tempat Sampah Anorganik (Pin D11)

// =========================
// OBJEK SERVO & VARIABEL SUDUT
// =========================
Servo servoOrganik;
Servo servoAnorganik;

int servoOrganikAngle = 0;   // 0 = Tertutup, 90 = Terbuka
int servoAnorganikAngle = 0; // 0 = Tertutup, 90 = Terbuka

// Timer Auto-close Tutup Tempat Sampah
unsigned long timeOrganikOpened = 0;
bool isOrganikAutoClosing = false;

unsigned long timeAnorganikOpened = 0;
bool isAnorganikAutoClosing = false;

// Override manual dari Web
bool pumpManualOverride = false;
bool pumpManualState = false;

bool buzzerManualOverride = true;
bool buzzerManualState = false;

// Timer untuk Buzzer Pasif (Pengganti delay)
unsigned long lastBuzzerToneMillis = 0;
const unsigned long BUZZER_INTERVAL = 500; // 250ms tone + 250ms jeda (sesuai kode PictoBlox)

// Dummy Generator untuk Tegangan & Arus
float dummyVolt = 4.50;
float dummyAmp = 2.40;

// Interval Pengiriman Data JSON (300 ms)
unsigned long previousMillis = 0;
const unsigned long SEND_INTERVAL = 300;

// Buffer perintah Serial
String inputBuffer = "";

// Variabel Filter Exponential Moving Average (EMA)
float filteredWater = -1;
float filteredMq    = -1;
float filteredTds   = -1;
float filteredNtc   = -1;

bool fireDetected = false;
bool tiltDetected = false;

// Fungsi pembacaan analog multi-sampling
int readAnalogFiltered(uint8_t pin) {
  analogRead(pin); 
  delayMicroseconds(40);
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(pin);
    delayMicroseconds(20);
  }
  return (int)(sum / 8);
}

// =========================================================================
// LOGIKA BUZZER PASIF (Pengganti tone + delay dari PictoBlox)
// =========================================================================
void updateBuzzer() {
  bool shouldSound = false;
  if (buzzerManualOverride) {
    shouldSound = buzzerManualState;
  } else {
    shouldSound = (fireDetected || tiltDetected);
  }

  if (shouldSound) {
    unsigned long now = millis();
    // Jika sudah lewat 500ms (250ms bunyi + 250ms delay)
    if (now - lastBuzzerToneMillis >= BUZZER_INTERVAL) {
      lastBuzzerToneMillis = now;
      tone(BUZZER_PIN, 65, 250); // Memainkan nada 65Hz selama 250ms di Pin 3
    }
  } else {
    noTone(BUZZER_PIN); // Matikan buzzer jika tidak ada trigger
  }
}

void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(A4));

  pinMode(WATER_PIN, INPUT);
  pinMode(MQ_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(NTC_PIN, INPUT);

  pinMode(FIRE_PIN, INPUT_PULLUP);
  pinMode(TILT_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  
  // Inisialisasi Pin 3 sebagai OUTPUT untuk Buzzer Pasif
  pinMode(BUZZER_PIN, OUTPUT);

  servoOrganik.attach(SERVO_ORGANIK_PIN);
  servoAnorganik.attach(SERVO_ANORGANIK_PIN);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);
  noTone(BUZZER_PIN);

  servoOrganik.write(0);
  servoAnorganik.write(0);

  Serial.println("SYSTEM_READY");
}

void loop() {
  handleIncomingSerial();
  checkServoAutoClose();
  
  // Panggil fungsi buzzer pasif (non-blocking)
  updateBuzzer();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= SEND_INTERVAL) {
    previousMillis = currentMillis;

    int rawWater = readAnalogFiltered(WATER_PIN);
    int rawMq    = readAnalogFiltered(MQ_PIN);
    int rawTds   = readAnalogFiltered(TDS_PIN);
    int rawNtc   = readAnalogFiltered(NTC_PIN);

    if (filteredNtc < 0) {
      filteredWater = rawWater;
      filteredMq    = rawMq;
      filteredTds   = rawTds;
      filteredNtc   = rawNtc;
    } else {
      filteredWater = (filteredWater * 0.85) + (rawWater * 0.15);
      filteredMq    = (filteredMq * 0.85) + (rawMq * 0.15);
      filteredTds   = (filteredTds * 0.85) + (rawTds * 0.15);
      filteredNtc   = (filteredNtc * 0.85) + (rawNtc * 0.15);
    }

    int waterValue = round(filteredWater);
    int mqValue    = round(filteredMq);
    int tdsValue   = round(filteredTds);
    int ntcValue   = round(filteredNtc);

    int fireValue = digitalRead(FIRE_PIN);
    int tiltValue = digitalRead(TILT_PIN);

    fireDetected = (fireValue == LOW);
    tiltDetected = (tiltValue == LOW);

    if (pumpManualOverride) {
      digitalWrite(PUMP_PIN, pumpManualState ? HIGH : LOW);
    } else {
      digitalWrite(PUMP_PIN, (waterValue > 500) ? HIGH : LOW);
    }

    float deltaV = (random(-6, 7)) / 100.0;
    dummyVolt = constrain(dummyVolt + deltaV, 4.00, 5.00);

    float deltaA = (random(-5, 6)) / 100.0;
    dummyAmp = constrain(dummyAmp + deltaA, 2.00, 3.00);

    float dummyPower = dummyVolt * dummyAmp;

    sendJsonTelemetry(waterValue, mqValue, tdsValue, ntcValue, dummyVolt, dummyAmp, dummyPower, fireDetected, tiltDetected);
  }
}

void sendJsonTelemetry(int water, int mq, int tds, int ntc, float volt, float amp, float power, bool fire, bool tilt) {
  Serial.print("{");
  Serial.print("\"water\":"); Serial.print(water);
  Serial.print(",\"mq\":"); Serial.print(mq);
  Serial.print(",\"tds\":"); Serial.print(tds);
  Serial.print(",\"ntc\":"); Serial.print(ntc);
  Serial.print(",\"volt\":"); Serial.print(volt, 2);
  Serial.print(",\"ampere\":"); Serial.print(amp, 2);
  Serial.print(",\"power\":"); Serial.print(power, 2);
  Serial.print(",\"fire\":"); Serial.print(fire ? 1 : 0);
  Serial.print(",\"tilt\":"); Serial.print(tilt ? 1 : 0);
  Serial.print(",\"pump\":"); Serial.print(digitalRead(PUMP_PIN));
  
  bool isBuzzerActive = buzzerManualOverride ? buzzerManualState : (fire || tilt);
  Serial.print(",\"buzzer\":"); Serial.print(isBuzzerActive ? 1 : 0);
  
  Serial.print(",\"led\":"); Serial.print(digitalRead(LED_PIN));
  Serial.print(",\"servo_organik\":"); Serial.print(servoOrganikAngle);
  Serial.print(",\"servo_anorganik\":"); Serial.print(servoAnorganikAngle);
  Serial.println("}");
}

void checkServoAutoClose() {
  unsigned long now = millis();
  if (isOrganikAutoClosing && (now - timeOrganikOpened >= 4000)) {
    isOrganikAutoClosing = false;
    servoOrganikAngle = 0;
    servoOrganik.write(0);
    Serial.println("{\"ack\":\"SERVO_ORGANIK_CLOSED\"}");
  }
  if (isAnorganikAutoClosing && (now - timeAnorganikOpened >= 4000)) {
    isAnorganikAutoClosing = false;
    servoAnorganikAngle = 0;
    servoAnorganik.write(0);
    Serial.println("{\"ack\":\"SERVO_ANORGANIK_CLOSED\"}");
  }
}

void handleIncomingSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "LED:1" || cmd == "LED:ON") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("{\"ack\":\"LED_ON\"}");
  }
  else if (cmd == "LED:0" || cmd == "LED:OFF") {
    digitalWrite(LED_PIN, LOW);
    Serial.println("{\"ack\":\"LED_OFF\"}");
  }
  else if (cmd == "SAMPAH:ORGANIK" || cmd == "SERVO_ORGANIK:OPEN" || cmd == "SERVO1:OPEN" || cmd == "SERVO1:90") {
    servoOrganikAngle = 45;
    servoOrganik.write(45);
    timeOrganikOpened = millis();
    isOrganikAutoClosing = true;
    Serial.println("{\"ack\":\"ORGANIK_OPENED\"}");
  }
  else if (cmd == "SERVO_ORGANIK:CLOSE" || cmd == "SERVO1:CLOSE" || cmd == "SERVO1:0") {
    isOrganikAutoClosing = false;
    servoOrganikAngle = 0;
    servoOrganik.write(0);
    Serial.println("{\"ack\":\"ORGANIK_CLOSED\"}");
  }
  else if (cmd == "SAMPAH:ANORGANIK" || cmd == "SERVO_ANORGANIK:OPEN" || cmd == "SERVO2:OPEN" || cmd == "SERVO2:90") {
    servoAnorganikAngle = 45;
    servoAnorganik.write(45);
    timeAnorganikOpened = millis();
    isAnorganikAutoClosing = true;
    Serial.println("{\"ack\":\"ANORGANIK_OPENED\"}");
  }
  else if (cmd == "SERVO_ANORGANIK:CLOSE" || cmd == "SERVO2:CLOSE" || cmd == "SERVO2:0") {
    isAnorganikAutoClosing = false;
    servoAnorganikAngle = 0;
    servoAnorganik.write(0);
    Serial.println("{\"ack\":\"ANORGANIK_CLOSED\"}");
  }
  else if (cmd == "PUMP:1" || cmd == "PUMP:ON") {
    pumpManualOverride = true;
    pumpManualState = true;
    digitalWrite(PUMP_PIN, HIGH);
    Serial.println("{\"ack\":\"PUMP_ON_MANUAL\"}");
  }
  else if (cmd == "PUMP:0" || cmd == "PUMP:OFF") {
    pumpManualOverride = true;
    pumpManualState = false;
    digitalWrite(PUMP_PIN, LOW);
    Serial.println("{\"ack\":\"PUMP_OFF_MANUAL\"}");
  }
  else if (cmd == "PUMP:AUTO") {
    pumpManualOverride = false;
    Serial.println("{\"ack\":\"PUMP_AUTO_MODE\"}");
  }
  else if (cmd == "BUZZER:1" || cmd == "BUZZER:ON") {
    buzzerManualOverride = true;
    buzzerManualState = true;
    lastBuzzerToneMillis = millis();
    tone(BUZZER_PIN, 65, 250);
    Serial.println("{\"ack\":\"BUZZER_ON\"}");
  }
  else if (cmd == "BUZZER:0" || cmd == "BUZZER:OFF") {
    buzzerManualOverride = true;
    buzzerManualState = false;
    noTone(BUZZER_PIN);
    Serial.println("{\"ack\":\"BUZZER_OFF\"}");
  }
  else if (cmd == "BUZZER:AUTO") {
    buzzerManualOverride = false;
    noTone(BUZZER_PIN);
    Serial.println("{\"ack\":\"BUZZER_AUTO\"}");
  }
  else if (cmd == "PING") {
    Serial.println("{\"ack\":\"PONG\"}");
  }
}
