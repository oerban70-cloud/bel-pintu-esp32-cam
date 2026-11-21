// Bel Pintu ESP32-CAM — tombol ditekan, kamera memotret, foto terkirim
// ke Telegram
//
// Cara kerja:
//  - Tamu menekan tombol di depan pintu (GPIO13, INPUT_PULLUP, aktif LOW).
//  - ESP32-CAM (AI Thinker) langsung: buzzer berbunyi 3 kali, LED
//    menyala, lampu kilat (flash LED GPIO4 bawaan board) menyala sesaat,
//    kamera mengambil satu foto JPEG.
//  - Foto dikirim ke Telegram (bila USE_TELEGRAM 1) beserta pesan teks
//    "Ada tamu di depan pintu!". Tanpa Telegram, foto tetap diambil dan
//    ukurannya dilaporkan di Serial Monitor.
//  - Cooldown 3 detik mencegah foto ganda bila tombol ditekan berulang
//    atau bergetar.
//
// Wiring (board AI Thinker ESP32-CAM):
//  - Tombol bel       : satu kaki -> GPIO13, kaki lain -> GND
//    (INPUT_PULLUP, aktif LOW saat ditekan)
//  - Buzzer aktif HIGH: + -> GPIO14, - -> GND
//  - LED              : anoda -> GPIO2 (seri resistor 220 ohm ke GND)
//  - Flash LED        : GPIO4 (sudah ada di board, dipakai sebagai
//    lampu kilat saat memotret)
//  - Kamera OV2640    : pin data/kontrol sudah tetap di board
//    (XCLK 0, D0-D7 5/18/19/21/36/39/34/35, VSYNC 25, HREF 23,
//    PCLK 22, SDA 26, SCL 27, PWDN 32) — jangan diubah.
//  - Catu             : 5V 2A ke pin 5V/GND board (USB serial saat
//    upload sketch, lalu catu 5V langsung).
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - esp_camera (sudah termasuk di ESP32 core 4.0.0, tanpa install)
//  - UniversalTelegramBot 1.3.0 + ArduinoJson 6.21.5
//    (hanya bila USE_TELEGRAM 1).
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32cam BelPintu
// (terverifikasi 21 Nov 2026: lihat README.md untuk angka pastinya,
//  ESP32 core 4.0.0, arduino-cli 1.6.0)

#include "esp_camera.h"

// ---------- PIN KAMERA (AI Thinker ESP32-CAM, tetap) ----------
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

// ---------- KONFIGURASI ----------
#define USE_TELEGRAM 1  // 1 = kirim foto + pesan ke Telegram, 0 = tanpa Telegram

#define PIN_TOMBOL 13  // tombol bel (INPUT_PULLUP, aktif LOW)
#define PIN_BUZZER 14  // buzzer aktif HIGH
#define PIN_LED 2      // LED tamu datang (seri resistor 220 ohm)
#define PIN_FLASH 4    // lampu kilat bawaan board ESP32-CAM

#define BACA_INTERVAL_MS 100  // baca tombol tiap 100 ms
#define DEBOUNCE_MS 50        // tombol harus stabil selama ini
#define BEEP_NYALA_MS 250     // buzzer bunyi 250 ms ...
#define BEEP_MATI_MS 150      // ... lalu diam 150 ms (pola 3x bip)
#define JUMLAH_BEEP 3         // buzzer berbunyi 3 kali per tamu
#define COOLDOWN_MS 3000      // jeda antar foto (hindari foto ganda)
#define KUALITAS_JPEG 12      // kualitas JPEG (10 = bagus, 63 = kecil)
#define UKURAN_FOTO FRAMESIZE_SVGA  // foto 800x600 piksel

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";

// Status tombol (sudah debounce) dan foto yang sedang dikirim.
bool tombolDulu = HIGH;            // status tombol loop sebelumnya
bool bacaanMentah = HIGH;          // bacaan pin terakhir (belum debounce)
unsigned long stabilSejak = 0;     // millis() bacaan mentah terakhir berubah
unsigned long bacaTerakhir = 0;    // millis() baca tombol terakhir
unsigned long fotoTerakhir = 0;    // millis() foto terakhir diambil
camera_fb_t *fbKirim = nullptr;    // frame buffer foto yang sedang dikirim
size_t posisiKirim = 0;            // byte foto berikutnya yang dikirim

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);

