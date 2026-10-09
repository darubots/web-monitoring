/**
 * =========================================================================
 * WEB SERIAL API SERVICE
 * Menghubungkan Browser Langsung ke Arduino Uno via Kabel USB
 * =========================================================================
 */

class ArduinoSerialService {
  constructor() {
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.readableStreamClosed = null;
    this.writableStreamClosed = null;
    this.isConnected = false;
    this.isConnecting = false;
    this.baudRate = 9600;
    this.lineBuffer = "";
    this.isSimulationMode = false;
    this.simulationTimer = null;

    // Callbacks
    this.onDataCallbacks = [];
    this.onLogCallbacks = [];
    this.onStatusChangeCallbacks = [];

    // Auto-detect browser Web Serial support
    this.isSupported = 'serial' in navigator;
  }

  /**
   * Daftarkan listener saat ada data sensor baru diterima
   */
  onData(callback) {
    this.onDataCallbacks.push(callback);
  }

  /**
   * Daftarkan listener untuk log serial mentah
   */
  onLog(callback) {
    this.onLogCallbacks.push(callback);
  }

  /**
   * Daftarkan listener untuk status koneksi (connected / disconnected)
   */
  onStatusChange(callback) {
    this.onStatusChangeCallbacks.push(callback);
  }

  emitStatus(status, message = "") {
    this.onStatusChangeCallbacks.forEach(cb => {
      try { cb(status, message); } catch (e) { console.error(e); }
    });
  }

  emitLog(direction, message) {
    const timestamp = new Date().toLocaleTimeString('id-ID', { hour12: false });
    this.onLogCallbacks.forEach(cb => {
      try { cb({ direction, message, timestamp }); } catch (e) { console.error(e); }
    });
  }

  emitData(parsedData, raw) {
    this.onDataCallbacks.forEach(cb => {
      try { cb(parsedData, raw); } catch (e) { console.error(e); }
    });
  }

  /**
   * Membuka dialog pilihan COM Port dan menghubungkan ke Arduino
   */
  async connect() {
    if (!this.isSupported) {
      const errMsg = "Web Serial API tidak didukung di browser ini. Gunakan Google Chrome, Microsoft Edge, atau Opera.";
      this.emitLog('SYS', errMsg);
      this.emitStatus('error', errMsg);
      alert(errMsg);
      return false;
    }

    try {
      this.isConnecting = true;
      this.emitStatus('connecting', 'Memilih port USB Arduino...');

      // Meminta izin pengguna untuk memilih COM Port Arduino Uno
      this.port = await navigator.serial.requestPort();

      // Buka port dengan baud rate 9600
      await this.port.open({ baudRate: this.baudRate });

      this.isConnected = true;
      this.isConnecting = false;
      this.emitStatus('connected', `Terhubung ke Arduino (9600 Baud)`);
      this.emitLog('SYS', `Berhasil terhubung ke Serial Port @ ${this.baudRate} bps`);

      // Mulai proses membaca stream
      this.readSerialLoop();
      return true;
    } catch (err) {
      this.isConnecting = false;
      this.isConnected = false;
      this.emitStatus('disconnected', err.message || 'Koneksi dibatalkan');
      this.emitLog('SYS', `Gagal membuka serial: ${err.message}`);
      return false;
    }
  }

  /**
   * Loop membaca data byte-by-byte dan membentuk baris utuh
   */
  async readSerialLoop() {
    while (this.port && this.port.readable && this.isConnected) {
      const textDecoder = new TextDecoderStream();
      this.readableStreamClosed = this.port.readable.pipeTo(textDecoder.writable);
      this.reader = textDecoder.readable.getReader();

      try {
        while (true) {
          const { value, done } = await this.reader.read();
          if (done) {
            // Stream ditutup
            break;
          }
          if (value) {
            this.handleIncomingChunk(value);
          }
        }
      } catch (err) {
        console.warn('Error saat membaca serial stream:', err);
        this.emitLog('SYS', `Serial read error: ${err.message}`);
      } finally {
        if (this.reader) {
          this.reader.releaseLock();
          this.reader = null;
        }
      }
    }

    // Jika loop selesai, berarti terputus
    if (this.isConnected) {
      await this.disconnect();
    }
  }

