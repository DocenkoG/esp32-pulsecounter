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
#include "SPIFFS.h"

const char shortboardname[] = "esp32";    // краткое наименование девайса
const char ver[] = "v0.2";                // Номер версии прошивки (как в git)
char hostname[16];
char buildversion[45];             // Переменная для полной версии в формате: hostname, ver, date and time compilation 
String buflog;                     // текстовый буфер для отладочного вывода в web
#define MAX_BUFLOG_SIZE 2000       // ограничение размера этого буфера.
unsigned long time_to_meashure = 0;       // (2^32 - 1) = 4,294,967,295
unsigned long time_to_reboot = 0;
unsigned long time_to_blink = 0;
size_t content_len;
const char platform[] = ARDUINO_BOARD;
const char device[] = "Pulse Counter 01";



void multiBlinc(int qty) {
   for (int i = 0; i < qty + qty; i++) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(150);
   }
}



void log(const char *sss) {
   char tbuf[14];
   sprintf(tbuf, "%010lu ", millis());
   Serial.print(tbuf);
   Serial.print(" ");
   Serial.println(sss);
   buflog += tbuf + String(sss) + "<br>";
   if (buflog.length() > MAX_BUFLOG_SIZE) {
      buflog = buildversion +  String("\n") + buflog.substring(buflog.indexOf("\n", 40));
   }
}



void saveNCharsToEeprom(int addres, int len, char *str) {
  while (*str != '\0' && len-- > 1) {
    EEPROM.write(addres++, *str);
    str++; 
  }
  EEPROM.write(addres, '\0');
}



void readNCharsFromEeprom(int addres, int len, char *str) {
  *str++ = char(EEPROM.read(addres++));
  len--;
  do {
    *str = char(EEPROM.read(addres++));
  } while ((*str++ != '\0') && (len-- > 1));
  *str = '\0';
}



void saveNewSSID( String  ssid, String pass) {
  char wifi_name[18];
  char wifi_pass[32];

  ssid.toCharArray(wifi_name, 18);
  pass.toCharArray(wifi_pass, 32);
  Serial.print("Save char:"); Serial.print(wifi_name); Serial.print("|");Serial.print(wifi_pass); Serial.println(".");
  saveNCharsToEeprom( 0, 18, wifi_name);
  saveNCharsToEeprom(18, 32, wifi_pass);
  EEPROM.commit();
}


void readOldSSID(char *ssid, char *pass) {
  readNCharsFromEeprom( 0, 18, ssid);
  readNCharsFromEeprom(18, 32, pass);
  Serial.print("Read:"); Serial.print(ssid); Serial.print("|");Serial.print(pass); Serial.println();
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
  }
  return(String());
}


void getNewSSID() {
  WiFi.mode(WIFI_AP);
  WiFiServer asServer(80);
  String header;
  String new_ssid;
  String new_pass;
  WiFi.softAP(hostname, "ota-pass");
 
  IPAddress IP = WiFi.softAPIP();
  Serial.print(" AP IP address: ");
  Serial.println(IP);
  Serial.println(WiFi.macAddress());
  
  asServer.begin();
  MDNS.end();
  MDNS.begin(hostname);
  
  unsigned long t2 = millis() + 1000 * 60 * 15;
  while (t2 > millis()) {
    WiFiClient client = asServer.available();
    if (client) {                             // If a new client connects,
      Serial.println("New Client.");          // print a message out in the serial port
      String currentLine = "";                // make a String to hold incoming data from the client
      header = "";
      while (client.connected()) {            // loop while the client's connected
        if (client.available()) {             // if there's bytes to read from the client,
          char c = client.read();             // read a byte, then
          Serial.write(c);                    // print it out the serial monitor
          header += c;
          if (c == '\n') {                    // if the byte is a newline character
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              int pos = header.indexOf("id=");
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();              
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println("</style></head>");
              client.println("<body><h1>ESP32 Web Server</h1>");
              if (pos >= 0) {
                client.println("<h2>Ok</h2>");
              } else {
                client.println("<h2>Type in URL:id=NewSsid/pwd</h2>");
              }
              client.println("</body></html>");
              client.println();      // The HTTP response ends with another blank line
              if (pos >= 0) {
                header = header.substring(pos+3, header.indexOf(" ", pos));
                new_ssid = header.substring(0, header.indexOf("/"));
                new_pass = header.substring(header.indexOf("/") + 1);
                Serial.print(" new_ssid<");
                Serial.print(new_ssid);
                Serial.println(">");
                Serial.print(" new_pass<");
                Serial.print(new_pass);
                Serial.println(">");
                saveNewSSID(new_ssid, new_pass);
                ESP.restart();
              }
              break;                 // Break out of the while loop
            }
          }
        }
      }
    }
  }
  ESP.restart();
}



