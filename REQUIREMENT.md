Hiện tại, tôi đang có ESP32 có kết nối tới các Buzzer

Dự án cần làm liên quan tới hệ thống phân tán. Cần thiết kế như sau:

- 1 máy chủ để điều phối và điều khiển các thiết bị:
- 1 esp kết nối tới máy chủ, lắng nghe điều khiển

Mỗi buzzer có vai trò như 1 loại nhạc cụ, lắng nghe từ nhạc trưởng (máy chủ)

Máy chủ sẽ cho phát 1 loại file cấu hình âm thanh, ví dụ:
trong giây đầu, các loại nhạc cụ sẽ phát gì.

Ví dụ:
-------------00:00 --- 00:01 --- 00:02 --- 00:03
Nhạc cụ 1: Đồ Mi Rê Đồ
Nhạc cụ 2: Rê Rê Si Son
Nhạc cụ 3: Rê Đồ La Si

Server có thể tua bài nhạc tới vị trí nào đó.
Các loa và esp cần nghe và đảm bảo đồng bộ bật các loại nhạc cụ tới thời gian đó.

Khi server ấn dừng, tất cả nhạc cụ dừng, ấn tiếp tục, tất cả tiếp tục
(Sử dụng MQTT, cần có confirm đảm bảo đồng bộ, bật chuẩn xác)

Ở mạch, mỗi loa cũnh có thể bị tắt, đảm bảo 1 loại nhạc cụ bị mất thì các nhạc cụ khác vẫn hoạt động bình thường, tưc là nhạc vẫn bạt, chỉ là thiếu nhạc cụ thôi
