/*=========================
  AIR MONITOR
  Created by JAPISOFT 2021
  =========================*/

#include "config.h"             // Nacita/prida subor config.h
#include <Wire.h>               // Zbernica I2C

#include <SparkFunCCS811.h>     // https://github.com/sparkfun/SparkFun_CCS811_Arduino_Library
CCS811 myCCS811(0x5A);          // Defaultna-prednastavena I2C adresa je 0x5A (0x5B - alternativna)

#include <Adafruit_Sensor.h>    // Podpora komunikacie so senzormi Adafruit
#include <Adafruit_BME680.h>    // Komunikacia s BME680
Adafruit_BME680 bme;                      // Vytvorenie objektu BME680 s nazvom bme
#define SEALEVELPRESSURE_HPA (1013.25)    // Zakladny tlak na hladine mora v hPa
float Temperature, Humidity, Pressure, Altitude, Aqi;

// WIFI + WEBSERVER + OTA update
#include <WiFi.h>                               // WIFI pre ESP32
#include <ESPAsyncWebServer.h>    // Asynchronny webovy server - https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);        // Vytvorenie asynchronneho weboveho servera s pristupom na porte 80
#include <AsyncElegantOTA.h>      // Asynchronny OTA update software - https://github.com/ayushsharma82/AsyncElegantOTA

// PRACA s JSON
#include <ArduinoJson.h>          // Praca s JSON - https://github.com/bblanchon/ArduinoJson


// Spojenie s WIFI
void wifiConnect() {
  Serial.println();
  Serial.printf("Spajam s %s ", ssid);
  WiFi.begin(ssid, password);            // Vykonat spojenie s lokalnou wifi sietou
  while (WiFi.status() != WL_CONNECTED)  // Kontrola, ci uz doslo k spojeniu - ak nie pise bodky na konzolu
  {
    Serial.print(".");
    delay(500);                          // Pauza 500 ms
  }
  Serial.println(" spojene.");
  Serial.print("Moja IP adresa: ");
  Serial.println(WiFi.localIP());        // Vypis pridelenej IP adresy na konzolu
  Serial.print("Moja MAC adresa: ");
  Serial.println(WiFi.macAddress());     // Vypis MAC adresy ESP32 na konzolu
}

// ŠTART A OBSLUHA ASYNCHRONNEHO WEBOVEHO SERVERA A OTA UPDATE
void handleRequests() {
  // web api /api
  server.on("/api", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "application/json", Get_json_values());
  });
  server.onNotFound(handle_NotFound);

  AsyncElegantOTA.begin(&server);                       // Start AsyncElegantOTA ... http://IPadresaESP32/update
  server.begin();                                       // Start weboveho servera
  Serial.println();
  Serial.println("Webovy server je nastartovany, OTA update funkcny.");
  Serial.println();
}

// OBSLUHA NEEXISTUJUCEJ ADRESY WEBOVEHO SERVERA
void handle_NotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Nenajdene!!!");
}


void setup() {
  Serial.begin(115200);               // Nastavenie a start seriovej komunikacie zadanou rychlostou
  Wire.begin(PIN_SDA, PIN_SCL);       // Start I2C zbernice na zadanych pinoch
  delay(1);                           // Pauza X sekund

  bme680_begin();                     // Inicializacia BME680
  ccs811_begin();                     // Inicializacia CCS811
  wifiConnect();                      // Spojenie s WiFi sietou
  handleRequests();                   // Obsluha volani weboveho servera
}

void loop() {

}


// INICIALIZACIA A NASTAVENIE BME680
void bme680_begin() {
  Serial.println();
  Serial.print("Inicializujem BME680 ...");
  if (!bme.begin()) {                         // Inicializacia BME680
    Serial.println(".");
    while (500);                              // Pauza 500 ms
  }
  Serial.println(" inicializované.");

  /* Konfiguracia BME680 */
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
}

// INICIALIZACIA a KONTROLA PRIPRAVENOSTI SENZORA CCS811
void ccs811_begin() {
  Serial.print("Inicializujem CCS811 ...");
  while (myCCS811.begin() == false) {         // Inicializacia CCS811
    Serial.print(".");
    delay(500);                               // Pauza 500 ms
  }
  Serial.println(" inicializované.");

  Serial.print("Pripravujem CCS811 ...");
  while (!myCCS811.dataAvailable()) {         // Kontrola pripravenosti CCS811
    Serial.print(".");
    delay(500);                               // Pauza 500 ms
  }
  Serial.println(" pripravené.");
}

// NACITA UDAJE + VYTVORI A VRATI JSON OBJEKT
String Get_json_values() {
  String ccs_json;
  StaticJsonDocument<200> json;                             // Vytvori prazdny JSON objekt s nazvom json a zadanou velkostou

  Temperature = bme.readTemperature();                      // Zisti teplotu
  Humidity = bme.readHumidity();                            // Zisti vlhkost
  Pressure = bme.readPressure() / 100.0F;                   // Zisti tlak
  Altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);        // Zisti nadmorsku vysku
  Aqi = (bme.gas_resistance / 1000.0);                      // Zisti kvalitu vzduchu
  json["temp"] = String(Temperature, 1);      // Zapise teplotu do JSON objektu
  json["humi"] = String(Humidity, 0);         // Zapise vlhkost do JSON objektu
  json["pres"] = String(Pressure, 0);         // Zapise tlak do JSON objektu
  json["alti"] = String(Altitude, 0);         // Zapise nadmorsku vysku do JSON objektu
  json["aqi"] = String(Aqi, 0);               // Zapise hodnotu kvality vzduchu do JSON objektu
  switch (int(Aqi)) {                         // Zapise slovne hodnotenie kvality vzduchu do JSON objektu
    case 0 ... 50:
      json["aq"] = String("Výborná");
      break;
    case 51 ... 100:
      json["aq"] = String("Dobrá");
      break;
    case 101 ... 150:
      json["aq"] = String("Priemerná");
      break;
    case 151 ... 200:
      json["aq"] = String("Horšia");
      break;
    case 201 ... 300:
      json["aq"] = String("Zlá");
      break;
    case 301 ... 1023:
      json["aq"] = String("Veľmi zlá");
      break;
    default:
      json["aq"] = String("-----");
  }
  json["dewp"] = String(DewPoint(Temperature, Humidity), 0); // Zapise Rosny bod do JSON objektu

  if (myCCS811.dataAvailable()) {                            // Vykona ak je senzor CCS811 pripraveny
    myCCS811.setEnvironmentalData(Humidity, Temperature);    // Zaslanie aktualnej vlhkosti/teploty do senzora CCS811 - zlepsuje to presnost senzora
    myCCS811.readAlgorithmResults();                         // Pripravi CCS811 na citanie udajov
    json["co2"] = String(myCCS811.getCO2());                 // Zisti CO2
    json["tvoc"] = String(myCCS811.getTVOC());               // Zisti TVOC
  }

  serializeJson(json, ccs_json);                             // Vytvori JSON objekt
  Serial.println(ccs_json);
  return ccs_json;                                           // Vrati JSON objekt
}

// VYPOCITA A VRATI Rosny bod
double DewPoint(double TemperatureCelsius, double Humidity)
{
  double a = 17.271;
  double b = 237.7;
  double temp = (a * TemperatureCelsius) / (b + TemperatureCelsius) + log(Humidity * 0.01);
  double DP = (b * temp) / (a - temp);
  return DP;
}
