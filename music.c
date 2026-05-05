#include <WiFi.h>
#include <WiFiUdp.h>

// --- CẤU HÌNH WIFI ---
const char* ssid = "P501";
const char* password = "12341234a";

// --- CẤU HÌNH PIN ---
const int PIN_GUITAR = 2;
const int PIN_BASS   = 16;
const int PIN_OBOE   = 19;
const int PIN_DRUMS  = 23;

const int PINS[] = {PIN_GUITAR, PIN_BASS, PIN_OBOE, PIN_DRUMS};

// --- CẤU HÌNH UDP ---
WiFiUDP udp;
unsigned int localPort = 4210;
byte packetBuffer[4]; // [ID, Pitch, Duration_H, Duration_L]

// Biến hỗ trợ kiểm tra trạng thái WiFi trong loop
unsigned long lastWifiCheck = 0;

void setup() {
  Serial.begin(115200);
  delay(100); // Đợi Serial ổn định
  
  Serial.println("\n--- BẮT ĐẦU KHỞI ĐỘNG ---");

  // Khởi tạo các chân Output
  for(int i=0; i<4; i++) {
    pinMode(PINS[i], OUTPUT);
    digitalWrite(PINS[i], LOW);
  }

  // Kết nối WiFi
  Serial.print("[WIFI] Đang kết nối tới mạng: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Log xác nhận đã kết nối thành công
  Serial.println("\n[WIFI] KẾT NỐI THÀNH CÔNG!");
  Serial.print("[WIFI] Địa chỉ IP: ");
  Serial.println(WiFi.localIP());

  // Bắt đầu lắng nghe UDP
  udp.begin(localPort);
  Serial.printf("[UDP] Đang lắng nghe trên cổng %d\n", localPort);
  Serial.println("-------------------------");
}

void loop() {
  // --- THÊM LOG KIỂM TRA TRẠNG THÁI WIFI MỖI 5 GIÂY ---
  if (millis() - lastWifiCheck >= 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      // Bạn có thể bỏ comment dòng dưới nếu muốn nó báo "Đang kết nối" liên tục mỗi 5s
      // Serial.println("[STATUS] WiFi vẫn đang kết nối tốt."); 
    } else {
      Serial.println("[CẢNH BÁO] Đã mất kết nối WiFi! Đang thử kết nối lại...");
      // Tùy chọn: Thêm lệnh WiFi.reconnect(); ở đây nếu cần thiết
    }
  }

  // --- XỬ LÝ GÓI TIN UDP ---
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udp.read(packetBuffer, 4);
    
    int instrId = packetBuffer[0];
    int pitch   = packetBuffer[1];
    int duration = (packetBuffer[2] << 😎 | packetBuffer[3];

    if (instrId >= 0 && instrId < 4) {
      Serial.printf("Nhận lệnh -> Nhạc cụ: %d | Pitch: %d | Thời gian: %dms\n", instrId, pitch, duration);
      
      int targetPin = PINS[instrId];
      
      // Chuyển đổi MIDI pitch sang tần số (Hz)
      float frequency = 440.0 * pow(2.0, (pitch - 69.0) / 12.0);
      
      if (instrId == 3) { // Drums - Tiếng click đơn giản
        digitalWrite(targetPin, HIGH);
        delay(20);
        digitalWrite(targetPin, LOW);
      } else {
        // Sử dụng hàm tone của ESP32 (Cần thư viện ESP32-Arduino bản mới hoặc ESP32Servo)
        tone(targetPin, (unsigned int)frequency, duration);
      }
    }
  }
}