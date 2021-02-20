#include <WiFi.h>
#include <WiFiClient.h>
// ----------------------------------- for OTA WEB update ----------------------------
//#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#define U_PART U_SPIFFS
AsyncWebServer webserver(80);
// ----------------------------------- Dallas Temperature ----------------------------
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONEWIRE_PIN  19                   // pin for OneWire DS18S20, DS18B20, DS1822
OneWire oneWire(ONEWIRE_PIN);             // объект для работы с библиотекой OneWire
DallasTemperature dallassensor(&oneWire); // объект для работы с библиотекой DallasTemperature
// ----------------------------------- MQTT ------------------------------------------
#include <AsyncMqttClient.h>
AsyncMqttClient mqttClient;
// -----------------------------------------------------------------------------------
#include <EEPROM.h>
#define EEPROM_OFFSET   1
#include "SPIFFS.h"
#define MQTT_HOST IPAddress(192, 168, 22, 78)
#define MQTT_PORT 1883

const char device[] = "Pulse Counter 01";
const char shortboardname[] = "esp32";  // краткое наименование девайса
const char ver[] = "v0.3";              // Номер версии прошивки (как в git)
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;
char hostname[16];
char buildversion[45];                  // Переменная для полной версии в формате: hostname, ver, date and time compilation 
String buflog = String('\n');           // текстовый буфер для отладочного вывода в web
#define MAX_BUFLOG_SIZE 4000            // ограничение размера этого буфера.
unsigned long time_to_meashure = 0;     // (2^32 - 1) = 4,294,967,295
unsigned long time_to_reboot = 0;
unsigned long time_to_blink = 0;
int period_blink = 5;                   // Период в секундах мигания при нормальной работе 
int period_meashure = 60;               // Периодичность проведения измерений в секундах 
size_t content_len;
const char platform[] = ARDUINO_BOARD;
char internal_temperature[7];
char external_temperature[7];
struct config{
  char is_defined[2];
  char wifi_ssid[16];
  char wifi_pass[32];
  char mqtt_host[32];
  char mqtt_login[16];
  char mqtt_pass[16];
  char mqtt_topic[16]; 
};
config setting;
int qty_wifi_attempt = 0;

void multiBlinc(int qty) {
   for (int i = 0; i < qty + qty; i++) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(150);
   }
}

void log(const char *sss) {
   char tbuf[14];
   sprintf(tbuf, "%010lu  ", millis());
   Serial.print(tbuf);
   Serial.println(sss);
   buflog += tbuf + String(sss) + "<br>";
   if (buflog.length() > MAX_BUFLOG_SIZE) {
      buflog = buflog.substring(buflog.indexOf("\n", 100));
   }
}

String processor(const String& var){
  if(var == "STATE"){
    if(digitalRead(LED_BUILTIN)){
      return(String("ON"));
    }else{
      return(String("OFF"));
    }
  } else if (var == "HOSTNAME") {
    return(String(hostname));
  } else if (var == "FIRMWARE") {
    return(String(buildversion));
  } else if (var == "PLATFORM") {
    return(String(platform));
  } else if (var == "DEVICE") {
    return(String(device));
  } else if (var == "BUFLOG") {
    return(buflog);
  } else if (var == "INTERNAL_T") {
    return(String(internal_temperature));
  } else if (var == "EXTERNAL_T") {
    return(String(external_temperature));
  } else if (var == "RSSI") {
    return(String(WiFi.RSSI()));
  } else if (var == "WIFI_SSID") {
    return(String(setting.wifi_ssid));
  } else if (var == "MQTT_HOST") {
    return(String(setting.mqtt_host));
  } else if (var == "MQTT_TOPIC") {
    return(String(setting.mqtt_topic));
  } else if (var == "MQTT_LOGIN") {
    return(String(setting.mqtt_login));
  } else if (var == "MQTT_PASS") {
    return(String(setting.mqtt_pass));
  }
  return(String());
}

void handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index){
    Serial.println("Update");
    content_len = request->contentLength();
    // if filename includes spiffs, update the spiffs partition
    int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
    Serial.println(cmd);
    Serial.println(filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Update.printError(Serial);
    }
  }
  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }
  if (final) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
    response->addHeader("Refresh", "20");  
    response->addHeader("Location", "/");
    request->send(response);
    if (!Update.end(true)){
      Update.printError(Serial);
    } else {
      Serial.println("Update complete");
      Serial.flush();
      ESP.restart();
    }
  }
}

void handleWifiUpdate(AsyncWebServerRequest *request) {
  char newSsid[16];
  char newPass[32];
  AsyncWebParameter* p;
  p = request->getParam(0);
  snprintf(newSsid, sizeof(newSsid), "%s", p->value().c_str());
  p = request->getParam(1);
  snprintf(newPass, sizeof(newPass), "%s", p->value().c_str());
  if ((newSsid[0] == '\0') || (newPass[0] == '\0')) {
    log("Error! SSID and PASSWORD both are required!" );
  } else {
    snprintf(setting.wifi_ssid, sizeof(setting.wifi_ssid),"%s",newSsid);
    snprintf(setting.wifi_pass, sizeof(setting.wifi_pass),"%s",newPass);
    setting.is_defined[0] = 'Y';
    setting.is_defined[1] = '\0';
    EEPROM.put(EEPROM_OFFSET, setting);
    EEPROM.commit();
    log(setting.is_defined);
    log(setting.wifi_ssid);
    log(setting.wifi_pass);
    ESP.restart();
  }
}

void handleMqttUpdate(AsyncWebServerRequest *request) {
  char newVal[32];
  snprintf(newVal,32,"%s",request->getParam(0)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_host, sizeof(setting.mqtt_host),"%s",newVal);
  }
  snprintf(newVal,32,"%s",request->getParam(1)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_topic, sizeof(setting.mqtt_topic),"%s",newVal);
  }
  snprintf(newVal,32,"%s",request->getParam(2)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_login, sizeof(setting.mqtt_login),"%s",newVal);
  }
  snprintf(newVal,32,"%s",request->getParam(3)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_pass, sizeof(setting.mqtt_pass),"%s",newVal);
  }
  EEPROM.put(EEPROM_OFFSET, setting);
  EEPROM.commit();
  log(setting.mqtt_host);
  log(setting.mqtt_topic);
  log(setting.mqtt_login);
  log(setting.mqtt_pass);
  ESP.restart();
}

void webserver_start() {
  char buf[60];
  /*use mdns for host name resolution*/
  if (!MDNS.begin(hostname)) {                        
    log("Error setting up MDNS responder!");
  }
  snprintf(buf,sizeof(buf),"mDNS responder started. Use http://%s.local",hostname);
  log(buf);
  webserver.on("/",          HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, "/index.html", String(), false, processor);});
  webserver.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, "/style.css", "text/css");});
  webserver.on("/toggle",    HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    request->redirect("/"); 
    request->send(SPIFFS, "/index.html", String(), false, processor); 
    });
  webserver.on("/off",       HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(LED_BUILTIN, LOW);  
    request->send(SPIFFS, "/index.html", String(), false, processor);
    });
  webserver.on("/doUpdate", HTTP_POST, [](AsyncWebServerRequest *request) {},
                                       [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    handleDoUpdate(request, filename, index, data, len, final);
    });
  webserver.on("/setwifi",  HTTP_POST, [](AsyncWebServerRequest *request){
    request->redirect("/"); 
    request->send(SPIFFS, "/index.html", String(), false, processor); 
    handleWifiUpdate(request);
    });
  webserver.on("/setmqtt",  HTTP_POST, [](AsyncWebServerRequest *request){
    request->redirect("/"); 
    request->send(SPIFFS, "/index.html", String(), false, processor); 
    handleMqttUpdate(request);
    });
  webserver.onNotFound([](AsyncWebServerRequest *request){request->send(404);});
  webserver.begin();
}

