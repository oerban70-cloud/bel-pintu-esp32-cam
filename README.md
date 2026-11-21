# Bel Pintu ESP32-CAM — Tombol + Buzzer + LED + Kamera

Tamu mengetuk pintu tapi kamu sedang di lantai atas? Satu tombol di depan
pintu memberitahumu siapa yang datang, lengkap dengan fotonya. Tamu menekan
tombol, buzzer berbunyi tiga kali, LED menyala, lampu kilat menyala sesaat,
kamera ESP32-CAM memotret, dan fotonya terkirim ke Telegram di HP-mu.

Video Short (60 detik): https://www.youtube.com/shorts/ZwakbU1PMlU

## Cara kerja

1. Tombol bel di depan pintu ditekan (GPIO13, INPUT_PULLUP, aktif LOW).
   ESP32 membaca tombol tiap 100 ms dengan debounce 50 ms.
2. Begitu tombol stabil ditekan, buzzer berbunyi tiga kali (250 ms bunyi,
   150 ms diam), LED hijau menyala, dan lampu kilat (flash LED GPIO4
   bawaan board) menyala sesaat.
3. Kamera OV2640 mengambil satu foto JPEG 800x600 (kualitas 12).
   Kalau tidak ada PSRAM, ukuran turun otomatis ke 320x240.
4. Foto dikirim ke Telegram beserta pesan "Ada tamu di depan pintu!"
   (bila `USE_TELEGRAM` 1 dan WiFi tersambung). Tanpa Telegram, foto
   tetap diambil dan ukurannya dilaporkan di Serial Monitor.
5. Cooldown 3 detik mencegah foto ganda bila tombol ditekan berulang
   atau bergetar. LED mati setelah foto terkirim.

## Wiring

| Komponen | Pin modul | ESP32-CAM |
| --- | --- | --- |
| Tombol bel kaki 2 | 2 | GPIO13 |
| Tombol bel kaki 1 | 1 | GND |
| Buzzer aktif HIGH (+) | + | GPIO14 |
| Buzzer (-) | - | GND |
| LED hijau (seri 220 Ω) | Anoda | GPIO2 |
| LED hijau (seri 220 Ω) | Katoda | GND |
| Flash LED (lampu kilat) | — | GPIO4 (sudah ada di board) |
| Adaptor USB 5V 2A | 5V / GND | pin 5V / GND |
| Kamera OV2640 | — | pin tetap di board AI Thinker (XCLK 0, D0–D7 5/18/19/21/36/39/34/35, VSYNC 25, HREF 23, PCLK 22, SDA 26, SCL 27, PWDN 32) |

Skema lengkap (gaya Fritzing): lihat `schematic.png`.

## Konfigurasi

Di `BelPintu/BelPintu.ino`:

- `USE_TELEGRAM` (1) — 1 = kirim foto + pesan ke Telegram, 0 = tanpa Telegram.
  Isi `WIFI_SSID`, `WIFI_PASS`, `BOT_TOKEN`, dan `CHAT_ID` bila Telegram aktif.
- `JUMLAH_BEEP` (3) / `BEEP_NYALA_MS` (250) / `BEEP_MATI_MS` (150) — pola bunyi bel.
- `BACA_INTERVAL_MS` (100) / `DEBOUNCE_MS` (50) — baca tombol + debounce.
- `COOLDOWN_MS` (3000) — jeda antar foto.
- `UKURAN_FOTO` (SVGA 800x600) / `KUALITAS_JPEG` (12) — ukuran dan kualitas foto.

## Komponen

- ESP32-CAM AI Thinker (kamera OV2640, PSRAM)
- Tombol tekan (push button)
- Buzzer aktif HIGH (5V)
- LED hijau + resistor 220 Ω
- Adaptor USB 5V 2A + kabel USB

## Compile (terverifikasi)

```
arduino-cli compile --fqbn esp32:esp32:esp32cam BelPintu
Sketch uses 1173287 bytes (37%) of program storage space. Maximum is 3145728 bytes.
Global variables use 62452 bytes (19%) of dynamic memory, leaving 265228 bytes
for local variables. Maximum is 327680 bytes.
```

Terverifikasi 21 November 2026, ESP32 core 4.0.0, arduino-cli 1.6.0.

## Library

Folder `libraries/` berisi ZIP library yang dipakai (pasang lewat
Sketch > Include Library > Add .ZIP Library bila belum ada):

- UniversalTelegramBot 1.3.0 (hanya bila `USE_TELEGRAM` 1)
- ArduinoJson 6.21.5 (hanya bila `USE_TELEGRAM` 1)
- esp_camera — sudah termasuk di ESP32 core 4.0.0, tanpa install

## Lisensi

MIT — lihat `LICENSE`.
