/**
 * =========================================================================
 * FIREBASE CONFIGURATION
 * =========================================================================
 * Silakan ganti nilai di bawah ini dengan konfigurasi Firebase milik Anda
 * yang didapat dari Firebase Console:
 * Project Settings -> General -> Your apps -> Web app (</>) -> SDK setup and configuration.
 *
 * Catatan:
 * Jika Anda belum mengisi konfigurasi di file ini, Anda juga bisa
 * memasukkannya langsung melalui tombol "⚙️ Konfigurasi Firebase" di website!
 * =========================================================================
 */

window.FIREBASE_CONFIG = {
  apiKey: "AIzaSy_YOUR_API_KEY_HERE",
  authDomain: "YOUR_PROJECT_ID.firebaseapp.com",
  databaseURL: "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com",
  projectId: "YOUR_PROJECT_ID",
  storageBucket: "YOUR_PROJECT_ID.appspot.com",
  messagingSenderId: "YOUR_MESSAGING_SENDER_ID",
  appId: "1:YOUR_APP_ID:web:YOUR_HASH"
};

/**
 * Mendapatkan konfigurasi Firebase yang aktif
 * Mengutamakan konfigurasi yang disimpan di LocalStorage browser (jika ada),
 * lalu fallback ke konfigurasi default di atas.
 */
function getActiveFirebaseConfig() {
  try {
    const saved = localStorage.getItem('smart_city_firebase_config');
    if (saved) {
      const parsed = JSON.parse(saved);
      if (parsed.apiKey && !parsed.apiKey.includes('YOUR_API_KEY')) {
        return parsed;
      }
    }
  } catch (e) {
    console.warn('Gagal membaca Firebase config dari localStorage:', e);
  }
  return window.FIREBASE_CONFIG;
}

/**
 * Cek apakah konfigurasi Firebase valid (bukan placeholder)
 */
function isFirebaseConfigured(config = getActiveFirebaseConfig()) {
  return (
    config &&
    config.apiKey &&
    !config.apiKey.includes('YOUR_API_KEY') &&
    config.databaseURL &&
    !config.databaseURL.includes('YOUR_PROJECT_ID')
  );
}
