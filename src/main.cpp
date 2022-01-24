#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFi-Credentials.h>
#include <ArduinoJson.h>
#include <StopWatch.h>
#include <SPI.h>
#include "epd1in54_V2.h"
#include "epdpaint.h"

#define DEEP_SLEEP_MODE
#ifdef DEEP_SLEEP_MODE
  #define SLEEP_TIME 300e6
  #define SLEEP_RESOLUTION StopWatch::MICROS
  #define SLEEP(...) { ESP.deepSleep(__VA_ARGS__); }
#else
  #define SLEEP_TIME 300e3
  #define SLEEP_RESOLUTION StopWatch::MILLIS  
  #define SLEEP(...) { delay(__VA_ARGS__); }
#endif

WiFiClient wifiClient;
HTTPClient httpClient;
DynamicJsonDocument json(2048);
Epd epd;
unsigned char image[1200]; // 200 * 48 / 8
Paint paint(image, 0, 0);
StopWatch stopWatch(SLEEP_RESOLUTION);  

//#define DEBUG_EDOMO
#ifdef DEBUG_EDOMO
  #define DEBUG_PRINTER Serial
  #define DEBUG_PRINT(...) { DEBUG_PRINTER.print(__VA_ARGS__); }
  #define DEBUG_PRINTLN(...) { DEBUG_PRINTER.println(__VA_ARGS__); }
  #define DEBUG_PRINTF(...) { DEBUG_PRINTER.printf(__VA_ARGS__); }
#else
  #define DEBUG_PRINT(...) {}
  #define DEBUG_PRINTLN(...) {}
  #define DEBUG_PRINTF(...) {}
#endif

//#define LED_DEBUG
#ifdef LED_DEBUG
  #define DEBUG_LED(x) { flash(x); }
  void flash(uint8_t x) {
    pinMode(LED_BUILTIN, OUTPUT);
    while(x-- > 0) {
      digitalWrite(LED_BUILTIN, HIGH); delay(500);
      digitalWrite(LED_BUILTIN, LOW); delay(500);
    }
  } 
#else
  #define DEBUG_LED(x) {}
#endif

#define IDX_LIVING_ROOM     167
#define IDX_AIR_QUALITY     168
#define IDX_GARDEN_TEMP     170
#define IDX_OWM_DESCRIPTION 508

#define COLORED     0
#define UNCOLORED   1

#define MAX_WIFI_RETRY 25

IPAddress hostIp(192, 168, 0, 97); 
IPAddress gatewayIp(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0); 
IPAddress domoticzpiIp(192, 168, 0, 40);
const uint16_t httpPort = 80;

String serverTime;
String sunriseTime;
String sunsetTime;
String airQuality;
String airQualityDescription;
String insideTemperature;
String insideHumidityStatus;
String outsideTemperature;
String outsideWeatherDescription;

void worker();
void getData();
void displayData();
void getDomoticzData(uint16_t idx);
uint8_t GetAirQualityIconIndex(String airQualityDescription);
uint8_t GetHumidityStatusIconIndex(String humidityStatus);
uint8_t GetWeatherIconIndex(String weatherDescription);
String mapAirQuality(String airQuality);
String mapHumidityStatus(String humidityStatus);


void setup() 
{  
  delay(10);

#ifdef DEBUG_EDOMO
  DEBUG_PRINTER.begin(115200);
  DEBUG_PRINTER.setDebugOutput(true);
#endif

#ifdef DEEP_SLEEP_MODE  
  worker();
#endif 
}


void loop() {
    worker();
}


void worker() {
  
  stopWatch.reset();
  stopWatch.start();

  getData();
  displayData();

  DEBUG_PRINTLN(F("Sleeping..."));
  
  SLEEP(SLEEP_TIME - stopWatch.elapsed());  // delay or deep sleep, depending on DEEP_SLEEP_MODE
}


