# Automatic Water Tank System

Sistem pemantauan dan kontrol otomatis untuk tangki air berbasis ESP32. Sistem ini memantau level air menggunakan sensor ultrasonik, mendeteksi aliran air menggunakan sensor water flow, dan terintegrasi secara real-time dengan Firebase.

![Gambar Sistem Tangki Air](image.png)

## Deskripsi Singkat
Sistem ini dirancang untuk secara cerdas mengontrol pengisian tangki air. Dengan membaca level air dan mendeteksi adanya aliran masuk, ESP32 dapat menyalakan pompa air pada jadwal tertentu atau mematikannya saat tangki sudah penuh. Jika pompa mendeteksi tidak ada aliran air dalam waktu tertentu (untuk mencegah pompa rusak/kering), sistem akan melakukan penghentian otomatis (auto-cutoff).

## Fitur Utama
- **Monitoring Level Air (Ultrasonik)**: Mengukur level jarak air dengan akurasi yang distabilkan (Moving Average Filter).
- **Monitoring Laju Aliran Air (Water Flow Sensor)**: Memantau debit (L/menit) dan total akumulasi volume air yang masuk.
- **IoT & Firebase**: Mengirim data level air, laju aliran air, volume, dan status pompa ke Firebase Realtime Database.
- **Jadwal Operasional Pompa**: Waktu aktif pompa bisa dijadwalkan dan diatur (jam mulai dan selesai) melalui sinkronisasi waktu NTP dan pengaturan Firebase.
- **LCD Display**: Menampilkan status koneksi, laju aliran (Flow), dan level air tangki (Tank %) secara langsung dengan LCD I2C (16x2).

## Hardware
- ESP32
- Sensor Ultrasonik (Trigger Pin: 13, Echo Pin: 12)
- Sensor Saluran Air / Flow Meter (Pin: 25)
- Relay (Relay Pompa: Pin 15, Relay Valve: Pin 23)
- LCD I2C 16x2

## Pengaturan Wi-Fi & Database
Sistem ini memerlukan konfigurasi nama Wi-Fi (SSID) dan kata sandinya, serta URL Host Firebase dan rahasia otentikasi (Auth/Token) untuk operasional IoT. Konfigurasi ini dapat disesuaikan di dalam file `.ino` utama.

<img src="PitchDeckFlowUp.png" alt="Gambar Sistem Tangki Air" width="500">
