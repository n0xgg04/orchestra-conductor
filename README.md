# Orchestra Conductor - Hệ thống Điều khiển Nhạc cụ Phân tán

> Đồ án môn **Hệ thống Phân tán** — Server điều phối nhiều thiết bị ESP32 phát nhạc đồng bộ qua giao thức MQTT.

---

## 1. Tổng quan

Hệ thống mô phỏng một **dàn nhạc giao hưởng phân tán**:
- **Nhạc trưởng (Server)**: Điều phối bài nhạc, phát/lùi/tua, đảm bảo đồng bộ.
- **Nhạc công (ESP32)**: Mỗi buzzer kết nối với ESP32 đóng vai trò một nhạc cụ (guitar, bass, oboe, drums).
- **Nhịp điệu (Timeline)**: Server lưu file cấu hình bài nhạc, xác định từng nhạc cụ phát gì tại thởi điểm nào.

Các nhạc cụ hoạt động độc lập qua **MQTT topics**, đảm bảo nếu một nhạc cụ bị tắt/mất kết nối, các nhạc cụ khác vẫn tiếp tục phát.

---

## 2. Kiến trúc Hệ thống

```
·------------------------·         ·------------------------·
|   Frontend (Browser)   |  HTTP   |   Server (Node.js)     |
|  - Play/Pause/Seek/Stop|<-------->|  - REST API            |
|  - Upload Score        |         |  - MQTT Client         |
|  - Real-time Status    |  WS     |  - Socket.IO           |
·------------------------·         ·------------|-----------·
                                                |
                                                | MQTTs (TLS)
                                                |
                                        ·-------|--------·
                                        |  HiveMQ Cloud   |
                                        |  MQTT Broker    |
                                        ·-------|--------·
                                                |
                          ----------------------+----------------------
                          |                      |                      |
                   ·------|------·        ·------|------·        ·------|------·
                   | Virtual ESP   |        |  ESP32 #1   |        |  ESP32 #2   |
                   | (Simulation)  |        |  (Physical) |        |  (Physical) |
                   | - 4 instruments|        | - 2 instruments|     | - 2 instruments|
                   ·---------------·        ·---------------·        ·---------------·
```

### Thành phần

| Thành phần | Công nghệ | Vai trò |
|-----------|-----------|---------|
| **Server** | Node.js + Express + MQTT.js | Điều phối, API, MQTT Publisher |
| **Frontend** | HTML5 + Vanilla JS + Socket.IO | UI điều khiển |
| **MQTT Broker** | HiveMQ Cloud (Free) | Message bus trung tâm |
| **ESP32** | Arduino (C++) + PubSubClient | Nhận lệnh, phát nhạc đồng bộ |
| **Virtual ESP** | Node.js + MQTT.js | Mô phỏng ESP khi không có phần cứng |

---

## 3. Yêu cầu Hệ thống

### Phần mềm
- **Node.js** >= 16.x
- **npm** >= 8.x
- **Arduino IDE** >= 2.0 (cho ESP32)
- **Tài khoản HiveMQ Cloud** (miễn phí)

### Phần cứng (tùy chọn)
- 1x ESP32 DevKit
- 4x Buzzer / Speaker passive
- Dây nối

> **Lưu ý**: Nếu không có phần cứng, có thể dùng **Virtual ESP** (`server/virtual-esp.js`) để mô phỏng toàn bộ luồng MQTT.

### Thư viện Arduino
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon

---

## 4. Cài đặt và Chạy

### 4.1. Server (Backend)

```bash
cd server
npm install

# Tạo file môi trường
cp .env.example .env

# Sửa .env với thông tin HiveMQ Cloud của bạn:
# MQTT_HOST=c1f31b6b585e4edb91d3796d03de6b6a.s1.eu.hivemq.cloud
# MQTT_PORT=8883
# MQTT_USER=your_username
# MQTT_PASS=your_password
# HTTP_PORT=3000

npm start
```

Server sẽ chạy tại: **http://localhost:3000**

### 4.2. Frontend

Frontend là static HTML được serve trực tiếp từ Express. Mở browser:

```
http://localhost:3000
```

### 4.3. Virtual ESP (Mô phỏng)

```bash
cd server
node virtual-esp.js
```

Virtual ESP sẽ kết nối HiveMQ Cloud, subscribe các topic, và log phản hồi ra console.

### 4.4. ESP32 vật lý (Arduino IDE)

1. Mở `arduino/orchestra_esp32/orchestra_esp32.ino`
2. Cập nhật thông tin WiFi:
   ```cpp
   const char* WIFI_SSID = "YourWiFi";
   const char* WIFI_PASS = "YourPassword";
   ```
3. Cập nhật MQTT (đã có sẵn host HiveMQ):
   ```cpp
   const char* MQTT_SERVER = "c1f31b6b585e4edb91d3796d03de6b6a.s1.eu.hivemq.cloud";
   const int   MQTT_PORT = 8883;
   ```
4. Nếu cluster bật authentication, uncomment và điền:
   ```cpp
   const char* MQTT_USER = "your_username";
   const char* MQTT_PASS = "your_password";
   ```
5. Chọn board **ESP32 Dev Module**, nạp code.
6. Mở Serial Monitor (115200 baud) để xem log.

---

## 5. Giao thức MQTT

### 5.1. Topic Structure

