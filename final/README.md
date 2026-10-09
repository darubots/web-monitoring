# 🏙️ Smart City, PLTS IoT Monitoring & AI Vision System

Sistem terintegrasi untuk pemantauan kota cerdas berbasis IoT (Arduino Uno), Pembangkit Listrik Tenaga Surya (PLTS), dan Computer Vision (YOLOv8 AI untuk CCTV lalu lintas serta pemilahan sampah pintar).

---

## 📁 Struktur Folder Project

```text
final/
│
├── 📜 start_dashboard.bat        # Launcher 1-klik untuk menjalankan server & web
├── 📜 upload_arduino.bat        # Skrip upload firmware ke Arduino Uno
├── 📜 requirements.txt          # Daftar library Python yang dibutuhkan
├── 📜 app_vision.py             # Server Flask backend AI YOLO (Port 5000)
├── 📜 yolov8n.pt                # Model AI YOLO ringan untuk deteksi cepat
│
├── 📂 website/                  # Antarmuka Web Dashboard (Port 8000)
│   ├── index.html               # Halaman utama dashboard widescreen
│   ├── serial-service.js        # Driver Web Serial API (USB Arduino)
│   ├── firebase-service.js      # Driver sinkronisasi Firebase Realtime Database
│   ├── firebase-config.js       # Konfigurasi default Firebase
│   └── diorama_camera.webp      # Gambar visual mock-up CCTV
│
├── 📂 monitoring_plts_firebase/ # Source Code Firmware Arduino
│   └── monitoring_plts_firebase.ino
│
├── 📂 object detection/         # Modul & bobot model pendukung deteksi objek
│   ├── object detection.py
│   └── yolov8s.pt
│
└── 📂 sampah/                   # Sampel gambar referensi deteksi sampah
    └── recyclable/
        └── botol.jpg
```

---

## 🚀 Panduan Menjalankan di Laptop Baru

### 1. Prasyarat Sistem
- **Python 3.9 s/d 3.12** terinstal (centang opsi *"Add python.exe to PATH"* saat instalasi).
- **Arduino IDE** (untuk memprogram Arduino Uno).
- **Web Browser modern** berbasis Chromium (Google Chrome, Microsoft Edge, atau Brave) karena mendukung **Web Serial API**.

### 2. Instalasi Dependensi Python
Buka Terminal / Command Prompt di dalam folder `final/`, lalu jalankan:
```bash
pip install -r requirements.txt
```

### 3. Upload Kode ke Arduino Uno
1. Hubungkan Arduino Uno ke port USB laptop.
2. Buka file sketch:
   `monitoring_plts_firebase/monitoring_plts_firebase.ino`
   menggunakan **Arduino IDE**.
3. Pastikan Board dipilih: **Arduino Uno** dan Port dipilih sesuai port USB yang terdeteksi (misal `COM3` atau `COM4`).
4. Klik tombol **Upload** (ikon panah kanan) di Arduino IDE hingga muncul pesan *"Done uploading"*.

### 4. Menjalankan Dashboard
Cukup klik ganda (double-click) file:
```text
start_dashboard.bat
```
Skrip ini akan secara otomatis:
1. Menjalankan backend AI Vision (`app_vision.py`) di background pada port `5000`.
2. Menjalankan web server lokal pada port `8000`.
3. Membuka dashboard web di browser pada alamat:
   **`http://localhost:8000/website/`**

### 5. Menghubungkan Hardware ke Website
1. Pada navbar web di pojok kanan atas, klik tombol **"Hubungkan USB"**.
2. Pilih perangkat Arduino Uno Anda pada pop-up browser, lalu klik **Connect**.
3. Status akan berubah menjadi **ONLINE (USB)** dan data sensor akan mulai mengalir secara real-time.

---

## 📌 Pinout Hardware Arduino Uno

