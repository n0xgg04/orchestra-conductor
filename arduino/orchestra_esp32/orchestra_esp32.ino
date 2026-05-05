#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==================== CONFIG ====================
const char* WIFI_SSID = "P501";
const char* WIFI_PASS = "12341234a";

// HiveMQ Cloud Connection Details
const char* MQTT_SERVER = "c1f31b6b585e4edb91d3796d03de6b6a.s1.eu.hivemq.cloud";
const int   MQTT_PORT = 8883;  // TLS port
const char* MQTT_CLIENT_ID = "esp32-orchestra";

// Uncomment and fill these if your HiveMQ cluster requires authentication
// const char* MQTT_USER = "your_username";
// const char* MQTT_PASS = "your_password";

#define TOPIC_COMMAND    "orchestra/conductor/command"
#define TOPIC_SYNC       "orchestra/conductor/sync"
#define TOPIC_SCORE_WILD "orchestra/inst/+/score"
#define TOPIC_STATUS     "orchestra/esp/status"

// ==================== STRUCTS ====================
struct Note {
  uint32_t time;
  uint16_t freq;
  uint16_t duration;
};

struct Instrument {
  int id;
  int pin;
  Note notes[50];
  int noteCount;
  int nextNoteIdx;
  unsigned long noteOffTime;
  bool isNotePlaying;
};

// ==================== GLOBALS ====================
WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

Instrument instruments[4];
int numInstruments = 0;

bool isPlaying = false;
unsigned long globalPosition = 0;
unsigned long lastSyncLocalTime = 0;

unsigned long lastReconnectAttempt = 0;
unsigned long lastStatusPublish = 0;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ORCHESTRA ESP32 BOOT ---");

  connectWiFi();
  wifiClient.setInsecure();  // Skip certificate verification for testing
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(4096);
}

void loop() {
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }

  unsigned long now = millis();
  if (mqtt.connected() && now - lastStatusPublish > 5000) {
    lastStatusPublish = now;
    publishStatus();
  }

  updatePlayback();
}

// ==================== WIFI ====================
void connectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WIFI] Connected, IP: ");
  Serial.println(WiFi.localIP());
}

// ==================== MQTT ====================
bool reconnectMQTT() {
  Serial.print("[MQTT] Attempting connection...");
  // Use authenticated connect if credentials are defined
  #if defined(MQTT_USER) && defined(MQTT_PASS)
    bool connected = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
  #else
    bool connected = mqtt.connect(MQTT_CLIENT_ID);
  #endif
  if (connected) {
    Serial.println(" connected");
    mqtt.subscribe(TOPIC_COMMAND, 1);
    mqtt.subscribe(TOPIC_SYNC, 1);
    mqtt.subscribe(TOPIC_SCORE_WILD, 1);
    publishStatus();
  } else {
    Serial.print(" failed, rc=");
    Serial.println(mqtt.state());
  }
  return mqtt.connected();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[4096];
  unsigned int copyLen = length < 4095 ? length : 4095;
  memcpy(message, payload, copyLen);
  message[copyLen] = '\0';

  Serial.print("[MQTT] Topic: ");
  Serial.println(topic);

  if (strcmp(topic, TOPIC_COMMAND) == 0) {
    handleCommand(message);
  }
  else if (strcmp(topic, TOPIC_SYNC) == 0) {
    handleSync(message);
  }
  else if (strncmp(topic, "orchestra/inst/", 15) == 0 && strstr(topic, "/score")) {
    handleScore(topic, message);
  }
}

void publishStatus() {
  StaticJsonDocument<256> doc;
  doc["clientId"] = MQTT_CLIENT_ID;
  doc["status"] = "online";
  doc["instruments"] = numInstruments;
  doc["isPlaying"] = isPlaying;
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf, n);
}

void publishAck(int instrumentId, const char* status) {
  char topic[64];
  snprintf(topic, sizeof(topic), "orchestra/inst/%d/ack", instrumentId);
  StaticJsonDocument<256> doc;
  doc["instrumentId"] = instrumentId;
  doc["status"] = status;
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(topic, buf, n);
}

// ==================== COMMAND HANDLERS ====================
void handleCommand(const char* message) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, message);
  if (err) return;

  const char* type = doc["type"];
  if (!type) return;

  unsigned long now = millis();

  if (strcmp(type, "play") == 0) {
    globalPosition = doc["position"] | 0;
    lastSyncLocalTime = now;
    isPlaying = true;
    resetNotesToPosition(globalPosition);
    Serial.printf("[CMD] PLAY at %lu ms\n", globalPosition);
  }
  else if (strcmp(type, "pause") == 0) {
    if (isPlaying) {
      globalPosition += (now - lastSyncLocalTime);
      isPlaying = false;
    }
    stopAllNotes();
    Serial.printf("[CMD] PAUSE at %lu ms\n", globalPosition);
  }
  else if (strcmp(type, "seek") == 0) {
    globalPosition = doc["position"] | 0;
    lastSyncLocalTime = now;
    resetNotesToPosition(globalPosition);
    stopAllNotes();
    Serial.printf("[CMD] SEEK to %lu ms\n", globalPosition);
  }
  else if (strcmp(type, "stop") == 0) {
    isPlaying = false;
    globalPosition = 0;
    stopAllNotes();
    resetNotesToPosition(0);
    Serial.println("[CMD] STOP");
  }
}