void getNewSSID() {
  WiFi.mode(WIFI_AP);
  WiFiServer asServer(80);
  WiFi.softAP(hostname, "ota-pass");
  IPAddress IP = WiFi.softAPIP();
  log(WiFi.macAddress().c_str());
  log(WiFi.softAPIP().toString().c_str());
  webserver_start();
  unsigned long t2 = millis() + 1000 * 60 * 3;
  while (t2 > millis()) {
  } 
  ESP.restart();
}

void connectToWifi() {
  log("[connetToWifi()]");
  if ((setting.is_defined[0] != 'Y') || (setting.wifi_ssid[0] == '\0')) {
    getNewSSID();
  }
  Serial.printf("Connecting to Wi-Fi, to %s ...\n",setting.wifi_ssid);
  WiFi.begin(setting.wifi_ssid, setting.wifi_pass);
}

void connectToMqtt() {
  log("Connecting to MQTT...");
  log(setting.mqtt_host);
  log(setting.mqtt_login);
  log(setting.mqtt_pass);
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected. IP address: ");
        Serial.println(WiFi.localIP());
        qty_wifi_attempt = 0;
        connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.printf("WiFi lost connection %d\n", ++qty_wifi_attempt);
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
        if (qty_wifi_attempt >= 4) {
          qty_wifi_attempt = 0;
          getNewSSID();
        }
        
		xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  uint16_t packetIdSub = mqttClient.subscribe("test/lol", 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);
  mqttClient.publish("test/lol", 0, true, "test 1");
  Serial.println("Publishing at QoS 0");
  uint16_t packetIdPub1 = mqttClient.publish("test/lol", 1, true, "test 2");
  Serial.print("Publishing at QoS 1, packetId: ");
  Serial.println(packetIdPub1);
  uint16_t packetIdPub2 = mqttClient.publish("test/lol", 2, true, "test 3");
  Serial.print("Publishing at QoS 2, packetId: ");
  Serial.println(packetIdPub2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void setup(void) {
  char chipstr[5];                                // последние 4 байта мак-адреса чипа
  IPAddress ipmqtt;
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  Serial.println("");
  log("begin");
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  uint64_t chipid = ESP.getEfuseMac();             // The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(chipstr, sizeof(chipstr), "%04X", chip);
  snprintf(hostname, sizeof(hostname), "%s-pc-01", shortboardname);
  snprintf(buildversion, sizeof(buildversion), "%s, %s, %s %s", hostname, ver, __DATE__, __TIME__);
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  if (!EEPROM.begin(150)) {
    Serial.println("failed to initialise EEPROM"); delay(1000 * 10);
  }
  multiBlinc(3);
  EEPROM.get(EEPROM_OFFSET, setting);
  WiFi.onEvent(WiFiEvent);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  ipmqtt.fromString(String(setting.mqtt_host));
  mqttClient.setServer(ipmqtt, MQTT_PORT);
  mqttClient.setCredentials(setting.mqtt_login, setting.mqtt_pass);
  connectToWifi();
  multiBlinc(2);
  webserver_start();
  time_to_blink = millis() + 1000UL * period_blink;
  dallassensor.begin();
}

void loop(void) {
  char str[32];
  if (millis() > time_to_blink) {
    time_to_blink = millis() + 1000UL * period_blink;
    multiBlinc(1);
  }
  if (millis() > time_to_meashure) {
    time_to_meashure = millis() + 1000UL * period_meashure;
    dallassensor.requestTemperatures(); // Send the command to get temperatures
    snprintf(internal_temperature, sizeof(internal_temperature), "%.2f", dallassensor.getTempCByIndex(1));
    snprintf(str,sizeof(str),"Internal_temperature = %s;", internal_temperature);
    log(str);
    uint16_t packetIdPub2 = mqttClient.publish("internal_temperature", 2, true, internal_temperature);
    snprintf(external_temperature, sizeof(external_temperature), "%.2f", dallassensor.getTempCByIndex(0));
    snprintf(str,sizeof(str),"External_temperature = %s;", external_temperature);
    log(str);
  }
  delay(1);
}