#include <Arduino.h>

#include <MAX3010x.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include "filters.h"

/* WiFi Configuration */
const char* ssid = "F";
const char* password = "kampret2018";

String sensor_state = "idle";

/* Server Configuration */
AsyncWebServer server(80);
String processor(const String& var){
  if(var == "STATE"){
    return sensor_state;
  }
  return String();
}

/* CSV Configuration */
File csv;

float data_sensor[10001]; 
uint32_t data_time[10001]; 
// float data_sensor2[10000]; 
uint16_t pointer;

/* MAX30105 Configuration */
MAX30105::MultiLedConfiguration cfg = {
  MAX30105::SLOT_RED, 
  MAX30105::SLOT_IR, 
  MAX30105::SLOT_GREEN, 
  MAX30105::SLOT_PILOT_IR
};
MAX30105 sensor;
const auto kSamplingRate = sensor.SAMPLING_RATE_1600SPS;
const float kSamplingFrequency = 400.0;
const float kLowPassCutoff = 3.0;
const float kHighPassCutoff = 0.7;
HighPassFilter high_pass_filter(kHighPassCutoff, kSamplingFrequency);
LowPassFilter low_pass_filter(kLowPassCutoff, kSamplingFrequency);
Differentiator differentiator(kSamplingFrequency);

void delete_csv(String path) {
  SPIFFS.remove(path);
}

void write_csv(String data) {
  csv = SPIFFS.open("/sinyal.csv", FILE_APPEND);
  if (!csv) {
    Serial.println("[SCV] Error when opening the csv");
    return;
  }
  csv.println(data);
  csv.close();
}

AsyncWebSocket ws("/ws");
void notifyClients(String data) {
  ws.textAll(data);
}
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "r") == 0) {
      sensor_state = "PEMBACAAN";
    }
  }
}
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}
void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void read_sensor() {
  auto sample = sensor.readSample(1000);
  float raw_data = sample.ir;
  raw_data = low_pass_filter.process(raw_data);
  raw_data = high_pass_filter.process(raw_data);
  raw_data = differentiator.process(raw_data);

  // if (pointer < 10000) {
    data_sensor[pointer] = raw_data;
  // }

  pointer++;
  // Serial.println(current_diff);
  // write_csv((String)time + ";" + (String)current_diff + ";" + (String)sample.slot[0] + ";" + (String)sample.slot[1] + ";" + (String)sample.slot[2] + ";" + (String)sample.slot[3]);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  if(sensor.begin() 
  && sensor.setSamplingRate(kSamplingRate)) { 
    sensor.setMultiLedConfiguration(cfg);
    sensor.setMode(MAX30105::MODE_MULTI_LED);
    Serial.println("Slot1,Slot2,Slot3,Slot4");
  } else {
    Serial.println("Sensor not found");  
    while(1);
  }  

  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  // Print local IP address and start web server
  Serial.println("[WiFi] Connected");
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());

  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    sensor_state = "PEMBACAAN";
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/sinyal.csv", "text/csv", true);
  });
  // Start server
  server.begin();

}

void loop() {

  if (sensor_state == "PEMBACAAN") {
    differentiator.reset();
    low_pass_filter.reset();
    high_pass_filter.reset();
    digitalWrite(BUILTIN_LED, HIGH);
    delete_csv("/sinyal.csv");
    long current_time = millis();
    notifyClients("123");
    while(millis() - current_time <= 1000*60*2) {
      
      auto sample = sensor.readSample(1000);
      float raw_data = sample.ir;
      raw_data = low_pass_filter.process(raw_data);
      raw_data = high_pass_filter.process(raw_data);
      raw_data = differentiator.process(raw_data);

      data_time[pointer] = millis() - current_time;
      data_sensor[pointer] = raw_data;

      pointer++;
      if (pointer > 10000) {
        break;
      // pointer++;
      // } else {
       
      }

    }
    Serial.print("pointer: " + (String)pointer);
    notifyClients("i");
    for (size_t i = 0; i < 20230; i++)
    {
      write_csv((String)data_time[i] + ";" + (String)data_sensor[i]);
    }
    
    Serial.print("Pembacaan Selesai");
    Serial.println();
    sensor_state = "idle";
    digitalWrite(BUILTIN_LED, LOW);
  }
  ws.cleanupClients();
}