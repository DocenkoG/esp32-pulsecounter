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
// -----------------------------------------------------------------------------------
#include <EEPROM.h>
#define EEPROM_OFFSET   1
#include "SPIFFS.h"

const char shortboardname[] = "esp32";    // краткое наименование девайса
const char ver[] = "v0.2";                // Номер версии прошивки (как в git)
char hostname[16];
char buildversion[45];             // Переменная для полной версии в формате: hostname, ver, date and time compilation 
String buflog = String('\n');             // текстовый буфер для отладочного вывода в web
#define MAX_BUFLOG_SIZE 4000              // ограничение размера этого буфера.
unsigned long time_to_meashure = 0;       // (2^32 - 1) = 4,294,967,295
unsigned long time_to_reboot = 0;
unsigned long time_to_blink = 0;
int period_blink = 5;              // Период в секундах мигания при нормальной работе 
int period_meashure = 60;           // Периодичность проведения измерений в секундах 
size_t content_len;
const char platform[] = ARDUINO_BOARD;
const char device[] = "Pulse Counter 01";
char internal_temperature[7];
struct config{
  char is_defined[2];
  char wifi_ssid[16];
  char wifi_pass[32];
  char mqtt_host[16];
  char mqtt_login[16];
  char mqtt_pass[16];
  char mqtt_topic[16]; 
};
config setting;


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
  } else if (var == "INTERNAL_TEMPERATURE") {
    return(String(internal_temperature));
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
  snprintf(newVal,15,"%s",request->getParam(0)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_host, 15,"%s",newVal);
  }
  snprintf(newVal,15,"%s",request->getParam(1)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_topic, 15,"%s",newVal);
  }
  snprintf(newVal,15,"%s",request->getParam(2)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_login, 15,"%s",newVal);
  }
  snprintf(newVal,15,"%s",request->getParam(3)->value().c_str());
  if (newVal[0] != '\0') {snprintf(setting.mqtt_pass, 15,"%s",newVal);
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
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(500);
      multiBlinc(2);
    }
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
  unsigned long t2 = millis() + 1000 * 60 * 15;
  while (t2 > millis()) {
  } 
  ESP.restart();
}


void connect_wifi() {
  EEPROM.get(EEPROM_OFFSET, setting);
  if (setting.is_defined[0] != 'Y') {
    getNewSSID();
  }
  WiFi.persistent(false);
  delay(10);
  log("Connecting to ");
  log(setting.wifi_ssid);
  WiFi.begin(setting.wifi_ssid, setting.wifi_pass);
  for (int k = 0; k < 12; ++k) {
    if (WiFi.status() == WL_CONNECTED) {
      log(WiFi.localIP().toString().c_str());
      return;
    } else {
      log(".");
      if (WiFi.status() == WL_CONNECT_FAILED) {
        break;
      }
      delay(1000);
    }
  }
  log(">>>  Timeout !");
  getNewSSID();
}


void setup(void) {
  char chipstr[5];                                // последние 4 байта мак-адреса чипа
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  Serial.println("");
  log("begin");
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
  connect_wifi();
  WiFi.enableSTA(true);
  // TODO Hostname setting does not work. Always shows up as "espressif"
  WiFi.setHostname(hostname);
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
    snprintf(internal_temperature, sizeof(internal_temperature), "%.2f", dallassensor.getTempCByIndex(0));
    snprintf(str,sizeof(str),"Internal_temperature = %s;", internal_temperature);
    log(str);
  }
  delay(1);
}