void connect_wifi() {
  char wifi_name[18];
  char wifi_pass[32];
  readOldSSID(wifi_name, wifi_pass);
  WiFi.persistent(false);
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(wifi_name); Serial.print("|"); Serial.print(wifi_pass); Serial.print("|");
  WiFi.begin(wifi_name, wifi_pass);
  for (int k = 0; k < 10; ++k) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(WiFi.localIP());
      return;
    } else {
      Serial.print(".");
      if (WiFi.status() == WL_CONNECT_FAILED) {
        break;
      }
      delay(1000);
    }
  }
  Serial.print(">>>  Timeout !");
  getNewSSID();
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



void printProgress(size_t prg, size_t sz) {
  Serial.printf("Progress: %d%%\n", (prg*100)/content_len);
}


void webserver_start() {
  /*use mdns for host name resolution*/
  if (!MDNS.begin(hostname)) {                         // http://chipname.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(500);
      multiBlinc(2);
    }
  }
  Serial.println("mDNS responder started");

  webserver.on("/",    HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, "/index.html", String(), false, processor);});
  webserver.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, "/style.css", "text/css");});
  webserver.on("/on",  HTTP_GET, [](AsyncWebServerRequest *request){digitalWrite(LED_BUILTIN, HIGH); request->send(SPIFFS, "/index.html", String(), false, processor);});
  webserver.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){digitalWrite(LED_BUILTIN, LOW);  request->send(SPIFFS, "/index.html", String(), false, processor);});
  webserver.on("/doUpdate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) 
                               {handleDoUpdate(request, filename, index, data, len, final);});
  webserver.onNotFound([](AsyncWebServerRequest *request){request->send(404);});
  webserver.begin();
  Update.onProgress(printProgress);
}



void setup(void) {
  char chipstr[5];                                // последние 4 байта мак-адреса чипа
  pinMode(LED_BUILTIN, OUTPUT);
    // Инициализируем SPIFFS:
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  Serial.println("");
  uint64_t chipid = ESP.getEfuseMac();             // The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(chipstr, sizeof(chipstr), "%04X", chip);
  snprintf(hostname, sizeof(hostname), "%s-pc-01", shortboardname);
  snprintf(buildversion, sizeof(buildversion), "%s, %s, %s %s", hostname, ver, __DATE__, __TIME__);
  Serial.println(buildversion);
  buflog = buildversion + String("\n");
  log("begin");
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  if (!EEPROM.begin(100))
  {
    Serial.println("failed to initialise EEPROM"); delay(1000 * 10);
  }
  multiBlinc(3);
  connect_wifi();

  WiFi.enableSTA(true);

  // TODO Hostname setting does not work. Always shows up as "espressif"
  if(WiFi.setHostname(hostname)) {
    Serial.printf("\nHostname set!\n");
  } else {
    Serial.printf("\nHostname NOT set!\n");
  }

  multiBlinc(2);
  webserver_start();
  dallassensor.begin();
}



void loop(void) {
  char str[32];

  if (millis() > time_to_blink) {
    time_to_blink = millis() + 1000UL * 5;
    multiBlinc(1);
  }
  
  if (millis() > time_to_meashure) {
    time_to_meashure = millis() + 1000UL * 60;
    dallassensor.requestTemperatures(); // Send the command to get temperatures
    snprintf(str, sizeof(str), " Temperature = %.2f", dallassensor.getTempCByIndex(0));
    log(str);
  }
}
