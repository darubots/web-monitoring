#include <Servo.h>

// =========================================================================
// SMART CITY & PLTS IOT SENSOR NODE + SMART WASTE SYSTEM
// Komunikasi Serial USB ke Web Monitoring & Firebase
// =========================================================================

// =========================
// PIN SENSOR ANALOG
// =========================
#define WATER_PIN A0  // Sensor Ketinggian Air (Water Level)
#define MQ_PIN    A1  // Sensor Kualitas Udara (MQ Gas Sensor - Dipindah ke A1)
#define TDS_PIN   A2  // Sensor Kualitas Air (TDS Sensor)
#define NTC_PIN   A3  // Sensor Suhu (NTC Thermistor)

// =========================
// PIN DIGITAL & AKTUATOR
// =========================
#define BUZZER_PIN          9   // Pin Buzzer (Pin D9 dengan fungsi tone(9, 65, 250))
#define LED_PIN             5   // Lampu LED Miniatur (Bisa dikendalikan ON/OFF dari Web)
#define PUMP_PIN            6   // Relay Pompa Air
#define FIRE_PIN            7   // Sensor Api (Flame Sensor, LOW = Api Terdeteksi)
#define TILT_PIN            8   // Sensor Kemiringan / Getaran (Tilt, HIGH = Terguncang)
#define SERVO_ORGANIK_PIN   10  // Servo Tempat Sampah Organik (Pin D10)
#define SERVO_ANORGANIK_PIN 11  // Servo Tempat Sampah Anorganik (Pin D11)

// =========================
// OBJEK SERVO & VARIABEL SUDUT
// =========================
Servo servoOrganik;
Servo servoAnorganik;

int servoOrganikAngle = 0;   // 0 = Tertutup, 90 = Terbuka
int servoAnorganikAngle = 0; // 0 = Tertutup, 90 = Terbuka

// Timer Auto-close Tutup Tempat Sampah (menutup sendiri setelah 4 detik)
unsigned long timeOrganikOpened = 0;
bool isOrganikAutoClosing = false;

unsigned long timeAnorganikOpened = 0;
bool isAnorganikAutoClosing = false;

// Override manual dari Web untuk Pompa & Buzzer
bool pumpManualOverride = false;
bool pumpManualState = false;

// DEFAULT BUZZER OVERRIDE = TRUE & STATE = FALSE:
// MEMASTIKAN BUZZER TIDAK BERBUNYI SENDIRI SAAT ARDUINO BOOT!
bool buzzerManualOverride = true;
bool buzzerManualState = false;

// Timer Sirine Nada Buzzer tone(3, 65, 250) + jeda 250ms
unsigned long lastBuzzerToneMillis = 0;
const unsigned long BUZZER_TONE_CYCLE = 500; // 250 ms bunyi + 250 ms jeda

// Dummy Generator untuk Tegangan (Volt 4-5V) & Arus (Ampere 2-3A)
float dummyVolt = 4.50;
float dummyAmp = 2.40;

// Interval Pengiriman Data JSON (300 ms untuk data streaming realtime & responsif ke Web)
unsigned long previousMillis = 0;
const unsigned long SEND_INTERVAL = 300;

// Buffer perintah Serial
String inputBuffer = "";

// Variabel Filter Exponential Moving Average (EMA) agar grafik & bar naik-turun halus tanpa glitch
float filteredWater = -1;
float filteredMq    = -1;
float filteredTds   = -1;
float filteredNtc   = -1;

// Status terkini api dan getaran
bool fireDetected = false;
bool tiltDetected = false;

// Fungsi pembacaan analog multi-sampling untuk membuang noise ADC & crosstalk
int readAnalogFiltered(uint8_t pin) {
  analogRead(pin); // Flush sisa muatan channel multiplexer
  delayMicroseconds(40);
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(pin);
    delayMicroseconds(20);
  }
  return (int)(sum / 8);
}

// Fungsi update bunyi sirine buzzer menggunakan tone(3, 65, 250) secara non-blocking
void updateBuzzer() {
  bool shouldSound = false;
  if (buzzerManualOverride) {
    shouldSound = buzzerManualState;
  } else {
    shouldSound = (fireDetected || tiltDetected);
  }

  if (shouldSound) {
    unsigned long now = millis();
    if (now - lastBuzzerToneMillis >= BUZZER_TONE_CYCLE) {
      lastBuzzerToneMillis = now;
      tone(BUZZER_PIN, 65, 250); // Sesuai permintaan pengguna: tone(3, 65, 250)
    }
  } else {
    noTone(BUZZER_PIN);
  }
}