| Topic | Kiểu | Mô tả |
|-------|------|-------|
| `orchestra/conductor/command` | Broadcast | Server gửi lệnh Play/Pause/Seek/Stop |
| `orchestra/conductor/sync` | Broadcast | Server gửi vị trí phát hiện tại mỗi 500ms |
| `orchestra/inst/{id}/score` | Unicast | Server gửi bản nhạc cho từng instrument |
| `orchestra/inst/{id}/ack` | Unicast | ESP xác nhận đã nhận score |
| `orchestra/esp/status` | Broadcast | ESP gửi heartbeat mỗi 5 giây |

### 5.2. Message Format

**Command (Play/Pause/Seek/Stop)**
```json
{
  "type": "play",
  "serverTime": 1714900000000,
  "position": 0
}
```

**Sync (Time Synchronization)**
```json
{
  "position": 1250,
  "isPlaying": true
}
```

**Score (Per-Instrument)**
```json
{
  "id": 1,
  "name": "guitar",
  "pin": 2,
  "notes": [
    { "t": 0, "f": 262, "d": 200 },
    { "t": 1000, "f": 330, "d": 200 }
  ]
}
```

**ACK**
```json
{
  "instrumentId": 1,
  "status": "ok"
}
```

---

## 6. Các Vấn đề Hệ thống Phân tán được Giải quyết

### 6.1. Clock Synchronization (Đồng bộ hóa đồng hồ)
- Server broadcast `sync` message mỗi **500ms** với vị trí phát hiện tại.
- ESP/Virtal ESP tự điều chỉnh `globalPosition` theo server clock, tránh drift.

### 6.2. Decoupled Communication (Giao tiếp phi tập trung)
- Server không biết IP của ESP. Chỉ cần publish đúng topic.
- ESP subscribe wildcard `orchestra/inst/+/score` để nhận score động.

### 6.3. Independent Addressing (Địa chỉ hóa độc lập)
- Mỗi instrument có topic riêng `orchestra/inst/{id}/score`.
- Có thể tắt/mở từng nhạc cụ riêng lẻ mà không ảnh hưởng node khác.

### 6.4. Reliable Delivery (Phân phối tin cậy)
- Command và Score được publish với **QoS 1** (at least once).
- ESP gửi **ACK** (`orchestra/inst/{id}/ack`) xác nhận đã nhận score.

### 6.5. State Consistency (Nhất quán trạng thái)
- Play/Pause/Seek/Stop là **atomic broadcast** tới tất cả node.
- Khi Seek, tất cả node reset timeline và phát từ vị trí mới.

### 6.6. Fault Tolerance (Chịu lỗi - Thiết kế)
- Kiến trúc cho phép **1 instrument chết, các instrument khác chạy tiếp**.
- Mỗi node độc lập, không phụ thuộc vào node khác để phát nhạc.

---

## 7. Hạn chế và Giới hạn

| Hạn chế | Giải thích |
|---------|-----------|
| **1 ESP vật lý** | Hiện tại chỉ có 1 ESP32 vật lý mô phỏng 4 logical node. Nếu ESP mất kết nối, cả 4 nhạc cụ cùng chết. Đây là giới hạn phần cứng, không phải giới hạn protocol. |
| **WiFiClientSecure.setInsecure()** | ESP32 bỏ qua xác thực chứng chỉ TLS để đơn giản hóa demo. Production cần load CA certificate thật. |
| **Max 50 notes** | ESP32 giới hạn 50 notes/instrument để tiết kiệm RAM. Có thể tăng nếu dùng ESP32-S3 hoặc PSRAM. |
| **JSON trên Arduino** | Dùng `ArduinoJson` với buffer cố định. Score quá lớn có thể gây parse error. |

---

## 8. Hướng Phát triển

- [ ] **Scale nhiều ESP32 vật lý**: Mỗi ESP điều khiển 1-2 nhạc cụ, chạy thật sự phân tán.
- [ ] **NTP Clock Sync**: Thay vì dùng server-relative time, dùng NTP để đồng bộ thời gian tuyệt đối.
- [ ] **TLS Certificate**: Load CA certificate HiveMQ vào ESP32, bỏ `setInsecure()`.
- [ ] **Offline Buffer**: ESP cache score vào SPIFFS/SD card để chơi offline khi mất WiFi.
- [ ] **Web-based Score Editor**: Cho phép vẽ timeline trực tiếp trên FE thay vì paste JSON.

---

## 9. Cấu trúc Thư mục

```
.
├── server/
│   ├── package.json          # Dependencies
│   ├── server.js             # Main server + MQTT client
│   ├── virtual-esp.js        # Virtual ESP simulator
│   ├── score-example.json    # Sample music score
│   └── .env.example          # Environment template
├── frontend/
│   └── index.html            # Conductor UI
├── arduino/
│   └── orchestra_esp32/
│       └── orchestra_esp32.ino   # ESP32 firmware
├── REQUIREMENT.md          # Original requirements
└── README.md               # This file
```

---

## 10. Tài liệu Tham khảo

- [HiveMQ Cloud Documentation](https://www.hivemq.com/docs/hivemq-cloud/)
- [MQTT Specification v3.1.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
- [PubSubClient Arduino Library](https://github.com/knolleary/pubsubclient)
- [ArduinoJson Library](https://arduinojson.org/)

---

> **Ghi chú**: Dự án được phát triển cho mục đích học tập môn Hệ thống Phân tán. Protocol và kiến trúc được thiết kế để dễ dàng scale từ 1 ESP mô phỏng lên nhiều node vật lý thực sự.