void handleSync(const char* message) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, message);
  if (err) return;

  unsigned long serverPos = doc["position"];
  bool serverPlaying = doc["isPlaying"];
  unsigned long sentAt = doc["sentAt"];

  globalPosition = serverPos;
  lastSyncLocalTime = millis();

  if (!serverPlaying && isPlaying) {
    isPlaying = false;
    stopAllNotes();
  }
}

void handleScore(const char* topic, const char* message) {
  int id = atoi(topic + 15);

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, message);
  if (err) {
    Serial.print("[SCORE] Parse error: ");
    Serial.println(err.c_str());
    publishAck(id, "error_parse");
    return;
  }

  int idx = findInstrumentIndex(id);
  if (idx < 0) {
    if (numInstruments >= 4) {
      publishAck(id, "error_full");
      return;
    }
    idx = numInstruments++;
    instruments[idx].id = id;
    instruments[idx].pin = doc["pin"] | getDefaultPin(id);
    instruments[idx].noteCount = 0;
    pinMode(instruments[idx].pin, OUTPUT);
    digitalWrite(instruments[idx].pin, LOW);
  }

  Instrument &inst = instruments[idx];
  inst.noteCount = 0;
  inst.nextNoteIdx = 0;
  inst.isNotePlaying = false;

  JsonArray arr = doc["notes"].as<JsonArray>();
  int count = 0;
  for (JsonObject noteObj : arr) {
    if (count >= 50) break;
    inst.notes[count].time = noteObj["t"];
    inst.notes[count].freq = noteObj["f"];
    inst.notes[count].duration = noteObj["d"];
    count++;
  }
  inst.noteCount = count;

  Serial.printf("[SCORE] Inst#%d loaded %d notes on pin %d\n", id, count, inst.pin);
  publishAck(id, "ok");
}

// ==================== PLAYBACK ====================
void updatePlayback() {
  unsigned long now = millis();

  for (int i = 0; i < numInstruments; i++) {
    Instrument &inst = instruments[i];
    if (inst.isNotePlaying && now >= inst.noteOffTime) {
      if (inst.id == 4) {
        digitalWrite(inst.pin, LOW);
      } else {
        noTone(inst.pin);
      }
      inst.isNotePlaying = false;
    }
  }

  if (!isPlaying) return;

  unsigned long currentPos = globalPosition + (now - lastSyncLocalTime);

  for (int i = 0; i < numInstruments; i++) {
    Instrument &inst = instruments[i];
    while (inst.nextNoteIdx < inst.noteCount) {
      Note &n = inst.notes[inst.nextNoteIdx];
      if (n.time <= currentPos) {
        playNote(inst, n, now);
        inst.nextNoteIdx++;
      } else {
        break;
      }
    }
  }
}

void playNote(Instrument &inst, Note &n, unsigned long now) {
  if (inst.id == 4) {
    digitalWrite(inst.pin, HIGH);
    inst.noteOffTime = now + n.duration;
    inst.isNotePlaying = true;
  } else {
    tone(inst.pin, n.freq);
    inst.noteOffTime = now + n.duration;
    inst.isNotePlaying = true;
  }
}

void stopAllNotes() {
  for (int i = 0; i < numInstruments; i++) {
    Instrument &inst = instruments[i];
    if (inst.id == 4) {
      digitalWrite(inst.pin, LOW);
    } else {
      noTone(inst.pin);
    }
    inst.isNotePlaying = false;
  }
}

void resetNotesToPosition(unsigned long pos) {
  for (int i = 0; i < numInstruments; i++) {
    int idx = 0;
    while (idx < instruments[i].noteCount && instruments[i].notes[idx].time < pos) {
      idx++;
    }
    instruments[i].nextNoteIdx = idx;
  }
}

// ==================== HELPERS ====================
int findInstrumentIndex(int id) {
  for (int i = 0; i < numInstruments; i++) {
    if (instruments[i].id == id) return i;
  }
  return -1;
}

int getDefaultPin(int id) {
  switch (id) {
    case 1: return 2;
    case 2: return 16;
    case 3: return 19;
    case 4: return 23;
    default: return 2;
  }
}
