#!/usr/bin/env python3
"""
=============================================================================
SMART CITY AI VISION SERVER: TRAFFIC & SMART WASTE CLASSIFIER
=============================================================================
Server Flask untuk 2 Dashboard AI Vision:
1. CCTV Lalu Lintas Jalan Raya (YouTube Live Stream + YOLO)
2. Smart Waste Sorting (Deteksi Sampah Organik vs Anorganik + Servo D10 & D11)
3. Sinkronisasi Event ke Firebase Realtime Database
=============================================================================
"""

import os
import sys
import time
import json
import threading
from datetime import datetime
import cv2
import numpy as np
from flask import Flask, Response, jsonify, request
from ultralytics import YOLO

# Coba import streamlink & requests
try:
    from streamlink import Streamlink
    STREAMLINK_AVAILABLE = True
except ImportError:
    STREAMLINK_AVAILABLE = False

try:
    import requests
    REQUESTS_AVAILABLE = True
except ImportError:
    REQUESTS_AVAILABLE = False

# Serial opsional jika ingin langsung mengirim perintah ke Arduino dari Python
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

app = Flask(__name__)

# Konfigurasi Model & Path:
# 1. CCTV Model: YOLOv8 Medium (Akurasi tinggi untuk kendaraan jarak jauh)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(BASE_DIR, "yolov8m.pt")
if not os.path.exists(MODEL_PATH):
    MODEL_PATH = os.path.join(BASE_DIR, "object detection", "yolov8s.pt")
if not os.path.exists(MODEL_PATH):
    MODEL_PATH = os.path.join(BASE_DIR, "yolov8n.pt")

print(f"📦 [AI CCTV] Memuat model: {MODEL_PATH}")
yolo_model = YOLO(MODEL_PATH)

# 2. Smart Waste Model: YOLOv8 Small / Nano (Ultra-Fast 0-Delay ~35ms untuk webcam realtime)
WASTE_MODEL_PATH = os.path.join(BASE_DIR, "object detection", "yolov8s.pt")
if not os.path.exists(WASTE_MODEL_PATH):
    WASTE_MODEL_PATH = os.path.join(BASE_DIR, "yolov8s.pt")
if not os.path.exists(WASTE_MODEL_PATH):
    WASTE_MODEL_PATH = MODEL_PATH

print(f"⚡ [AI WASTE] Memuat model deteksi sampah kilat: {WASTE_MODEL_PATH}")
yolo_waste_model = YOLO(WASTE_MODEL_PATH)

# Warmup model di awal agar tidak terjadi jeda kompilasi PyTorch 1.8 detik saat frame pertama dibuka
print("🔥 Melakukan warmup AI model agar tidak ada lag saat pertama kali kamera dinyalakan...")
_warmup_f = np.zeros((480, 640, 3), dtype=np.uint8)
yolo_model(_warmup_f, imgsz=480, verbose=False)
yolo_waste_model(_warmup_f, imgsz=480, verbose=False)
print("✅ AI Models siap dengan latensi ultra rendah!")

# Link YouTube Live CCTV Jalan Raya
YOUTUBE_URL = "https://www.youtube.com/live/JJ3MWNYVCU4?si=00EZLlAVTAP0caTL"

# Klasifikasi Sampah Berdasarkan Kelas COCO
ORGANIC_CLASSES = {
    'banana', 'apple', 'sandwich', 'orange', 'broccoli', 'carrot',
    'hot dog', 'pizza', 'donut', 'cake', 'potted plant', 'food'
}

INORGANIC_CLASSES = {
    'bottle', 'wine glass', 'cup', 'fork', 'knife', 'spoon', 'bowl',
    'can', 'plastic', 'cell phone', 'remote', 'keyboard', 'mouse',
    'laptop', 'scissors', 'toothbrush', 'book', 'paper', 'cardboard',
    'box', 'tin can'
}

VEHICLE_CLASSES = {
    0: 'person',
    1: 'bicycle',
    2: 'car',
    3: 'motorcycle',
    5: 'bus',
    7: 'truck'
}

# State Global
traffic_state = {
    "total_vehicles": 0,
    "cars": 0,
    "motorcycles": 0,
    "buses": 0,
    "trucks": 0,
    "density": "Lancar",
    "updated_at": "",
    "status": "Inisialisasi"
}

