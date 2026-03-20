#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_BROKER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;

// CHANGE THIS PREFIX
const char* TOPIC_TELEMETRY = "smartclass/demo01/telemetry";
const char* TOPIC_COMMAND   = "smartclass/demo01/command";
const char* TOPIC_STATUS    = "smartclass/demo01/status";

const int PIR_PIN    = 14;
const int DHT_PIN    = 15;
const int LIGHT_PIN  = 26;
const int FAN_PIN    = 27;
const int BUZZER_PIN = 25;

#define DHTTYPE DHT22
DHT dhtSensor(DHT_PIN, DHTTYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool autoMode = true;
bool manualLight = false;
bool manualFan = false;
bool forceOff = false;
bool afterHoursAlert = false;

bool occupied = false;
unsigned long lastMotionMs = 0;
unsigned long lastPublishMs = 0;

float tempThreshold = 28.0;

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void publishStatus(const char* text) {
  mqttClient.publish(TOPIC_STATUS, text);
}

void applyLogic(float tempC, bool motion) {
  if (motion) {
    lastMotionMs = millis();
  }

  occupied = (millis() - lastMotionMs < 20000);

  bool lightState = false;
  bool fanState = false;

  if (forceOff) {
    lightState = false;
    fanState = false;
  } else if (autoMode) {
    lightState = occupied;
    fanState = (!isnan(tempC) && tempC > tempThreshold);
  } else {
    lightState = manualLight;
    fanState = manualFan;
  }

  digitalWrite(LIGHT_PIN, lightState ? HIGH : LOW);
  digitalWrite(FAN_PIN, fanState ? HIGH : LOW);

  if (afterHoursAlert && motion) {
    tone(BUZZER_PIN, 1000);
  } else {
    noTone(BUZZER_PIN);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.println("Invalid command JSON");
    return;
  }

  if (doc.containsKey("mode")) {
    String mode = doc["mode"].as<String>();
    autoMode = (mode == "auto");
  }

  if (doc.containsKey("light")) {
    manualLight = doc["light"];
  }

  if (doc.containsKey("fan")) {
    manualFan = doc["fan"];
  }

  if (doc.containsKey("forceOff")) {
    forceOff = doc["forceOff"];
  }

  if (doc.containsKey("afterHoursAlert")) {
    afterHoursAlert = doc["afterHoursAlert"];
  }

  if (doc.containsKey("tempThreshold")) {
    tempThreshold = doc["tempThreshold"];
  }

  publishStatus("Command received");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "smartclass-esp32-" + String(random(1000, 9999));
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT connected");
      mqttClient.subscribe(TOPIC_COMMAND);
      publishStatus("ESP32 connected");
    } else {
      delay(1000);
    }
  }
}

void publishTelemetry(float tempC, bool motion) {
  StaticJsonDocument<256> doc;

  doc["temperature"] = isnan(tempC) ? -999 : tempC;
  doc["motion"] = motion;
  doc["occupied"] = occupied;
  doc["light"] = digitalRead(LIGHT_PIN);
  doc["fan"] = digitalRead(FAN_PIN);
  doc["mode"] = autoMode ? "auto" : "manual";
  doc["forceOff"] = forceOff;
  doc["afterHoursAlert"] = afterHoursAlert;
  doc["tempThreshold"] = tempThreshold;

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(TOPIC_TELEMETRY, buffer);

  Serial.println(buffer);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LIGHT_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  noTone(BUZZER_PIN);

  dhtSensor.begin();

  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  float tempC = dhtSensor.readTemperature();
  bool motion = digitalRead(PIR_PIN);

  applyLogic(tempC, motion);

  if (millis() - lastPublishMs > 2000) {
    publishTelemetry(tempC, motion);
    lastPublishMs = millis();
  }

  delay(200);
}