void getDomoticzData(uint16_t idx)
{
  String url = String(F("http://")) + domoticzpiIp.toString() + F("/json.htm?type=devices&rid=") + idx;
  
  httpClient.useHTTP10(true);

  httpClient.begin(wifiClient, url);
  httpClient.GET();
  
  deserializeJson(json, httpClient.getStream());  
}

bool wifiConnect() {

  DEBUG_PRINTLN(F("WiFi connect..."));

  WiFi.forceSleepWake();
  delay(1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.config(hostIp, gatewayIp, subnet); // speed things up
  WiFi.begin(ssid, password);

  uint8_t retryCount = MAX_WIFI_RETRY;
  while (retryCount-- > 0) {
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN(WiFi.localIP()); 
      return true; 
    }
    delay(500);
    DEBUG_PRINTLN(".");
  }

  DEBUG_PRINTLN(F("ERROR: wifiConnect"));

  return false;
}

void wifiDisconnect() {
  
  DEBUG_PRINTLN(F("wifiDisconnect"));
  
  httpClient.end();  
  wifiClient.stop();

  WiFi.disconnect();
  WiFi.forceSleepBegin();
  delay(1);  
}


void getData()
{
  if (!wifiConnect()) return;

#ifdef DEBUG_EDOMO
  WiFi.printDiag(DEBUG_PRINTER);
#endif

  DEBUG_PRINTLN(F("Client connect..."));
  if (!wifiClient.connect(domoticzpiIp, httpPort)) {    
    DEBUG_PRINTLN(F("ERROR: Client connection"));
    return;
  }  

  JsonObject result;

  // inside - temperature
  getDomoticzData(IDX_LIVING_ROOM);
  result = json[F("result")][0];
  insideTemperature = result[F("Temp")].as<String>();
  insideHumidityStatus = mapHumidityStatus(result[F("HumidityStatus")].as<String>());
  
  // and some general stuff
  serverTime = json[F("ServerTime")].as<String>();
  serverTime = serverTime.substring(serverTime.indexOf(' ') + 1, serverTime.length() - 3); // only hh:mm part
  sunriseTime = json[F("Sunrise")].as<String>();
  sunsetTime = json[F("Sunset")].as<String>();
     
  // inside - air quality
  getDomoticzData(IDX_AIR_QUALITY);
  result = json[F("result")][0];
  airQuality = result[F("Data")].as<String>();
  airQuality = airQuality.substring(0, airQuality.indexOf(' '));
  airQualityDescription = mapAirQuality(result[F("Quality")].as<String>());

  // outside - temperature
  getDomoticzData(IDX_GARDEN_TEMP);
  result = json[F("result")][0];
  outsideTemperature = result[F("Temp")].as<String>();  

  // outside - weather description
  getDomoticzData(IDX_OWM_DESCRIPTION);
  result = json[F("result")][0];
  outsideWeatherDescription = result[F("Data")].as<String>();

  wifiDisconnect();  
}


