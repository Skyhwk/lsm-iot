# LSM / Sound Meter (ESP32)

Firmware untuk **sound level meter** berbasis ESP32 (Gravity analog + OLED), dengan penyimpanan log di **kartu microSD**, konfigurasi **biner `config.bin`**, dan koneksi **WiFi + MQTT** ke backend Intilab.

---

## Ringkasan arsitektur

| Komponen | Peran |
|----------|--------|
| `src/main.cpp` | Entry point, task LCD / sensor / MQTT / waktu |
| `lib/config_manager` | Baca/tulis `DeviceConfig` → file `/config.bin` di SD |
| `lib/storage` | Inisialisasi SPI untuk microSD |
| `lib/storage_manager` | Log biner `/log.bin` |
| `lib/gravity_lsm` | ADC → dBA (kalibrasi trimpot) |
| `lib/lcd_manager` | OLED SSD1306 via I²C |
| `lib/wifi_manager` | STA (DHCP/static) |
| `lib/mqtt_manager` | Broker, subscribe perintah, publish data |
| `data/` | Aset portal web (SPIFFS); penggunaan di firmware dapat dikembangkan lebih lanjut |

Folder `test/main.cpp` adalah **varian monolitik terpisah** (SQLite, web server berbeda) dan **bukan** target build default PlatformIO (yang memakai `src/`).

---

## Pengembangan (toolchain)

### Prasyarat

- [PlatformIO](https://platformio.org/) (ekstensi VS Code atau CLI)
- Python 3 (untuk PlatformIO / alat bantu)
- USB driver untuk board ESP32 Anda

### Kloning & build

```bash
git clone <url-repo-ini> lsm-esp32
cd lsm-esp32
pio run
```

### Upload firmware

```bash
pio run -t upload
```

Port serial dapat ditentukan jika perlu, misalnya:

```bash
pio run -t upload --upload-port COM5
```

### Monitor serial

Baud rate default: **115200**.

```bash
pio device monitor -b 115200
```

### Partisi & filesystem

- Partisi aplikasi besar: `partitions/huge_app.csv`
- **SPIFFS** diaktifkan (`board_build.filesystem = spiffs`)

Upload image SPIFFS setelah mengubah isi folder `data/`:

```bash
pio run -t uploadfs
```

---

## Wiring (per kode firmware)

### MicroSD (SPI)

Pin didefinisikan di `lib/storage/sdcard.cpp`:

| Sinyal | GPIO ESP32 |
|--------|------------|
| CS | **5** |
| MOSI | **19** |
| MISO | **23** |
| SCK | **18** |

**Jangan** memakai pin-pin ini untuk modul lain; SD wajib terpasang dan ter-mount agar `config.bin`, `log.bin`, dan operasi penyimpanan berjalan.

### OLED SSD1306 (I²C)

| Sinyal | GPIO (default Arduino-ESP32 `Wire`) |
|--------|-------------------------------------|
| SDA | **21** |
| SCL | **22** |

Alamat I²C OLED: **0x3C** (lihat `lcd_manager.cpp`).

### Sensor suara Gravity + kalibrasi

Default di `GravityLSM` (`lib/gravity_lsm/gravity.h`):

| Fungsi | GPIO |
|--------|------|
| Keluaran analog sensor | **34** (ADC1) |
| Trimpot / kalibrasi | **35** (ADC1) |

Gunakan **GND dan 3,3 V** yang sesuai dengan modul; hindari tegangan 5 V langsung ke pin ADC.

---

## Konfigurasi perangkat

### File `config.bin` di kartu SD

Konfigurasi disimpan sebagai **struktur biner packed** `DeviceConfig` (ukuran tetap **404 byte**, diuji di `lib/config_manager/config_manager.h` dengan `static_assert`).

Isi utama:

- WiFi: `ssid`, `password`, `dhcp`, `ip`, `gateway`, `subnet`
- MQTT: `host`, `port`, `iddev`, `topic_subscribe`, `topic_publish`
- `offsetday` (offset zona waktu / NTP)
- Sesi pengukuran: `no_sample`, `shift`, `is_ready`

Jika `/config.bin` belum ada, firmware akan mencoba membuat **default** (lihat `ConfigManager::setDefaultIfInvalid()`).

### Menggunakan aplikasi **Binnary-Maker** (Python)

Untuk mengedit konfigurasi secara visual dari **profil JSON** dan menulis ulang **`config.bin`**, gunakan repositori:

**[https://github.com/Skyhwk/Binnary-Maker](https://github.com/Skyhwk/Binnary-Maker)**

Langkah umum (sesuai README upstream):

1. Clone repo tersebut dan install dependensi: `pip install -r requirements.txt`
2. Jalankan aplikasi desktop: `python -m app.main`
3. Pilih profil JSON yang **sesuai layout struct** `DeviceConfig` firmware ini (field dan urutan biner harus sama dengan `lib/config_manager/config_manager.h`)
4. Generate / export **`config.bin`**
5. Salin `config.bin` ke **akar kartu SD** (path: `/config.bin`), pasang kembali ke board, lalu reset ESP32

**Penting:** Profil di Binnary-Maker harus **kompatibel byte-per-byte** dengan struct C++ di firmware (termasuk `#pragma pack(1)`). Jika ukuran atau urutan field berbeda, firmware dapat gagal membaca konfigurasi. Selaraskan profil JSON / builder dengan definisi `DeviceConfig` di repo ini, atau sesuaikan salah satu sisi (firmware atau profil) secara eksplisit.

### Konfigurasi lewat MQTT

Setelah WiFi dan broker MQTT aktif, perintah JSON dapat mengubah `no_sample`, `is_ready`, menghentikan sesi, dll. (lihat `MQTTManager::handleCommandJson` di `lib/mqtt_manager/mqtt_manager.cpp`).

Perintah **`reset`** (sesuai implementasi) dapat menghapus `/config.bin` dan me-restart perangkat — gunakan hanya jika Anda ingin memaksa regenerasi konfigurasi default.

---

## Pengujian

1. **Serial monitor** — pastikan tidak ada loop error SD/SPIFFS; periksa log `[Config]`, `[MQTT]`, `[Session]`.
2. **SD** — pastikan `config.bin` terbaca; cek log pembuatan default jika file baru.
3. **WiFi** — LCD menampilkan status; IP tercetak di log/ LCD setelah terhubung.
4. **MQTT** — subscribe ke topik aksi perangkat; uji publish payload sesuai skema di kode.
5. **Sensor** — bandingkan dBA dengan referensi jika ada; sesuaikan trimpot pada pin kalibrasi.

---

## Struktur penting di kartu SD

| Path | Fungsi |
|------|--------|
| `/config.bin` | Konfigurasi perangkat (binary) |
| `/log.bin` | Log pengukuran (lihat `storage_manager`) |

File countdown sesi disimpan di **SPIFFS** (misalnya `remaining_seconds.txt`, `total_shift_seconds.txt` di root SPIFFS), bukan di SD.

---

## Lisensi / tim

Sesuaikan bagian ini dengan kebijakan repositori Anda.