| Komponen / Sensor | Tipe Pin | Pin Arduino | Keterangan & Fungsi |
| :--- | :---: | :---: | :--- |
| **Water Level Sensor** | Analog | **A0** | Membaca ketinggian air (0-1023), otomatis menyalakan pompa jika > 500 |
| **MQ Gas Sensor** | Analog | **A1** | Kualitas udara & pendeteksi kebocoran gas |
| **TDS Sensor** | Analog | **A2** | Kualitas air & ppm kejernihan |
| **NTC Thermistor** | Analog | **A3** | Suhu lingkungan (°C) |
| **Lampu LED Miniatur** | Digital | **Pin 5 (D5)** | Indikator penerangan miniatur kota (Kontrol ON/OFF) |
| **Relay Pompa Air** | Digital | **Pin 6 (D6)** | Otomatis ON jika air > 500 atau kendali manual web |
| **Flame / Fire Sensor** | Digital | **Pin 7 (D7)** | Deteksi nyala api (Pemicu sirine bahaya kebakaran) |
| **Tilt / Kemiringan** | Digital | **Pin 8 (D8)** | Deteksi guncangan gempa (Pemicu sirine gempa) |
| **Sirine Buzzer (Tone)**| Digital | **Pin 9 (D9)** | Sirine nada `tone(9, 65, 250)` per 250ms |
| **Servo Sampah Organik**| Digital PWM| **Pin 10 (D10)**| Membuka tutup tempat sampah organik saat AI mendeteksi sisa makanan |
| **Servo Sampah Anorganik**| Digital PWM| **Pin 11 (D11)**| Membuka tutup tempat sampah anorganik saat AI mendeteksi botol/plastik |
| **Kelistrikan PLTS DC** | Software / JSON | *Dummy* | Tegangan: **4.0 - 5.0 V**, Arus: **2.0 - 3.0 A**, Daya: **Volt × Ampere** |

---

## 📡 Protokol JSON & Komunikasi Serial

### A. Format Data Sensor (Dikirim Arduino Tiap 300 ms)
Arduino mengirim 1 baris JSON utuh via Serial `9600 baud`:
```json
{"water":185,"mq":210,"tds":320,"ntc":498,"volt":4.52,"ampere":2.38,"power":10.76,"fire":0,"tilt":0,"pump":0,"buzzer":0,"led":0,"servo_organik":0,"servo_anorganik":0}
```

### B. Perintah Kendali (Dikirim dari Web ke Arduino)
String teks polos diakhiri Newline (`\n`):
- `LED:1` / `LED:0` : Nyalakan / Matikan LED Pin D5
- `PUMP:1` / `PUMP:0` / `PUMP:AUTO` : Kendali Relay Pompa Air Pin D6
- `BUZZER:1` / `BUZZER:0` / `BUZZER:AUTO` : Kendali Sirine Buzzer Pin D9
- `SAMPAH:ORGANIK` : Buka servo D10 ke 90° (tutup otomatis setelah 4 detik)
- `SAMPAH:ANORGANIK` : Buka servo D11 ke 90° (tutup otomatis setelah 4 detik)

### C. Respon Konfirmasi Arduino (ACK)
```json
{"ack":"LED_ON"}
{"ack":"LED_OFF"}
{"ack":"PUMP_ON_MANUAL"}
{"ack":"PUMP_OFF_MANUAL"}
{"ack":"PUMP_AUTO_MODE"}
{"ack":"BUZZER_ON"}
{"ack":"BUZZER_OFF"}
{"ack":"BUZZER_AUTO"}
{"ack":"ORGANIK_OPENED"}
{"ack":"ANORGANIK_OPENED"}
```

---

## 🌐 Port Jaringan yang Digunakan
- **Port 8000**: Web Dashboard (`http://localhost:8000/website/`)
- **Port 5000**: AI Vision Flask Server (`http://localhost:5000`)
- **Port USB (COM)**: Komunikasi Serial Arduino Uno (9600 bps)
