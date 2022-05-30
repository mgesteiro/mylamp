/*
 * OTA info at https://github.com/esp8266/Arduino/tree/master/doc/ota_updates
 * GPIO pins info at https://circuits4you.com/2018/01/01/esp8266-gpio-pins/
 * SPIFFs info at https://github.com/esp8266/arduino-esp8266fs-plugin
 * WiFiManager library at https://github.com/tzapu/WiFiManager
 */
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <FS.h>

#define WIFISSID "MyLamp"
#define WIFIPSK  "12345678"
#define TPin  4
#define SLed  5
#define WPin 16
#define RPin 14
#define GPin 12
#define BPin 13
#define longPressTime 1000L  // 1 sec.


bool OTAavailable = false;
ESP8266WebServer wserver(80);


/*
 * Enable OTA's service if OTA WiFi is found
 */
bool initOTA(const char* OTAssid, const char* OTApsk) {
  #define OTASSID "lampsetup"
  #define OTAPSK  "lampsetup"
  // try to connect to OTA WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(OTAssid, OTApsk);
  Serial.print("Connecting to OTA WiFi [");
  Serial.print(OTAssid);
  Serial.print("/");
  Serial.print(OTApsk);
  Serial.print("]... ");
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("FAIL");
    return false;
  }
  // setup OTA service
  // ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(OTAssid); // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("OTA Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("OTA Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("OTA Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("OTA Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("OTA End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("SUCCESS");
  Serial.print("OTA ready on IP address: ");
  Serial.println(WiFi.localIP());
  return true;
} // initOTA()

/*
 * Starts WiFiManager to setup a WiFi connection for the Lamp:
 * It will connect automatically to a previous setup WiFi if available or
 * it will create a Cautive Portal ("WiFiLamp") if not.
 * Either way, upon return of this function, the portal will be closed.
 */
void startWM() {
  // hack to remove config from previous call to begin() for OTA WiFi
  struct station_config conf;
  wifi_station_get_config_default(&conf);
  wifi_station_set_config(&conf);
  // WiFiManager
  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  wm.setShowInfoUpdate(false);
  wm.setTitle("MyLamp WiFi setup");
  bool res = wm.autoConnect("WiFiLampSetup");
  if(!res) {
    Serial.println("Failed to connect to WiFi network: quit or wrong password?");
  } else {
    Serial.print("Connected to WiFi network [");
    Serial.print(WiFi.SSID());
    Serial.print("] with IP address: ");
    Serial.println(WiFi.localIP());
  }
} // startWM()


/*******************************************************************
 * 
 *   W E B   S T U F F
 * 
 *******************************************************************/
int R,G,B;  
bool isLightOn, touch, touchActive, longPressActive = false;
long touchTimer = 0;
String ihtml="";

String RGBToString() {
  String r,t;
  t=String(R / 4, 16);
  if (t.length() < 2) t="0"+t;
  r=t;
  t=String(G / 4, 16);
  if (t.length() < 2) t="0"+t;
  r+=t;
  t=String(B / 4, 16);
  if (t.length() < 2) t="0"+t;
  r+=t;
  return r;
}

void StringToRGB(String rgb) {
  if (rgb.length() != 6) rgb="404040";
  R=strtol(rgb.substring(0,2).c_str(), 0, 16) *4;
  G=strtol(rgb.substring(2,4).c_str(), 0, 16) *4;
  B=strtol(rgb.substring(4).c_str(), 0, 16)   *4;
}

void loadConfig() {
  File f = SPIFFS.open("/config.txt", "r");
  if (!f) {
    Serial.println("ERROR: config open failed");
    Serial.println("Defaulting to: #404040");
    StringToRGB("404040"); // 1/4 intensity white
    return;
  }
  String param=f.readString();
  f.close();
  Serial.println("Loaded config: #"+param);
  StringToRGB(param);
}

void saveConfig() {
  File f = SPIFFS.open("/config.txt", "w");
  if (!f) {
    Serial.println("Config open failed (saving)");
    return;
  }
  String param=RGBToString();
  Serial.println("Saving config: #"+param);
  f.print(param);
  f.close();
}

void loadIndex() {
  File f = SPIFFS.open("/index.html", "r");
  if (f) ihtml=f.readString();
  f.close();
}

void setupLamp() {
  // Touch
  pinMode(TPin, INPUT);
  pinMode(SLed, OUTPUT);
  // RGB
  pinMode(RPin, OUTPUT);
  pinMode(GPin, OUTPUT);
  pinMode(BPin, OUTPUT);
}

void switchLamp(int R, int G, int B, int W) {
  analogWrite(RPin, R);
  analogWrite(GPin, G);
  analogWrite(BPin, B);
  analogWrite(WPin, W);
  isLightOn=(R+G+B)>10; // light on or off indicative
}