  /**
   * Menggabungkan chunk teks menjadi baris per baris
   */
  handleIncomingChunk(chunk) {
    this.lineBuffer += chunk;
    const lines = this.lineBuffer.split(/\r?\n/);
    // Simpan bagian terakhir yang belum lengkap (jika belum ada newline)
    this.lineBuffer = lines.pop();

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      this.emitLog('IN', trimmed);

      // Coba parse sebagai JSON dari Arduino
      if (trimmed.startsWith('{') && trimmed.endsWith('}')) {
        try {
          const parsed = JSON.parse(trimmed);
          this.emitData(parsed, trimmed);
        } catch (e) {
          console.warn('Gagal parse JSON dari serial:', trimmed, e);
        }
      } else if (trimmed === "SYSTEM_READY") {
        this.emitLog('SYS', "Arduino mengkonfirmasi: SYSTEM_READY");
      }
    }
  }

  /**
   * Mengirimkan perintah ke Arduino melalui USB
   * Contoh: sendCommand("SERVO1:90") atau sendCommand("PUMP:ON")
   */
  async sendCommand(cmd) {
    if (!this.isConnected || !this.port || !this.port.writable) {
      if (this.isSimulationMode) {
        this.emitLog('OUT', `[SIMULASI] ${cmd}`);
        if (cmd === 'BUZZER:1' || cmd === 'BUZZER:ON') this.simBuzzer = 1;
        else if (cmd === 'BUZZER:0' || cmd === 'BUZZER:OFF') this.simBuzzer = 0;
        else if (cmd === 'LED:1' || cmd === 'LED:ON') this.simLed = 1;
        else if (cmd === 'LED:0' || cmd === 'LED:OFF') this.simLed = 0;
        return true;
      }
      this.emitLog('SYS', 'Gagal kirim: Arduino belum terhubung.');
      return false;
    }

    try {
      const encoder = new TextEncoder();
      const writer = this.port.writable.getWriter();
      await writer.write(encoder.encode(cmd + "\n"));
      writer.releaseLock();
      this.emitLog('OUT', cmd);
      return true;
    } catch (err) {
      this.emitLog('SYS', `Gagal kirim perintah: ${err.message}`);
      return false;
    }
  }

  /**
   * Memutuskan koneksi USB
   */
  async disconnect() {
    this.isConnected = false;
    this.stopSimulation();

    if (this.reader) {
      try {
        await this.reader.cancel();
      } catch (e) {}
      this.reader = null;
    }

    if (this.port) {
      try {
        await this.port.close();
      } catch (e) {}
      this.port = null;
    }

    this.emitStatus('disconnected', 'Koneksi serial terputus');
    this.emitLog('SYS', 'Koneksi Arduino diputus.');
  }

  /**
   * Mode Simulasi untuk Demo saat Arduino fisik belum dicolok
   */
  startSimulation() {
    this.isSimulationMode = true;
    this.emitStatus('connected', 'Mode Simulasi Aktif (Demo)');
    this.emitLog('SYS', 'Memulai stream sensor simulasi...');

    if (this.simulationTimer) clearInterval(this.simulationTimer);

    let simWater = 450;
    let simVolt = 4.65; // Dummy 4 - 5 V
    let simAmp = 2.45;  // Dummy 2 - 3 A
    let simTds = 145;
    let simNtc = 525;   // ~28.5C
    let simMq = 120;    // AQI ~42 (Pin A1)
    let simFire = 0;
    let simTilt = 0;
    let simPump = 0;
    let simBuzzer = 0;
    let simLed = 0;
    let simServoOrganik = 0;
    let simServoAnorganik = 0;

    this.simBuzzer = simBuzzer;
    this.simLed = simLed;

    this.simulationTimer = setInterval(() => {
      // Fluktuasi halus yang realistis
      simWater = Math.max(100, Math.min(800, simWater + (Math.floor(Math.random() * 9) - 4)));
      simVolt = parseFloat(Math.max(4.00, Math.min(5.00, simVolt + (Math.random() * 0.1 - 0.05))).toFixed(2));
      simAmp = parseFloat(Math.max(2.00, Math.min(3.00, simAmp + (Math.random() * 0.08 - 0.04))).toFixed(2));
      const simPower = parseFloat((simVolt * simAmp).toFixed(2));
      simTds = Math.max(80, Math.min(250, simTds + (Math.floor(Math.random() * 5) - 2)));
      simNtc = Math.max(480, Math.min(580, simNtc + (Math.floor(Math.random() * 5) - 2)));
      simMq = Math.max(70, Math.min(180, simMq + (Math.floor(Math.random() * 7) - 3)));
      simPump = simWater > 500 ? 1 : 0;

      const simData = {
        water: simWater,
        mq: simMq,
        tds: simTds,
        ntc: simNtc,
        volt: simVolt,
        ampere: simAmp,
        power: simPower,
        fire: simFire,
        tilt: simTilt,
        pump: simPump,
        buzzer: this.simBuzzer || 0,
        led: this.simLed || 0,
        servo_organik: simServoOrganik,
        servo_anorganik: simServoAnorganik
      };

      const jsonStr = JSON.stringify(simData);
      this.emitLog('IN', jsonStr);
      this.emitData(simData, jsonStr);
    }, 500);
  }

  stopSimulation() {
    if (this.simulationTimer) {
      clearInterval(this.simulationTimer);
      this.simulationTimer = null;
    }
    this.isSimulationMode = false;
  }
}

// Inisialisasi instance singleton global
window.arduinoSerial = new ArduinoSerialService();