void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(A4)); // Seed acak dari pin analog kosong

  // Pin Sensor Analog
  pinMode(WATER_PIN, INPUT);
  pinMode(MQ_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(NTC_PIN, INPUT);

  // Pin Sensor Digital (Gunakan INPUT_PULLUP agar pin tidak mengambang/floating)
  pinMode(FIRE_PIN, INPUT_PULLUP);
  pinMode(TILT_PIN, INPUT_PULLUP);

  // Pin Output Digital
  pinMode(LED_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Pasang Servo
  servoOrganik.attach(SERVO_ORGANIK_PIN);
  servoAnorganik.attach(SERVO_ANORGANIK_PIN);

  // Pastikan Kondisi Awal Semua Output MATI (LOW / noTone)
  digitalWrite(LED_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);
  noTone(BUZZER_PIN);

  servoOrganik.write(0);
  servoAnorganik.write(0);

  Serial.println("SYSTEM_READY");
}

void loop() {
  // 1. Baca Perintah Masuk dari Laptop / Web
  handleIncomingSerial();

  // 2. Cek Auto-Close untuk Servo Tempat Sampah (Organik & Anorganik)
  checkServoAutoClose();

  // 3. Update Nada Sirine Buzzer tone(3, 65, 250)
  updateBuzzer();

  // 4. Loop Utama Pengiriman Telemetri Berdasarkan Timer Millis
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= SEND_INTERVAL) {
    previousMillis = currentMillis;

    // BACA SENSOR FISIK DENGAN MULTI-SAMPLING & FILTER PENGHALUS (ANTI-GLITCH)
    int rawWater = readAnalogFiltered(WATER_PIN);
    int rawMq    = readAnalogFiltered(MQ_PIN);
    int rawTds   = readAnalogFiltered(TDS_PIN);
    int rawNtc   = readAnalogFiltered(NTC_PIN);

    // Inisialisasi awal atau smoothing (Low-Pass Filter)
    if (filteredNtc < 0) {
      filteredWater = rawWater;
      filteredMq    = rawMq;
      filteredTds   = rawTds;
      filteredNtc   = rawNtc;
    } else {
      // Bobot 0.85 nilai lama + 0.15 nilai baru agar garis sensor bergerak pelan-pelan naik dan turun
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

    // Flame sensor aktif LOW saat ada api.
    // Tilt sensor aktif LOW saat terguncang (dengan INPUT_PULLUP)
    fireDetected = (fireValue == LOW);
    tiltDetected = (tiltValue == LOW);

    // KENDALI POMPA AIR
    if (pumpManualOverride) {
      digitalWrite(PUMP_PIN, pumpManualState ? HIGH : LOW);
    } else {
      if (waterValue > 500) {
        digitalWrite(PUMP_PIN, HIGH);
      } else {
        digitalWrite(PUMP_PIN, LOW);
      }
    }

    // UPDATE DATA DUMMY VOLTASE (4.0 - 5.0 V) & AMPERE (2.0 - 3.0 A)
    // Berfluktuasi secara halus dan realistis
    float deltaV = (random(-6, 7)) / 100.0;
    dummyVolt = constrain(dummyVolt + deltaV, 4.00, 5.00);

    float deltaA = (random(-5, 6)) / 100.0;
    dummyAmp = constrain(dummyAmp + deltaA, 2.00, 3.00);

    float dummyPower = dummyVolt * dummyAmp;

    // KIRIM JSON LENGKAP KE WEB / LAPTOP
    sendJsonTelemetry(waterValue, mqValue, tdsValue, ntcValue, dummyVolt, dummyAmp, dummyPower, fireDetected, tiltDetected);
  }
}

// Fungsi mengirimkan data terformat JSON
void sendJsonTelemetry(int water, int mq, int tds, int ntc, float volt, float amp, float power, bool fire, bool tilt) {
  Serial.print("{");

  Serial.print("\"water\":");
  Serial.print(water);

  Serial.print(",\"mq\":");
  Serial.print(mq);

  Serial.print(",\"tds\":");
  Serial.print(tds);

  Serial.print(",\"ntc\":");
  Serial.print(ntc);

  Serial.print(",\"volt\":");
  Serial.print(volt, 2);

  Serial.print(",\"ampere\":");
  Serial.print(amp, 2);

  Serial.print(",\"power\":");
  Serial.print(power, 2);

  Serial.print(",\"fire\":");
  Serial.print(fire ? 1 : 0);

  Serial.print(",\"tilt\":");
  Serial.print(tilt ? 1 : 0);

  Serial.print(",\"pump\":");
  Serial.print(digitalRead(PUMP_PIN));

  Serial.print(",\"buzzer\":");
  bool isBuzzerActive = buzzerManualOverride ? buzzerManualState : (fire || tilt);
  Serial.print(isBuzzerActive ? 1 : 0);

  Serial.print(",\"led\":");
  Serial.print(digitalRead(LED_PIN));

  Serial.print(",\"servo_organik\":");
  Serial.print(servoOrganikAngle);

  Serial.print(",\"servo_anorganik\":");
  Serial.print(servoAnorganikAngle);

  Serial.println("}");
}

// Cek apakah sudah waktunya menutup otomatis tempat sampah setelah dibuka
void checkServoAutoClose() {
  unsigned long now = millis();

  // Organik
  if (isOrganikAutoClosing && (now - timeOrganikOpened >= 4000)) {
    isOrganikAutoClosing = false;
    servoOrganikAngle = 0;
    servoOrganik.write(0);
    Serial.println("{\"ack\":\"SERVO_ORGANIK_CLOSED\"}");
  }

  // Anorganik
  if (isAnorganikAutoClosing && (now - timeAnorganikOpened >= 4000)) {
    isAnorganikAutoClosing = false;
    servoAnorganikAngle = 0;
    servoAnorganik.write(0);
    Serial.println("{\"ack\":\"SERVO_ANORGANIK_CLOSED\"}");
  }
}

// Baca perintah masuk dari laptop / web
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

// Proses perintah yang dikirimkan Web / AI Vision Python:
// - LED:1 / LED:0            -> Menyalakan / Mematikan LED D5
// - SAMPAH:ORGANIK           -> Buka servo organik 90 deg selama 4 detik
// - SAMPAH:ANORGANIK         -> Buka servo anorganik 90 deg selama 4 detik
// - SERVO_ORGANIK:OPEN/CLOSE -> Buka / tutup manual
// - SERVO_ANORGANIK:OPEN/CLOSE
// - PUMP:1 / PUMP:0 / PUMP:AUTO
// - BUZZER:1 / BUZZER:0 / BUZZER:AUTO
void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  // --- KENDALI LED D5 ---
  if (cmd == "LED:1" || cmd == "LED:ON") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("{\"ack\":\"LED_ON\"}");
  }
  else if (cmd == "LED:0" || cmd == "LED:OFF") {
    digitalWrite(LED_PIN, LOW);
    Serial.println("{\"ack\":\"LED_OFF\"}");
  }

  // --- KENDALI SAMPAH ORGANIK (SERVO D10) ---
  else if (cmd == "SAMPAH:ORGANIK" || cmd == "SERVO_ORGANIK:OPEN" || cmd == "SERVO1:OPEN" || cmd == "SERVO1:90") {
    servoOrganikAngle = 90;
    servoOrganik.write(90);
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

  // --- KENDALI SAMPAH ANORGANIK (SERVO D11) ---
  else if (cmd == "SAMPAH:ANORGANIK" || cmd == "SERVO_ANORGANIK:OPEN" || cmd == "SERVO2:OPEN" || cmd == "SERVO2:90") {
    servoAnorganikAngle = 90;
    servoAnorganik.write(90);
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

  // Sudut Langsung
  else if (cmd.startsWith("SERVO1:")) {
    int angle = cmd.substring(7).toInt();
    servoOrganikAngle = constrain(angle, 0, 180);
    servoOrganik.write(servoOrganikAngle);
    isOrganikAutoClosing = false;
  }
  else if (cmd.startsWith("SERVO2:")) {
    int angle = cmd.substring(7).toInt();
    servoAnorganikAngle = constrain(angle, 0, 180);
    servoAnorganik.write(servoAnorganikAngle);
    isAnorganikAutoClosing = false;
  }

  // --- KENDALI POMPA AIR ---
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

  // --- KENDALI BUZZER DENGAN TONE (Pin D3, 65 Hz, 250 ms) ---
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