void flashColor(int R, int G, int B, bool on) {
  for (int i=0; i<5; i++) {
    // on
    switchLamp(R,G,B,0);
    delay(175);
    if (i>=4 && on) break;
    // off
    switchLamp(0,0,0,0);
    delay(100);
  }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  flashColor(255*2, 50*2, 0, true); // half intensity orange
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) {
    path += "index.html";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    wserver.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleIndex() {
  if (ihtml.length() == 0) loadIndex();
  String ndx = ihtml;
  ndx.replace("[RGB]", "#"+RGBToString());
  wserver.send(200, "text/html", ndx);
}

void handleDo() {
  if (!wserver.hasArg("color")) {
    wserver.send(500, "text/plain", "BAD ARGS");
    return;
  }
  String param = wserver.arg("color");
  R=strtol(param.substring(0,2).c_str(), 0, 16);
  G=strtol(param.substring(2,4).c_str(), 0, 16);
  B=strtol(param.substring(4).c_str(), 0, 16);
  
  Serial.println("Color change requested: "+param);
  R*=4; // mapping 256 to 1024
  G*=4; // mapping 256 to 1024
  B*=4; // mapping 256 to 1024
  switchLamp(R, G, B, 0);
  wserver.send(200, "text/plain", "DONE");
}

void handleSave() {
  saveConfig();
  wserver.send(200, "text/plain", "DONE");
}

void startServer() {
  if (!MDNS.begin(WIFISSID)) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.print("mDNS responder started: ");
    Serial.print(WIFISSID);
    Serial.println(".local");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }
  // webServer (depends of SPIFFs)  
  wserver.on("/", handleIndex);
  wserver.on("/do", handleDo);
  wserver.on("/save", handleSave);
  wserver.onNotFound([]() {
    if (!handleFileRead(wserver.uri())) {
      wserver.send(404, "text/plain", "FileNotFound");
    }
  });
  wserver.begin();
  Serial.print("HTTP server started: go to http://");
  Serial.print(WIFISSID);
  Serial.println(".local");
} // startServer()

/*******************************************************************
 *
 *   S E T U P   &   L O O P
 *
 *******************************************************************/
void setup() {
  // hardware setup
  setupLamp();
  flashColor(255, 0, 0, false); // ready
  // status LEDs
  digitalWrite(SLed, LOW); // turn-on
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turn-on
  // debug output
  Serial.begin(115200);
  Serial.println("\nBooting...");
  // Filesystem
  SPIFFS.begin();
  loadConfig();
  // OTA
  OTAavailable = initOTA(OTASSID, OTAPSK);
  if (OTAavailable) {
    flashColor(255*2, 50*2, 0, true); // half intensity orange ON
  } else { 
    startWM(); // try to connect to a WiFi
    // start AP if no connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Starting Lamp's WiFi AP [");
      Serial.print(WIFISSID);
      Serial.print("/");
      Serial.print(WIFIPSK);
      Serial.println("]...");
      delay(1000);
      if (!WiFi.softAP(WIFISSID, WIFIPSK)) {
        Serial.println("ERROR: unable to create AP!");
        Serial.println("[System halted]");
        while(true);
      } else {
        Serial.print("IP address: ");
        Serial.println(WiFi.softAPIP());
      }
    }
    // start server
    delay(1000);
    startServer();
    // all OK
    flashColor(0, 255, 0, false); // half intensity green
    //switchLamp(R, G, B, 0);
  }
  WiFi.hostname(WIFISSID);
  Serial.println("Initialization finished.");
  digitalWrite(LED_BUILTIN, HIGH); // turn-off
  digitalWrite(SLed, HIGH); // turn-off
}

void loop() {
  if (OTAavailable) {
    ArduinoOTA.handle();
  } else {    
    // server
    MDNS.update();
    wserver.handleClient();
    // touch
    touch = (digitalRead(TPin) == LOW ? true : false);
    if (touch) {
      if ( !touchActive) {
        touchActive = true;
        touchTimer = millis();
        digitalWrite(SLed, !touch);

        // quick press action
        Serial.println("Quick press!");
        isLightOn = !isLightOn;
        Serial.print("LIGHT: ");
        Serial.println(isLightOn);
        if (isLightOn)
          switchLamp(R, G, B, 0);
        else
          switchLamp(0, 0, 0, 0);      
      }
      if ((millis() - touchTimer > longPressTime) && (!longPressActive)) {
        longPressActive = true;
        // long press action
        switchLamp(0, 0, 0, 1023); // full bright white
        isLightOn=true;
      }
    } else {
      longPressActive = false;
      touchActive = false;
      digitalWrite(SLed, !touch);
    }
  }
}