void kirimTelegram(const String &pesan) {
  if (WiFi.status() != WL_CONNECTED) return;
  bot.sendMessage(CHAT_ID, pesan, "");
}

void sambungWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) {
    delay(250);
  }
}
#else
void kirimTelegram(const String &) {}
void sambungWiFi() {}
#endif

// ---------- Callback pengiriman foto (byte demi byte) ----------
// Dipakai UniversalTelegramBot::sendPhotoByBinary: library memanggil
// ketiganya berulang sampai fotoMasihAda() false.
bool fotoMasihAda() {
  return fbKirim != nullptr && posisiKirim < fbKirim->len;
}

byte fotoByteBerikutnya() {
  return fbKirim->buf[posisiKirim++];
}

// ---------- Kamera ----------
bool kameraMulai() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = UKURAN_FOTO;
  config.jpeg_quality = KUALITAS_JPEG;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  if (psramFound()) {
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  esp_err_t hasil = esp_camera_init(&config);
  if (hasil != ESP_OK) {
    Serial.printf("Kamera gagal mulai, kode %d\n", hasil);
    return false;
  }
  return true;
}

// ---------- Bunyi bel ----------
// Buzzer berbunyi JUMLAH_BEEP kali (blocking sebentar, sengaja: bel
// harus terdengar penuh sebelum foto dikirim).
void bunyiBel() {
  for (int i = 0; i < JUMLAH_BEEP; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(BEEP_NYALA_MS);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < JUMLAH_BEEP - 1) delay(BEEP_MATI_MS);
  }
}

// ---------- Satu kejadian tamu ----------
// Tombol ditekan: bel bunyi, LED + lampu kilat nyala, foto diambil,
// foto dikirim ke Telegram (bila aktif), lalu LED mati.
void tamuDatang() {
  fotoTerakhir = millis();
  Serial.println("Bel ditekan, tamu datang!");
  digitalWrite(PIN_LED, HIGH);
  bunyiBel();
  digitalWrite(PIN_FLASH, HIGH);
  camera_fb_t *fb = esp_camera_fb_get();
  digitalWrite(PIN_FLASH, LOW);
  if (fb == nullptr) {
    Serial.println("Gagal mengambil foto.");
    digitalWrite(PIN_LED, LOW);
    return;
  }
  Serial.printf("Foto diambil: %u byte.\n", fb->len);
  sambungWiFi();
#if USE_TELEGRAM
  if (WiFi.status() == WL_CONNECTED) {
    fbKirim = fb;
    posisiKirim = 0;
    String hasil = bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", fb->len,
                                         fotoMasihAda, fotoByteBerikutnya,
                                         nullptr, nullptr);
    fbKirim = nullptr;
    Serial.print("Foto terkirim ke Telegram: ");
    Serial.println(hasil);
    kirimTelegram("Ada tamu di depan pintu!");
  } else {
    Serial.println("WiFi tidak tersambung, foto tidak dikirim.");
  }
#endif
  esp_camera_fb_return(fb);
  digitalWrite(PIN_LED, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TOMBOL, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_FLASH, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_FLASH, LOW);
  if (!kameraMulai()) {
    Serial.println("Periksa kabel kamera, lalu reset board.");
  }
  sambungWiFi();
  Serial.println("Bel pintu ESP32-CAM siap.");
}

void loop() {
  unsigned long sekarang = millis();
  if (sekarang - bacaTerakhir < BACA_INTERVAL_MS) return;
  bacaTerakhir = sekarang;

  bool baca = digitalRead(PIN_TOMBOL);
  if (baca != bacaanMentah) {
    bacaanMentah = baca;
    stabilSejak = sekarang;
  }
  // Tombol dianggap ditekan hanya bila stabil LOW selama DEBOUNCE_MS.
  if (sekarang - stabilSejak < DEBOUNCE_MS) return;
  if (bacaanMentah == LOW && tombolDulu == HIGH) {
    if (sekarang - fotoTerakhir >= COOLDOWN_MS) {
      tamuDatang();
    }
  }
  tombolDulu = bacaanMentah;
}