void displayData()
{
  epd.LDirInit();
  epd.Clear();

  paint.SetWidth(200);
  paint.SetHeight(48);

  // living room - Temperature
  paint.Clear(UNCOLORED);
  paint.DrawIconAt(0, 0, GetHumidityStatusIconIndex(insideHumidityStatus), &Icon48, COLORED);
  paint.DrawStringAt(56, 4, insideTemperature.c_str(), &Font24, COLORED);
  paint.DrawStringAt(56 + Font24.Width * insideTemperature.length(), 4, "o", &Font16, COLORED);
  paint.DrawStringAt(56, 32, insideHumidityStatus.c_str(), &Font16, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());

  // living room - air quality
  paint.Clear(UNCOLORED);
  paint.DrawIconAt(0, 0, GetAirQualityIconIndex(airQualityDescription), &Icon48, COLORED);
  paint.DrawStringAt(56, 4, airQuality.c_str(), &Font24, COLORED);
  paint.DrawStringAt(56 + Font24.Width * airQuality.length(), 4, "ppm", &Font16, COLORED);
  paint.DrawStringAt(56, 32, airQualityDescription.c_str(), &Font16, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 60, paint.GetWidth(), paint.GetHeight());

  // outside - Temperature
  paint.Clear(UNCOLORED);
  paint.DrawIconAt(0, 0, GetWeatherIconIndex(outsideWeatherDescription), &Icon48, COLORED);
  paint.DrawStringAt(56, 4, outsideTemperature.c_str(), &Font24, COLORED);
  paint.DrawStringAt(56 + Font24.Width * outsideTemperature.length(), 4, "o", &Font16, COLORED);
  paint.DrawStringAt(56, 32, outsideWeatherDescription.c_str(), &Font16, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 120, paint.GetWidth(), paint.GetHeight());

  // sunrise, sunset & last update time
  paint.Clear(UNCOLORED);
  paint.DrawStringAt(0, 0, sunriseTime.c_str(), &Font16, COLORED);
  paint.DrawStringAt(Font16.Width * 6, 0, sunsetTime.c_str(), &Font16, COLORED);
  paint.DrawStringAt(200 - Font12.Width * 5, 4, serverTime.c_str(), &Font12, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 184, paint.GetWidth(), paint.GetHeight());

  epd.DisplayFrame();
   
  epd.Sleep();
}


String mapHumidityStatus(String humidityStatus)
{
  if (humidityStatus.equals(F("Dry")))
      return String(F("droog"));
  else if (humidityStatus.equals(F("Normal")))        
    return String(F("normaal"));
  else if (humidityStatus.equals(F("Comfortable")))        
    return String(F("comfortabel"));
  else if (humidityStatus.equals(F("Wet")))
    return String(F("nat"));
    
  return String(F("onbekend"));
}


String mapAirQuality(String airQuality)
{
  if (airQuality.equals(F("Excellent")))
      return String(F("uitstekend"));
  else if (airQuality.equals(F("Good")))        
    return String(F("goed"));
  else if (airQuality.equals(F("Fair")))        
    return String(F("redelijk"));
  else if (airQuality.equals(F("Inferior")))
    return String(F("inferieur"));
  else if (airQuality.equals(F("Inferior")))
    return String(F("slecht"));
    
  return String(F("onbekend"));
}


uint8_t GetWeatherIconIndex(String weatherDescription)
{
  if (weatherDescription.equals(F("bewolkt")))
    return ICON48_WEATHER_CLOUDY;
  else if (weatherDescription.equals(F("onbewolkt")))
    return ICON48_WEATHER_SUNNY;
  else if (weatherDescription.indexOf(F("wolk")) != -1)
    return ICON48_WEATHER_SUNNY_CLOUDY;
  else if (weatherDescription.indexOf(F("regen")) != -1)
    return ICON48_WEATHER_RAINY;

  return ICON48_ISSUE;
}


uint8_t GetHumidityStatusIconIndex(String humidityStatus)
{
  if (humidityStatus.equals(F("normaal")) || humidityStatus.equals(F("comfortabel")))
    return ICON48_FACE_HAPPY;
  else if (humidityStatus.equals(F("droog")) || humidityStatus.equals(F("nat")))
    return ICON48_FACE_UNHAPPY;

  return ICON48_ISSUE;
}


uint8_t GetAirQualityIconIndex(String airQualityDescription)
{
  if (airQualityDescription.equals(F("uitstekend")) || airQualityDescription.equals(F("goed")))
    return ICON48_FACE_HAPPY;
  else if (airQualityDescription.equals(F("redelijk")))
    return ICON48_FACE_NORMAL;
  else if (airQualityDescription.equals(F("inferieur")) || airQualityDescription.equals(F("slecht")))
    return ICON48_ALERT;

  return ICON48_ISSUE;
}
