# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**child-detector** — an ESP32-S3 IoT device for child/parent monitoring. It captures images, supports chat via a custom binary WebSocket protocol, and displays a touch UI via LVGL. The project uses ESP-IDF v5.2+ with CMake.

- **Target chip**: ESP32S3 with 16MB flash, 8MB Octal PSRAM
- **Display**: 320×240 ST7789 TFT via SPI + FT5x06 capacitive touch via I2C
- **Camera**: GC0308 (YUV422, QVGA)
- **Audio**: ES8311 DAC + ES7210 ADC via I2S
- **File system**: LittleFS on `storage` partition
- **Connectivity**: WiFi with BLE provisioning (Security 2), mDNS, custom WebSocket protocol

## Build & Flash Commands

```sh
# Build
idf.py build

# Flash (UART, the default)
idf.py flash

# Flash + monitor
idf.py flash monitor

# Full clean build
idf.py fullclean build

# Erase eFuse emulation region (for provisioning reset)
esptool.py -p /dev/ttyACM0 erase_region 0x12000 0x2000

# Build with unit tests (CMake option)
idf.py -DENABLE_UNIT_TESTS=ON build
```

The project uses ESP-IDF's component manager. Dependencies are locked in `dependencies.lock` and installed under `managed_components/`.

## Partition Layout

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data | 0x9000 | 24KB |
| otadata | ota | 0xf000 | 8KB |
| phy_init | phy | 0x11000 | 4KB |
| efuse_em | efuse | 0x12000 | 8KB |
| ota_0 | app | 0x20000 | 4MB |
| ota_1 | app | 0x420000 | 4MB |
| storage | littlefs | 0x820000 | 3MB |

Custom partition table (`partitions.csv`) enables OTA with dual app slots.

## Architecture

### Bootstrap Flow (`main/main.c:app_main`)

1. Initialize I2C bus, PCA9557 GPIO expander, LVGL display + touch
2. Mount LittleFS, init NVS, derive device key from eFuse UUID
3. Show splash screen, detect boot mode (normal vs OTA by long-pressing GPIO 0)
4. **Normal mode**: Init WiFi provisioning (BLE), wait for WiFi, sync SNTP, init mDNS, init camera + face detector, start S3 uploader task, init HTTP helper, init global WebSocket, init image queue, start monitor task, push main page
5. **OTA mode**: Release BT classic memory, push OTA page directly

### Key Subsystems

**UI Navigation** (`components/UI/Core/gs_nav.*`)
- Simple stack-based page navigation with `gs_nav_push`/`gs_nav_pop`
- Each page is a `gs_page_desc_t` with `init_cb`, `render_cb`, `deinit_cb`
- Pages: splash, main, chat, menu, cam, todo, OTA

**Communication Protocol** (`components/UI/Utils/protocol.*`)
- Custom binary framing: STX (0x02), CRC8, typed frames over WebSocket
- Frame types: MODE_SWITCH, CMD, DATA, REASONING, ACK, HISTORY_REQ, SYN, END, NOTIFY, SESSION_RANDOM, SESSION_READY
- Streams: CONTROL (0x00), CHAT (0x01), REASONING (0x02)
- AES-based encryption session management derived from server random challenge + device key

**Network Layer** (`components/UI/Utils/net_adapter.*`)
- WebSocket client connecting to server (host:port from mDNS resolution + NVS device/parent IDs)
- Auto-reconnect with status callbacks

**Image Upload Pipeline** (`main/utils/img_queue.*`, `main/utils/s3_helper.*`)
- Priority-based image upload queue with retry
- Images sent via HTTP PUT to S3-compatible storage
- Two job types: MONITOR (automatic, low priority) and MANUAL (user-triggered, high priority)

**Camera** (`main/cam_helper.*`, `main/utils/face_detector_helper.*`)
- GC0308 camera initialized in YUV422 QVGA mode
- Face detection via esp-dl component
- Captured frames uploaded through the image pipeline

**Task Manager** (`main/utils/task_manager.*`)
- Fetches task list from server, tracks active task, start/complete lifecycle

**Monitor** (`main/utils/monitor_mamager.*`)
- Background task that periodically captures and uploads images for monitoring

**OTA** (`main/Pages/page_ota.*`, `main/utils/ota_backend.*`)
- BLE-based OTA using `espressif/ble_ota` component
- Firmware images are encrypted (`esp_encrypted_img`) during build
- Progress displayed on LVGL UI

**Device Identity** (`main/utils/efuse_helper.*`, `main/utils/aes_crypto_helper.*`)
- Device UUID read from eFuse (virtual eFuse in debug, real in production)
- Device key derived via HKDF from UUID + fixed info string, stored in NVS
- Platform binding via `esp_secure_cert_mgr`

### Board Support Package (`main/esp32_s3_szp.*`)

All hardware pin definitions and driver initialization for the specific board:
- I2C bus on GPIO 1/2 with PCA9557 GPIO expander at 0x19
- ST7789 LCD on SPI3 (MOSI 40, CLK 41, DC 39, backlight PWM 42)
- Camera on DVP bus (XCLK 5, D0-D7 on pins 16,18,8,17,15,6,4,9)
- Audio on I2S1 (MCLK 38, BCLK 14, WS 13, DOUT 45, SDIN 12)
- PCA9557 controls: LCD_CS, PA_EN, DVP_PWDN

### Component Tree

- `main/` — Application entry, BSP, pages (UI screens), utility modules
- `components/UI/` — Custom UI framework (gs_nav navigation, chat components, protocol, network adapter, chat service)
- `components/wifi_prov/` — WiFi BLE provisioning with Security 2, QR code generation
- `components/AHEasing/` — Robert Penner easing functions for animations
- `components/esp-dl/` — Espressif deep learning (face detection models)
- `components/testable/` — Example test component; `TEST_COMPONENTS` list in root CMakeLists

### Kconfig (`main/Kconfig.projbuild`)

- `HTTP_MODE`: mDNS vs HTTPS for server location
- `SERVER_HOSTNAME` (default: `youth-test`), `SERVER_PORT` (default: 3000)
- `HTTPS_URL`, `S3_PUT_URL_PREFIX` for HTTPS/S3 modes

### Debug Mode

`IS_DEBUG_MODE` is set to `1` in `main/context.h`. When enabled, `app_main` initializes test NVS values (device_id=3, parent_id=1), prints memory info, and prints NVS info. Time/memory profiling macros are in `main/utils/time_test_helper.h`.

## Unit Testing

Tests run on-device via the Unity framework. The `ENABLE_UNIT_TESTS` CMake option switches `main/main.c` → `main/unit_test_main.c`. Test code lives in the `testable` component, discovered by `TEST_COMPONENTS` in the root `CMakeLists.txt`. Run with:

```sh
idf.py -DENABLE_UNIT_TESTS=ON build flash monitor
```