waste_state = {
    "latest_item": "Menunggu objek...",
    "category": "STANDBY",  # 'ORGANIK' | 'ANORGANIK' | 'STANDBY'
    "confidence": 0.0,
    "target_servo": "None", # 'D10' (Organik) | 'D11' (Anorganik)
    "history": [],
    "last_detection_time": 0
}

latest_traffic_frame = None
latest_waste_frame = None
frame_lock = threading.Lock()

# Firebase Helper
def push_event_to_firebase(path, payload):
    if not REQUESTS_AVAILABLE:
        return
    try:
        # Cek apakah ada firebase_config.json
        cfg_file = os.path.join(BASE_DIR, "firebase_config.json")
        db_url = None
        if os.path.exists(cfg_file):
            with open(cfg_file, 'r', encoding='utf-8') as f:
                cfg = json.load(f)
                db_url = cfg.get("databaseURL", "").rstrip("/")

        if db_url and "YOUR_PROJECT_ID" not in db_url:
            url = f"{db_url}/{path.lstrip('/')}.json"
            requests.put(url, json=payload, timeout=2)
    except Exception as e:
        pass

# =========================================================================
# THREAD 1: CCTV JALAN RAYA (YOUTUBE STREAMLINK + YOLO)
# =========================================================================
def traffic_worker():
    global latest_traffic_frame, traffic_state
    print("🚦 Thread CCTV Lalu Lintas dimulai...")

    while True:
        cap = None
        stream_url = None

        if STREAMLINK_AVAILABLE:
            try:
                session = Streamlink()
                streams = session.streams(YOUTUBE_URL)
                # Prioritaskan stream kualitas lebih tinggi untuk deteksi yang jelas
                for res in ['720p', '480p', 'best', '360p']:
                    if res in streams:
                        stream_url = streams[res].to_url()
                        print(f"🎥 Menggunakan stream YouTube resolusi: {res}")
                        break
            except Exception as e:
                print(f"⚠️ Gagal mendapatkan stream YouTube: {e}")

        if stream_url:
            cap = cv2.VideoCapture(stream_url)
            traffic_state["status"] = "Terhubung ke YouTube Live"
        else:
            traffic_state["status"] = "Mode Fallback Video Lalu Lintas"

        # Loop membaca frame
        frame_count = 0
        while cap and cap.isOpened():
            success, frame = cap.read()
            if not success:
                print("⚠️ Stream frame terputus. Mencoba reconnect...")
                break

            frame_count += 1
            # Proses deteksi tiap 2 frame untuk menghemat CPU
            if frame_count % 2 == 0:
                h, w = frame.shape[:2]
                # Pertahankan resolusi widescreen (hingga 1024px) agar objek kendaraan terlihat jelas
                if w > 1024:
                    frame = cv2.resize(frame, (1024, int(h * (1024 / w))))
                elif w < 720:
                    # Upscale resolusi jika stream rendah (360p) agar YOLOv8m dapat mendeteksi objek kecil
                    frame = cv2.resize(frame, (960, int(h * (960 / w))))

                # Jalankan deteksi kendaraan dengan YOLOv8 (conf=0.25, NMS iou=0.45)
                # Kelas: person(0), bicycle(1), car(2), motorcycle(3), bus(5), truck(7)
                results = yolo_model(frame, classes=[0, 1, 2, 3, 5, 7], conf=0.25, iou=0.45, verbose=False)
                
                # Visualisasi hasil bawaan YOLO dengan garis & teks rapi (seperti di object detection.py)
                annotated_frame = results[0].plot(line_width=1, font_size=8)

                cars = 0
                motos = 0
                buses = 0
                trucks = 0

                vehicle_motor_boxes = []
                rider_boxes = []

                for box in results[0].boxes:
                    cls_id = int(box.cls[0])
                    conf = float(box.conf[0])
                    x1, y1, x2, y2 = map(int, box.xyxy[0])

                    if cls_id == 2:
                        cars += 1
                    elif cls_id == 3:
                        motos += 1
                        vehicle_motor_boxes.append((x1, y1, x2, y2))
                    elif cls_id == 5:
                        buses += 1
                    elif cls_id == 7:
                        trucks += 1
                    elif cls_id == 1:
                        # Sepeda / Roda Dua
                        motos += 1
                    elif cls_id == 0:
                        # Person di jalan raya (rider motor)
                        rider_boxes.append((x1, y1, x2, y2, conf))

                # Logika asosiasi cerdas:
                # Jika pengendara motor malam hari terdeteksi sebagai 'person' tanpa box motor di bawahnya, hitung sebagai motor
                for rx1, ry1, rx2, ry2, rconf in rider_boxes:
                    is_riding_existing = False
                    for mx1, my1, mx2, my2 in vehicle_motor_boxes:
                        inter_x1 = max(rx1, mx1)
                        inter_y1 = max(ry1, my1)
                        inter_x2 = min(rx2, mx2)
                        inter_y2 = min(ry2, my2)
                        if inter_x2 > inter_x1 and inter_y2 > inter_y1:
                            inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1)
                            rider_area = (rx2 - rx1) * (ry2 - ry1)
                            if inter_area / max(1, rider_area) > 0.25:
                                is_riding_existing = True
                                break

                    if not is_riding_existing:
                        motos += 1
                        vehicle_motor_boxes.append((rx1, ry1, rx2, ry2))

                total = cars + motos + buses + trucks
                density = "Lancar"
                if total > 15: density = "Padat Merayap"
                elif total > 7: density = "Ramai Lancar"

                traffic_state["total_vehicles"] = total
                traffic_state["cars"] = cars
                traffic_state["motorcycles"] = motos
                traffic_state["buses"] = buses
                traffic_state["trucks"] = trucks
                traffic_state["density"] = density
                traffic_state["updated_at"] = datetime.now().strftime("%H:%M:%S")

                # Tambahkan overlay HUD modern bergaya Command Center
                hud_w, hud_h = 360, 92
                hud_overlay = annotated_frame.copy()
                cv2.rectangle(hud_overlay, (15, 15), (15 + hud_w, 15 + hud_h), (10, 16, 26), -1)
                cv2.addWeighted(hud_overlay, 0.85, annotated_frame, 0.15, 0, annotated_frame)
                cv2.rectangle(annotated_frame, (15, 15), (15 + hud_w, 15 + hud_h), (0, 238, 252), 1)

                cv2.putText(annotated_frame, f"TOTAL KENDARAAN: {total} UNIT", (25, 45),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 238, 252), 2, cv2.LINE_AA)
                cv2.putText(annotated_frame, f"STATUS: {density}", (25, 68),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
                cv2.putText(annotated_frame, f"Mobil: {cars}  |  Motor: {motos}  |  Bus/Truk: {buses + trucks}", (25, 88),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.40, (180, 215, 225), 1, cv2.LINE_AA)

                with frame_lock:
                    latest_traffic_frame = annotated_frame.copy()

            time.sleep(0.03)

        if cap:
            cap.release()

        # Jika stream gagal atau putus, buat frame placeholder interaktif
        placeholder = np.zeros((540, 960, 3), dtype=np.uint8)
        cv2.putText(placeholder, "CCTV LALU LINTAS - MENGHUBUNGKAN ULANG...", (80, 270),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.85, (0, 238, 252), 2)
        with frame_lock:
            latest_traffic_frame = placeholder
        time.sleep(3)


# =========================================================================
# THREADED CAMERA CAPTURE (MENGHILANGKAN BUFFER DELAY 100% PADA WINDOWS)
# =========================================================================
class DirectShowWebcam:
    """Threaded Webcam Reader untuk membuang antrian buffer driver Windows.
    Menghasilkan video real-time 0ms delay tanpa lag."""
    def __init__(self, src=0):
        # DirectShow (CAP_DSHOW) di Windows membuka webcam tanpa buffer latency
        self.cap = cv2.VideoCapture(src, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            self.cap = cv2.VideoCapture(src)
        
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.cap.set(cv2.CAP_PROP_FPS, 30)

        self.running = True
        self.frame = None
        self.lock = threading.Lock()
        
        if self.cap.isOpened():
            ret, f = self.cap.read()
            if ret:
                self.frame = f
            self.thread = threading.Thread(target=self._reader, daemon=True)
            self.thread.start()

    def _reader(self):
        while self.running:
            ret, f = self.cap.read()
            if ret:
                with self.lock:
                    self.frame = f
            time.sleep(0.005) # Menguras buffer driver secara real-time

    def read(self):
        with self.lock:
            return self.frame.copy() if self.frame is not None else None

    def is_opened(self):
        return self.cap.isOpened() if self.cap else False

    def release(self):
        self.running = False
        if self.cap:
            self.cap.release()


# =========================================================================
# THREAD 2: DETEKSI SAMPAH ULTRA-FAST (0-DELAY REALTIME)
# =========================================================================
def waste_worker():
    global latest_waste_frame, waste_state
    print("♻️ Thread Deteksi Sampah Ultra-Fast (0-Delay) dimulai...")

    cam = DirectShowWebcam(0)
    use_webcam = cam.is_opened()
    if use_webcam:
        print("📷 Webcam terhubung via DirectShow (Zero Buffer Delay)!")
    else:
        print("ℹ️ Webcam tidak terdeteksi. Menggunakan pipeline simulasi gambar sampah dinamis.")

    # Siapkan gambar sampel untuk fallback
    sample_bottle_path = os.path.join(BASE_DIR, "sampah", "recyclable", "botol.jpg")
    sample_img = None
    if os.path.exists(sample_bottle_path):
        sample_img = cv2.imread(sample_bottle_path)

    frame_count = 0
    cached_frame = None

    while True:
        frame = cam.read() if use_webcam else None

        if frame is None:
            # Buat frame simulasi inspeksi tempat sampah (format widescreen 960x540)
            if sample_img is not None:
                frame = sample_img.copy()
            else:
                frame = np.zeros((540, 960, 3), dtype=np.uint8)
                cv2.rectangle(frame, (180, 60), (780, 480), (35, 42, 54), -1)
                cv2.putText(frame, "KAMERA SMART WASTE READY (WIDESCREEN)", (200, 270),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.75, (78, 222, 163), 2)

        frame_count += 1

        # Jalankan deteksi YOLO cepat (imgsz=480, model yolo_waste_model ~35ms)
        # Deteksi tiap frame / 2 frame agar CPU tetap dingin dan video 30 FPS super mulus
        if frame_count % 2 == 0 or cached_frame is None:
            h, w = frame.shape[:2]
            inf_frame = frame
            if w > 960:
                inf_frame = cv2.resize(frame, (960, int(h * (960 / w))))

            results = yolo_waste_model(inf_frame, conf=0.28, imgsz=480, verbose=False)
            annotated_frame = results[0].plot(line_width=1, font_size=8)
            
            detected_category = None
            detected_item_name = None
            detected_conf = 0.0

            for box in results[0].boxes:
                cls_id = int(box.cls[0])
                name = yolo_waste_model.names[cls_id].lower()
                conf = float(box.conf[0])

                # Cek apakah organik atau anorganik
                if name in ORGANIC_CLASSES:
                    detected_category = "ORGANIK"
                    detected_item_name = name
                    detected_conf = conf
                    break
                elif name in INORGANIC_CLASSES:
                    detected_category = "ANORGANIK"
                    detected_item_name = name
                    detected_conf = conf
                    break

            # Jika terdeteksi kategori sampah dan belum dalam cooldown 3 detik
            now = time.time()
            if detected_category and (now - waste_state["last_detection_time"] > 3.0):
                waste_state["last_detection_time"] = now
                waste_state["category"] = detected_category
                waste_state["latest_item"] = detected_item_name
                waste_state["confidence"] = round(detected_conf, 2)
                waste_state["target_servo"] = "D10" if detected_category == "ORGANIK" else "D11"

                # Rekam riwayat
                event_record = {
                    "item": detected_item_name,
                    "category": detected_category,
                    "confidence": round(detected_conf * 100, 1),
                    "servo": waste_state["target_servo"],
                    "timestamp": datetime.now().strftime("%H:%M:%S")
                }
                waste_state["history"].insert(0, event_record)
                if len(waste_state["history"]) > 20:
                    waste_state["history"].pop()

                print(f"🗑️ [AI WASTE INSTAN] Terdeteksi: {detected_item_name} -> {detected_category}! Buka Servo {waste_state['target_servo']}")

                # Push ke Firebase
                push_event_to_firebase("monitoring/waste_events", event_record)

            # Banner HUD pada feed
            hud_bg = (0, 100, 0) if waste_state["category"] == "ORGANIK" else ((120, 80, 0) if waste_state["category"] == "ANORGANIK" else (30, 30, 30))
            cv2.rectangle(annotated_frame, (10, 10), (320, 65), hud_bg, -1)
            cv2.rectangle(annotated_frame, (10, 10), (320, 65), (255, 255, 255), 1)
            cv2.putText(annotated_frame, f"STATUS: {waste_state['category']} (Servo {waste_state['target_servo']})",
                        (20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.48, (255, 255, 255), 2)
            cv2.putText(annotated_frame, f"Objek: {waste_state['latest_item']}",
                        (20, 53), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (220, 220, 220), 1)

            cached_frame = annotated_frame

        with frame_lock:
            latest_waste_frame = cached_frame

        time.sleep(0.01)


# =========================================================================
# FLASK STREAMING GENERATORS & ENDPOINTS
# =========================================================================
def generate_mjpeg(get_frame_fn):
    while True:
        with frame_lock:
            frame = get_frame_fn()
        if frame is not None:
            ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 72])
            if ret:
                frame_bytes = buffer.tobytes()
                yield (b'--frame\r\n'
                       b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
        time.sleep(0.02)

@app.after_request
def add_cors_headers(response):
    response.headers['Access-Control-Allow-Origin'] = '*'
    response.headers['Access-Control-Allow-Headers'] = 'Content-Type,Authorization'
    response.headers['Access-Control-Allow-Methods'] = 'GET,PUT,POST,DELETE,OPTIONS'
    return response

@app.route('/video_feed/traffic')
def video_feed_traffic():
    """Streaming CCTV Jalan Raya dengan Deteksi Kendaraan YOLO"""
    return Response(generate_mjpeg(lambda: latest_traffic_frame),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/video_feed/waste')
def video_feed_waste():
    """Streaming Kamera Pemilah Sampah Organik & Anorganik"""
    return Response(generate_mjpeg(lambda: latest_waste_frame),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/api/traffic/stats')
def api_traffic_stats():
    return jsonify(traffic_state)

@app.route('/api/waste/status')
def api_waste_status():
    return jsonify(waste_state)

@app.route('/api/waste/trigger/<category>', methods=['POST', 'GET'])
def api_waste_trigger(category):
    """Memicu deteksi sampah manual dari Web untuk pengujian servo"""
    cat = category.upper()
    if cat not in ['ORGANIK', 'ANORGANIK']:
        return jsonify({"error": "Kategori harus ORGANIK atau ANORGANIK"}), 400

    item_name = "Apel / Buah Organik" if cat == "ORGANIK" else "Botol Plastik Bekas"
    servo = "D10" if cat == "ORGANIK" else "D11"

    waste_state["last_detection_time"] = time.time()
    waste_state["category"] = cat
    waste_state["latest_item"] = item_name
    waste_state["confidence"] = 0.95
    waste_state["target_servo"] = servo

    event_record = {
        "item": item_name,
        "category": cat,
        "confidence": 95.0,
        "servo": servo,
        "timestamp": datetime.now().strftime("%H:%M:%S")
    }
    waste_state["history"].insert(0, event_record)
    if len(waste_state["history"]) > 20:
        waste_state["history"].pop()

    push_event_to_firebase("monitoring/waste_events", event_record)

    return jsonify({
        "status": "success",
        "category": cat,
        "item": item_name,
        "servo": servo,
        "command": f"SAMPAH:{cat}"
    })

@app.route('/api/status')
def api_status():
    return jsonify({
        "server": "Smart City AI Vision",
        "yolo_model": MODEL_PATH,
        "traffic_stream": YOUTUBE_URL,
        "timestamp": datetime.now().isoformat()
    })

if __name__ == '__main__':
    # Jalankan background thread
    t_traffic = threading.Thread(target=traffic_worker, daemon=True)
    t_traffic.start()

    t_waste = threading.Thread(target=waste_worker, daemon=True)
    t_waste.start()

    print("🚀 AI Vision Server berjalan di http://localhost:5000")
    print("   - Stream CCTV Jalan: http://localhost:5000/video_feed/traffic")
    print("   - Stream Deteksi Sampah: http://localhost:5000/video_feed/waste")
    app.run(host='0.0.0.0', port=5000, threaded=True, debug=False)
