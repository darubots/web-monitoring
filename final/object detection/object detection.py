import cv2
from streamlink import Streamlink
from ultralytics import YOLO

# 1. Load model YOLOv8 'Small'
model = YOLO('yolov8s.pt')

# 2. Link YouTube Live CCTV
youtube_url = 'https://www.youtube.com/live/JJ3MWNYVCU4?si=00EZLlAVTAP0caTL'

print('Menghubungkan ke live stream CCTV YouTube via Streamlink...')

# 3. Ambil direct stream URL menggunakan Streamlink
try:
  session = Streamlink()
  streams = session.streams(youtube_url)
  if 'best' in streams:
    stream_url = streams['best'].to_url()
  else:
    print('Stream kualitas terbaik tidak ditemukan!')
    exit()
except Exception as e:
  print(f'Gagal mengekstrak live stream: {e}')
  exit()

print('Berhasil terhubung! Memulai deteksi objek dengan tampilan rapi...')

# 4. Buka stream menggunakan OpenCV
cap = cv2.VideoCapture(stream_url)

while cap.isOpened():
  success, frame = cap.read()
  if not success:
    print('Gagal membaca frame atau stream terputus.')
    break

  # Jalankan deteksi objek
  results = model(frame, classes=[0, 1, 2], conf=0.3, imgsz=1280)

  # 5. Visualisasikan hasil dengan teks & garis yang lebih kecil/tipis
  # - line_width=1 : Membuat garis kotak lebih tipis
  # - font_size=8  : Mengecilkan ukuran huruf agar tidak menumpuk
  annotated_frame = results[0].plot(line_width=1, font_size=8)

  # Tampilkan jendela CCTV
  cv2.imshow('CCTV Live Object Detection - Clean View', annotated_frame)

  # Tekan tombol 'q' untuk keluar
  if cv2.waitKey(1) & 0xFF == ord('q'):
    break

cap.release()
cv2.destroyAllWindows()