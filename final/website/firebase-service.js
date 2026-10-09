/**
 * =========================================================================
 * FIREBASE REALTIME DATABASE SERVICE
 * Sinkronisasi Data Sensor Arduino ke Cloud Firebase Realtime Database
 * =========================================================================
 */

class FirebaseMonitoringService {
  constructor() {
    this.app = null;
    this.db = null;
    this.isInitialized = false;
    this.isListening = false;
    this.status = 'unconfigured'; // 'unconfigured' | 'connecting' | 'connected' | 'error'
    this.onStatusChangeCallbacks = [];
    this.onRemoteDataCallbacks = [];
    this.lastPushTime = 0;
    this.minPushInterval = 1000; // Minimal interval 1 detik untuk efisiensi kuota Firebase
  }

  onStatusChange(callback) {
    this.onStatusChangeCallbacks.push(callback);
  }

  onRemoteData(callback) {
    this.onRemoteDataCallbacks.push(callback);
  }

  emitStatus(status, details = "") {
    this.status = status;
    this.onStatusChangeCallbacks.forEach(cb => {
      try { cb(status, details); } catch (e) { console.error(e); }
    });
  }

  /**
   * Inisialisasi Firebase dengan konfigurasi aktif
   */
  async init(customConfig = null) {
    const config = customConfig || (typeof getActiveFirebaseConfig === 'function' ? getActiveFirebaseConfig() : window.FIREBASE_CONFIG);

    if (!config || !isFirebaseConfigured(config)) {
      this.emitStatus('unconfigured', 'Konfigurasi Firebase belum diisi');
      return false;
    }

    try {
      this.emitStatus('connecting', 'Menghubungkan ke Firebase...');

      // Cek apakah library Firebase SDK sudah terload
      if (typeof firebase === 'undefined') {
        throw new Error('Firebase SDK belum termuat di browser. Pastikan terhubung internet.');
      }

      // Hapus app yang ada jika re-inisialisasi
      if (firebase.apps && firebase.apps.length > 0) {
        await Promise.all(firebase.apps.map(app => app.delete()));
      }

      this.app = firebase.initializeApp(config);
      this.db = firebase.database();
      this.isInitialized = true;

      // Pantau status koneksi Firebase Realtime Database (.info/connected)
      const connectedRef = this.db.ref('.info/connected');
      connectedRef.on('value', (snap) => {
        if (snap.val() === true) {
          this.emitStatus('connected', 'Terhubung ke Firebase Realtime DB');
        } else {
          this.emitStatus('connecting', 'Menghubungkan ke server Firebase...');
        }
      });

      // Dengarkan perubahan realtime dari cloud jika ada perangkat lain yang mengupdate
      this.startListening();

      // Dengarkan perintah remote dari Firebase (contoh: kontrol pompa/servo dari jarak jauh)
      this.listenToRemoteCommands();

      return true;
    } catch (err) {
      console.error('Firebase Init Error:', err);
      this.emitStatus('error', err.message);
      return false;
    }
  }

  /**
   * Mengirimkan data sensor terbaru ke Firebase
   * Menulis ke /monitoring/realtime (state saat ini)
   * dan menambah catatan ke /monitoring/logs (riwayat time-series)
   */
  async pushTelemetry(sensorData) {
    if (!this.isInitialized || !this.db) {
      return false;
    }

    const now = Date.now();
    if (now - this.lastPushTime < this.minPushInterval) {
      return false;
    }
    this.lastPushTime = now;

    const payload = {
      ...sensorData,
      timestamp: now,
      isoTime: new Date(now).toISOString(),
      updatedBy: 'Arduino_Uno_Gateway_Laptop'
    };

    try {
      // 1. Simpan kondisi realtime terbaru (Overwrite)
      await this.db.ref('monitoring/realtime').set(payload);

      // 2. Tambahkan ke history log berkala (Push record baru)
      // Simpan log setiap 5 detik agar database tidak terlalu membengkak
      if (!this.lastLogPushTime || now - this.lastLogPushTime >= 5000) {
        this.lastLogPushTime = now;
        await this.db.ref('monitoring/logs').push(payload);
      }

      return true;
    } catch (err) {
      console.warn('Gagal push data ke Firebase:', err);
      return false;
    }
  }

  /**
   * Mengirimkan event deteksi sampah ke Firebase
   */
  async pushWasteEvent(wasteData) {
    if (!this.isInitialized || !this.db) return false;
    try {
      const payload = {
        ...wasteData,
        timestamp: Date.now(),
        isoTime: new Date().toISOString()
      };
      await this.db.ref('monitoring/waste_latest').set(payload);
      await this.db.ref('monitoring/waste_events').push(payload);
      return true;
    } catch (e) {
      console.warn('Gagal push waste event ke Firebase:', e);
      return false;
    }
  }

  /**
   * Mengirimkan statistik lalu lintas CCTV ke Firebase
   */
  async pushTrafficStats(trafficData) {
    if (!this.isInitialized || !this.db) return false;
    try {
      await this.db.ref('monitoring/traffic').set({
        ...trafficData,
        updatedAt: Date.now()
      });
      return true;
    } catch (e) {
      return false;
    }
  }

  /**
   * Berlangganan data /monitoring/realtime untuk mode viewer
   * (Misal membuka website dari hp/laptop lain saat gateway berjalan)
   */
  startListening() {
    if (!this.isInitialized || !this.db || this.isListening) return;

    this.isListening = true;
    const realtimeRef = this.db.ref('monitoring/realtime');
    realtimeRef.on('value', (snapshot) => {
      const val = snapshot.val();
      if (val) {
        this.onRemoteDataCallbacks.forEach(cb => {
          try { cb(val); } catch (e) { console.error(e); }
        });
      }
    });
  }

  /**
   * Mendengarkan perintah remote dari Firebase path /monitoring/commands
   */
  listenToRemoteCommands() {
    if (!this.isInitialized || !this.db) return;

    const cmdRef = this.db.ref('monitoring/commands');
    cmdRef.limitToLast(1).on('child_added', (snapshot) => {
      const cmdData = snapshot.val();
      if (cmdData && !cmdData.executed && window.arduinoSerial && window.arduinoSerial.isConnected) {
        if (cmdData.command) {
          window.arduinoSerial.sendCommand(cmdData.command);
          // Tandai perintah telah dieksekusi
          snapshot.ref.update({ executed: true, executedAt: Date.now() });
        }
      }
    });
  }

  /**
   * Mengirim perintah remote ke Firebase (misal dari dashboard web)
   */
  async sendRemoteCommand(cmdString) {
    if (!this.isInitialized || !this.db) return false;
    try {
      await this.db.ref('monitoring/commands').push({
        command: cmdString,
        executed: false,
        createdAt: Date.now()
      });
      return true;
    } catch (e) {
      console.error(e);
      return false;
    }
  }
}

// Inisialisasi instance singleton global
window.firebaseMonitoring = new FirebaseMonitoringService();
