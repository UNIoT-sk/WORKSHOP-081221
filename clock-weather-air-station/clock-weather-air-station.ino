/*=========================
  CLOCK-WEATHER-AIR STATION
  Created by JAPISOFT 2021
  =========================*/

#include "config.h"              // Nacita/prida subor config.h
#include <EasyNextionLibrary.h>  // Komunikacia s Nextion displejom - https://github.com/Seithan/EasyNextionLibrary
EasyNex myNex(Serial);           // Vytvorenie objektu myNex na seriovu komunikaciu s displejom Nextion

#include <Wire.h>                // Komunikacia cez zbernicu I2C

#include <SparkFunCCS811.h>      // https://github.com/sparkfun/SparkFun_CCS811_Arduino_Library
CCS811 myCCS811(0x5A);           // Defaultna I2C adresa je 0x5A (0x5B - alternativna)
uint16_t CO2 = 400, TVOC = 0;    // Startovacia hodnota CO2 a TVOC

#include <Adafruit_Sensor.h>     // Podpora komunikacie so senzormi Adafruit - https://github.com/adafruit/Adafruit_Sensor
#include <Adafruit_BME680.h>     // Komunikacia s BME680 - https://github.com/adafruit/Adafruit_BME680
Adafruit_BME680 bme;                    // Vytvorenie objektu BME680 s nazvom bme
#define SEALEVELPRESSURE_HPA (1013.25)  // Zakladny tlak na hladine mora v hPa
float Temperature, Humidity, Pressure, Altitude, Aqi;
String Aq;

// WIFI + WEBSERVER + OTA update
#include <WiFi.h>                 // WIFI pre ESP32
#include <ESPAsync_WiFiManager.h> // WIFI Manager - https://github.com/khoih-prog/ESPAsync_WiFiManager
AsyncWebServer webServer(80);     // Vytvorenie asynchronneho weboveho servera s pristupom na porte 80
DNSServer dnsServer;              // Vytvorenie DNS servera
#include <AsyncElegantOTA.h>      // Asynchronny OTA update software - https://github.com/ayushsharma82/AsyncElegantOTA

// PRISTUP k SUBOROVEMU SYSTEMU a PRACA s JSON
#include <SPIFFS.h>               // Pristup k suborovemu systemu ESP32
#include <ArduinoJson.h>          // Praca s JSON - https://github.com/bblanchon/ArduinoJson

// TIME a NTP
#include <time.h>                 // Praca s datumom a casom
const char* ntpServer = "europe.pool.ntp.org";  // Zvoleny NTP server
const uint16_t gmtOffset_sec = 3600;            // časový posun ku GMT času
const uint16_t daylightOffset_sec = 3600;       // zimný/letný časový posun
String time_hour, time_min, time_sec, date_day, date_month, date_year, date_wday;
String time_lastmin = "", date_lastday = "";
const String weekDays[7] = {"Nedeľa", "Pondelok", "Utorok", "Streda", "Štvrtok", "Piatok", "Sobota"};
const String months[12] = {"január", "február", "marec", "apríl", "máj", "jún", "júl", "august", "september", "október", "november", "december"};

#include <Ticker.h>               // Casovac - paralelne opakovane spustanie kodu
Ticker ticker, ticker1;           // Vytvorenie dvoch casovacov

// Premenne pre pocasie, vzduch a budik
uint16_t weatherIconID = 0;
uint8_t outNb = 1;
String tempExt[3], humiExt[3], presExt[3], aqiExt[3], aqExt[3], co2Ext[3], tvocExt[3];
char sunrise[6], sunset[6];
uint16_t y[8];
uint8_t h[8];
float minTemp, maxTemp;
unsigned long nextUpdateTime[2];           // Pole - Cas v sekundach pre nasledujucu aktualizaciu udajov {pocasia, Air-Monitora}
String alarmH, alarmM, alarmONOFF = "";    // Premenne pre budik (hodiny, minuty, zap-vyp)
//#define LED_BUILTIN 2 // definovanie GPIO 2 ako LED_BUILTIN


// 111111111111111111111111111111111111111111111111111111111111111111
// PRIKAZY VYKONAVANE IBA RAZ PRI STARTE PROGRAMU (zapnuti napajania)
void setup() {
  myNex.begin(9600);                  // Start myNex a nastavenie seriovej komunikacie - defaultne 9600 Bd pre Nextion displej
  pinMode(LED_BUILTIN, OUTPUT);       // Nastavenie vstavanej LED (namiesto LED_BUILTIN moze byt lubovolne cislo GPIO pinu)
  digitalWrite(LED_BUILTIN, LOW);     // Budik vypnuty - LED off

  if (!SPIFFS.begin(true)) {          // Start/pripojenie suboroveho systemu SPIFFS (true = ak treba, tak naformatuje)
    Serial.println("Pri pripajani suboroveho systemu SPIFFS sa vyskytla chyba!");
    return;                           // Navrat - dalej nepokracuje
  }

  Wire.begin(PIN_SDA, PIN_SCL);       // Start I2C zbernice na zadanych pinoch
  delay(100);                         // Pauza X milisekund

  bme680_begin();                     // Inicializacia BME680
  ccs811_begin();                     // Inicializacia CCS811
  wifiConnect();                      // Spojenie s WiFi sietou
  handleRequests();                   // Obsluha volani weboveho servera

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Ziskanie presneho casu z NTP servera

  updateDateTimeAndValues();                  // Zapis datumu a casu na displej, ziska udaje z internych senzorov
  ticker.attach(1, updateDateTimeAndValues);  // Nastavenie 1.casovaca - (opakuje kazdych X sekund, vykonavana metoda)

  sendDataInToDisplay();                      // Prvy zapis udajov z internych senzorov na displej
  ticker1.attach(60, sendDataInToDisplay);    // Nastavenie 2.casovaca - (opakuje kazdych X sekund, vykonavana metoda)

  getDataFromOWM();                           // Ziskanie prvych udajov o pocasi z OWM API servera
  getDataFromAirMonitor();                    // Ziskanie prvych udajov z Air-Monitorov
  delay(1000);                                // Pauza 1000 ms
  sendDataOutToDisplay();                     // Aktualizacia udajov z API servera/Air-Monitora na displeji

  nextUpdateTime[0] = getEpoch() + updateInterval[0];   // Cas v sekundach pre nasledujucu aktualizaciu udajov pocasia
  nextUpdateTime[1] = getEpoch() + updateInterval[1];   // Cas v sekundach pre nasledujucu aktualizaciu udajov z Air-Monitora
}

// &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// NEUSTALE SA OPAKUJUCE PRIKAZY po funkcii setup()
void loop() {
  myNex.NextionListen();                                // Prijima prikazy z Nextion displeja
  if (alarmONOFF == "") {                               // Vykona iba raz po spusteni/restarte
    trigger4();                                         // Pociatocne udaje pre budik prevzate z displeja
  }

  if (nextUpdateTime[0] <= getEpoch()) {                // Vykona ak uplynul interval pre aktualizaciu pocasia z externeho OWM API
    getDataFromOWM();                                   // AKTUALIZACIA UDAJOV O POCASI Z OWM API SERVERA
    sendDataOutToDisplay();                             // AKTUALIZACIA EXTERNYCH HODNOT Z OWM API SERVERA POCASIA NA DISPLEJI
    nextUpdateTime[0] = getEpoch() + updateInterval[0]; // Cas v sekundach pre nasledujucu aktualizaciu udajov pocasia
  }

  if (nextUpdateTime[1] <= getEpoch()) {                // Vykona ak uplynul interval pre aktualizaciu udajov z Air-Monitorov
    getDataFromAirMonitor();                            // AKTUALIZACIA UDAJOV Z AIR-MONITOROV
    sendExtValuesToDisplay();                           // Aktualizacia ext udajov z OWM a AM na displeji
    nextUpdateTime[1] = getEpoch() + updateInterval[1]; // Cas v sekundach pre nasledujucu aktualizaciu udajov z Air-Monitorov
  }
}
// ==================================================================


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
  //Serial.print("Inicializujem CCS811 ...");
  while (myCCS811.begin() == false) {         // Inicializacia CCS811
    //Serial.print(".");
    delay(500);                               // Pauza 500 ms
  }
  //Serial.println(" inicializované.");

  //Serial.print("Pripravujem CCS811 ...");   // Kontrola pripravenosti CCS811
  while (!myCCS811.dataAvailable()) {
    //Serial.print(".");
    delay(500);                               // Pauza 500 ms
  }
  //Serial.println(" pripravené.");
}

// AKTUALIZACIA UDAJOV Z INTERNYCH SENZOROV
void updateValuesIN() {
  getBME680values();                                    // Nacita aktualne hodnoty zo senzora BME680
  if (myCCS811.dataAvailable())                         // Vykona ak je senzor CCS811 pripraveny
  {
    myCCS811.setEnvironmentalData(Humidity, Temperature);    // Zaslanie aktualnej vlhkosti/teploty do senzora CCS811 - zlepsuje to presnost senzora
    myCCS811.readAlgorithmResults();                    // Pripravi CCS811 na citanie udajov
    CO2 = myCCS811.getCO2();                            // Zisti CO2
    TVOC = myCCS811.getTVOC();                          // Zisti TVOC
  }

  /*Spusti budik ak je budik zapnuty a suhlasi nastaveny cas*/
  if (alarmONOFF == "ON" && alarmH == time_hour.substring(0, 2) && alarmM == time_min && time_sec == "00") {
    digitalWrite(LED_BUILTIN, HIGH);                    // spustenie budika/alarmu - LED on
  }
}

// NACITA AKTUALNE HODNOTY ZO SENZORA BME680
void getBME680values() {
  if (! bme.performReading()) {                             // Zaciatok citania udajov z BME680 - 1.sposob
    Serial.println("Failed to perform reading :(");
    return;
  }
  //  unsigned long endTime = bme.beginReading();             // Zaciatok citania udajov z BME680 - 2.sposob
  //  if (endTime == 0) {
  //    Serial.println(F("Failed to begin reading :("));
  //    return;
  //  }
  //  if (!bme.endReading()) {
  //    Serial.println(F("Failed to complete reading :("));
  //    return;
  //  }
  Temperature = bme.temperature;                            // Zisti teplotu
  Humidity = bme.humidity;                                  // Zisti vlhkost
  Pressure = bme.pressure / 100.0;                          // Zisti tlak
  Altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);        // Zisti nadmorsku vysku
  Aqi = (bme.gas_resistance / 1000.0);                      // Zisti kvalitu vzduchu
  switch (int(Aqi)) {                                       // Zapise slovne hodnotenie kvality vzduchu do JSON objektu
    case 0 ... 50:
      Aq = String("Výborná");
      break;
    case 51 ... 100:
      Aq = String("Dobrá");
      break;
    case 101 ... 150:
      Aq = String("Priemerná");
      break;
    case 151 ... 200:
      Aq = String("Horšia");
      break;
    case 201 ... 300:
      Aq = String("Zlá");
      break;
    case 301 ... 1023:
      Aq = String("Veľmi zlá");
      break;
    default:
      Aq = String("-----");
  }
}

// VRATI AKTUALNY EPOCH CAS
unsigned long getEpoch() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

// AKTUALIZACIA DATUMU, CASU a UDAJOV z INTERNYCH SENZOROV
void updateDateTimeAndValues() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    //Serial.println("Nastala chyba pri aktualizacii casu!");
    return;
  }
  //Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  if (timeinfo.tm_sec % 2 == 0) time_hour = timeinfo.tm_hour < 10 ? "0" + String(timeinfo.tm_hour) + ":" : String(timeinfo.tm_hour) + ":";
  else time_hour = timeinfo.tm_hour < 10 ? "0" + String(timeinfo.tm_hour) + " " : String(timeinfo.tm_hour) + " ";
  time_min = timeinfo.tm_min < 10 ? "0" + String(timeinfo.tm_min) : String(timeinfo.tm_min);
  time_sec = timeinfo.tm_sec < 10 ? "0" + String(timeinfo.tm_sec) : String(timeinfo.tm_sec);
  sendTimeToDisplay();        // Aktualizacia casu na displeji

  if (date_lastday != String(timeinfo.tm_mday)) { // Vykona iba ak sa zmeni den
    date_day = timeinfo.tm_mday;
    date_lastday = date_day;  // Zapamata si aktualny den
    date_month = timeinfo.tm_mon + 1;
    date_year = timeinfo.tm_year + 1900;
    date_wday = timeinfo.tm_wday;
    sendDateToDisplay();      // Aktualizacia datumu na displeji
  }

  updateValuesIN();           // Aktualizacia udajov z internych senzorov
}

// AKTUALIZACIA DATUMU NA DISPLEJI
void sendDateToDisplay() {
  myNex.writeStr("date.txt", weekDays[date_wday.toInt()] + ", " + String(date_day) + ". " + months[date_month.toInt() - 1] + " " + String(date_year));
}

// AKTUALIZACIA CASU NA DISPLEJI
void sendTimeToDisplay() {
  myNex.writeStr("time_hour.txt", time_hour);
  if (time_lastmin != time_min) {               // Vykona iba ak sa zmeni minuta
    myNex.writeStr("time_min.txt", time_min);
    time_lastmin = time_min;
  }
  myNex.writeStr("time_sec.txt", time_sec);
}

// VRATI MIN HODNOTU z POLA 8 HODNOT do 100.0
float getMinimumValue(float* array) {
  float minValue = 100.0;
  for (int i = 0; i < 8; i++) {
    if (array[i] < minValue) {
      minValue = array[i];
    }
  }
  return minValue;
}

// VRATI MAX HODNOTU z POLA 8 HODNOT vacsich ako -100.0
float getMaximumValue(float* array) {
  float maxValue = -100.0;
  for (int i = 0; i < 8; i++) {
    if (array[i] > maxValue) {
      maxValue = array[i];
    }
  }
  return maxValue;
}

// AKTUALIZACIA UDAJOV Z AIR-MONITOROV
void getDataFromAirMonitor() {
  for (int mNb = 1; mNb <= airMonitorsNb ; mNb++) {
    String result = "";
    WiFiClient client;                                   // Vytvorenie objektu WiFiClient s nazvom client
    if (!client.connect(ServerHost[mNb], 80)) continue;  // Ak API server AM neodpoveda, tak ukonci pokus o spojenie a pokracuje dalsim AM
    client.print(String("GET ") + ServerUrl[mNb] + " HTTP/1.1\r\n" +   // REQUEST - Vyziada udaje z AirMonitora
                 "Host: " + ServerHost[mNb] + "\r\n" +
                 "Connection: close\r\n\r\n");
    unsigned long timeout = millis();    // Aktualny cas
    while (client.available() == 0) {    // Ak nie je aktivne spojenie s API serverom
      if (millis() - timeout > 5000) {   // Po 5-tich sekundach cakania na spojenie ho zrusi
        client.stop();
        Serial.println();
        Serial.println("Spojenie s AirMonitor zrusene po 5 sekundach cakania!");
        return;
      }
    }
    while (client.available()) result = client.readStringUntil('\r'); // RESPONSE - Nacita celu odpoved servera
    char jsonArray [result.length() + 1];
    result.toCharArray(jsonArray, sizeof(jsonArray));   // Prevod odpovede na pole znakov
    jsonArray[result.length() + 1] = '\0';              // Pridanie ukoncovacieho znaku
    DynamicJsonDocument root(5100);                     // Vytvori prazdny JSON objekt s nazvom root a zadanou velkostou
    deserializeJson(root, jsonArray);                   // Z pola znakov vytvori JSON objekt

    String temp = root["temp"];                         // Teplota vzduchu
    tempExt[mNb] = String(temp.toFloat(), 1);
    String humi = root["humi"];                         // Vlhkost vzduchu
    humiExt[mNb] = String(humi.toFloat(), 0);
    String pres = root["pres"];                         // Tlak
    presExt[mNb] = String(pres.toFloat(), 0);
    String aqi = root["aqi"];                           // AQI cislo
    aqiExt[mNb] = aqi;
    String aq = root["aq"];                             // AirQuality slovne
    aqExt[mNb] = aq;
    String co2 = root["co2"];                           // CO2
    co2Ext[mNb] = co2;
    String tvoc = root["tvoc"];                         // TVOC
    tvocExt[mNb] = tvoc;
  }
}

// AKTUALIZACIA UDAJOV O POCASI Z OWM API SERVERA POCASIA
void getDataFromOWM() {
  String result = "";
  WiFiClient client;                                   // Vytvorenie objektu WiFiClient s nazvom client
  if (!client.connect(ServerHost[0], 80)) return;      // Ak API server neodpoveda, tak ukonci pokus o spojenie
  client.print(String("GET ") + ServerUrl[0] + " HTTP/1.1\r\n" +   // REQUEST - Vyziada udaje z  API servera
               "Host: " + ServerHost[0] + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();   // Aktualny cas
  while (client.available() == 0) {   // Ak nie je aktivne spojenie s API serverom
    if (millis() - timeout > 5000) {  // Po 5-tich sekundach cakania na spojenie ho zrusi
      client.stop();
      return;
    }
  }
  while (client.available()) result = client.readStringUntil('\r'); // Nacita celu odpoved servera

  char jsonArray [result.length() + 1];
  result.toCharArray(jsonArray, sizeof(jsonArray));       // Prevod odpovede na pole znakov
  jsonArray[result.length() + 1] = '\0';                  // Pridanie ukoncovacieho znaku
  DynamicJsonDocument root(5100);                         // Vytvori prazdny JSON objekt s nazvom root a zadanou velkostou
  deserializeJson(root, jsonArray);                       // Z pola znakov vytvori JSON objekt

  String temp = root["current"]["temp"];                  // Teplota vzduchu
  tempExt[0] = String(temp.toFloat(), 1);
  String humi = root["current"]["humidity"];              // Vlhkost vzduchu
  humiExt[0] = String(humi.toFloat(), 0);
  String pres = root["current"]["pressure"];              // Tlak
  presExt[0] = String(pres.toFloat(), 0);
  time_t sunUp = root["current"]["sunrise"];              // Vychod slnka
  struct tm *tmUp = localtime(&sunUp);                    // Prevod na lokalny cas
  strftime(sunrise, sizeof(sunrise), "%H:%M", tmUp);      // Formatovanie casu vychodu slnka
  time_t sunDown = root["current"]["sunset"];             // Zapad slnka
  struct tm *tmDown = localtime(&sunDown);                // Prevod na lokalny cas
  strftime(sunset, sizeof(sunset), "%H:%M", tmDown);      // Formatovanie casu zapadu slnka
  String idString = root["current"]["weather"][0]["id"];  // Cislo ikony pre aktualne pocasie
  weatherIconID = idString.toInt();                       // Prevod zo Stringu na Integer typ

  String minDailyTempS[8] = {root["daily"][0]["temp"]["min"], root["daily"][1]["temp"]["min"], root["daily"][2]["temp"]["min"], root["daily"][3]["temp"]["min"], root["daily"][4]["temp"]["min"], root["daily"][5]["temp"]["min"], root["daily"][6]["temp"]["min"], root["daily"][7]["temp"]["min"]};  // String pole 8 min dennych teplot
  float minDailyTempF[8] = {minDailyTempS[0].toFloat(), minDailyTempS[1].toFloat(), minDailyTempS[2].toFloat(), minDailyTempS[3].toFloat(), minDailyTempS[4].toFloat(), minDailyTempS[5].toFloat(), minDailyTempS[6].toFloat(), minDailyTempS[7].toFloat()};                                           // Float pole 8 min dennych teplot
  minTemp = getMinimumValue(minDailyTempF);               // Min teplota z 8 min dennych teplot

  String maxDailyTempS[8] = {root["daily"][0]["temp"]["max"], root["daily"][1]["temp"]["max"], root["daily"][2]["temp"]["max"], root["daily"][3]["temp"]["max"], root["daily"][4]["temp"]["max"], root["daily"][5]["temp"]["max"], root["daily"][6]["temp"]["max"], root["daily"][7]["temp"]["max"]};  // String pole 8 max dennych teplot
  float maxDailyTempF[8] = {maxDailyTempS[0].toFloat(), maxDailyTempS[1].toFloat(), maxDailyTempS[2].toFloat(), maxDailyTempS[3].toFloat(), maxDailyTempS[4].toFloat(), maxDailyTempS[5].toFloat(), maxDailyTempS[6].toFloat(), maxDailyTempS[7].toFloat()};                                           // Float pole 8 max dennych teplot
  maxTemp = getMaximumValue(maxDailyTempF);               // Max teplota z 8 max dennych teplot

  float cPx = 55 / (maxTemp - minTemp);                   // Pocet pixlov na 1 stupen Celzia
  for (int i = 0; i < 8; i++) {                           // Cyklus od 0 do 7
    y[i] = 185 + ((maxTemp - maxDailyTempF[i]) * cPx);    // Pole 8-mich y-novych suradnic grafickych predpovedi
    h[i] = (maxDailyTempF[i] - minDailyTempF[i]) * cPx;   // Pole 8-mich vysok grafickych predpovedi
  }
}

// AKTUALIZACIA EXT UDAJOV Z OWM A MONITOROV VZDUCHU NA DISPLEJI NEXTION
void sendExtValuesToDisplay() {
  myNex.writeStr("tempowm.txt", tempExt[0]);              // Teplota vzduchu z OWM servera pocasia
  myNex.writeStr("humiowm.txt", humiExt[0]);              // Vlhkost vzduchu z OWM servera pocasia
  myNex.writeStr("presowm.txt", presExt[0]);              // Tlak z OWM servera pocasia
  myNex.writeStr("tempam.txt", tempExt[outNb]);           // Teplota vzduchu z Monitorov vzduchu
  myNex.writeStr("humiam.txt", humiExt[outNb]);           // Vlhkost vzduchu z Monitorov vzduchu
  myNex.writeStr("presam.txt", presExt[outNb]);           // Tlak z Monitorov vzduchu
  myNex.writeStr("aqiam.txt", aqiExt[outNb]);             // AQI z Monitorov vzduchu
  myNex.writeStr("co2am.txt", co2Ext[outNb]);             // CO2 z Monitorov vzduchu
  myNex.writeStr("tvocam.txt", tvocExt[outNb]);           // TVOC z Monitorov vzduchu
}

// AKTUALIZACIA GRAFU PREDPOVEDE POCASIA na displeji
void sendForecastGraphToDisplay() {
  myNex.writeStr("fill 21,185,127,54,891");                 // Vymazanie oblasti predpovede
  /*
    myNex.writeStr("line 0,124,320,124,YELLOW");              // Ciara pod datumom
    myNex.writeStr("draw 0,0,320,240,YELLOW");                // Obdlznik okolo displeja
    myNex.writeStr("draw 0,125,148,184,YELLOW");              // Obdlznik okolo budika
    myNex.writeStr("draw 148,125,230,240,YELLOW");            // Obdlznik okolo HOME a CO2/TVOC
    myNex.writeStr("draw 0,184,148,240,YELLOW");              // Obdlznik okolo predpovede
    myNex.writeStr("draw 230,125,320,240,YELLOW");            // Obdlznik okolo OUT
  */
  myNex.writeStr("maxtemp.txt", String(maxTemp, 0));          // Max teplota 7 dnovej predpovede
  myNex.writeStr("mintemp.txt", String(minTemp, 0));          // Min teplota 7 dnovej predpovede
  myNex.writeStr("fill 21," + String(y[0]) + ",15," + String(h[0]) + ",YELLOW");  // Aktualny den
  myNex.writeStr("fill 37," + String(y[1]) + ",15," + String(h[1]) + ",YELLOW");  // +1 den
  myNex.writeStr("fill 53," + String(y[2]) + ",15," + String(h[2]) + ",YELLOW");  // +2 den
  myNex.writeStr("fill 69," + String(y[3]) + ",15," + String(h[3]) + ",YELLOW");  // +3 den
  myNex.writeStr("fill 85," + String(y[4]) + ",15," + String(h[4]) + ",YELLOW");  // +4 den
  myNex.writeStr("fill 101," + String(y[5]) + ",15," + String(h[5]) + ",YELLOW"); // +5 den
  myNex.writeStr("fill 117," + String(y[6]) + ",15," + String(h[6]) + ",YELLOW"); // +6 den
  myNex.writeStr("fill 133," + String(y[7]) + ",15," + String(h[7]) + ",YELLOW"); // +7 den
}

// AKTUALIZACIA EXTERNYCH HODNOT Z OWM API SERVERA POCASIA NA DISPLEJI
void sendDataOutToDisplay() {
  myNex.writeStr("weathericon.txt", getWeatherIcon(weatherIconID));  // Ikona aktualneho pocasia
  sendExtValuesToDisplay();                                          // Aktualizacia ext udajov z OWM a AM na displeji
  myNex.writeStr("sunrise.txt", String(sunrise));                    // Vychod slnka
  myNex.writeStr("sunset.txt", String(sunset));                      // Zapad slnka
  sendForecastGraphToDisplay();                                      // AKTUALIZACIA GRAFU PREDPOVEDE POCASIA na displeji
}

// AKTUALIZACIA NAMERANYCH INTERNYCH HODNOT NA DISPLEJI
void sendDataInToDisplay() {
  myNex.writeStr("tempin.txt", String(Temperature, 1));   // Interna teplota
  myNex.writeStr("humiin.txt", String(Humidity, 0));      // Interna vlhkost
  myNex.writeStr("presin.txt", String(Pressure, 0));      // Interny tlak
  myNex.writeStr("aqiin.txt", String(Aqi, 0));            // Interny index kvality vzduchu
  myNex.writeStr("co2in.txt", String(CO2));               // Interne CO2
  myNex.writeStr("tvocin.txt", String(TVOC));             // Interne TVOC
}

// VRATI String REPREZENTUJUCI IKONU AKTUALNEHO POCASIA v meteo fonte
String getWeatherIcon(int id) {
  String icon = ")";
  switch (id)   // Podla hodnoty id vrati ikonu pocasia
  {
    case 800: icon = "B"; break;  //drawClearWeather
    case 801: icon = "B"; break;  //drawFewClouds
    case 802: icon = "B"; break;  //drawFewClouds
    case 803: icon = "N"; break;  //drawCloud
    case 804: icon = "N"; break;  //drawCloud

    case 200: icon = "P"; break;  //drawThunderstorm
    case 201: icon = "P"; break;  //drawThunderstorm
    case 202: icon = "P"; break;  //drawThunderstorm
    case 210: icon = "P"; break;  //drawThunderstorm
    case 211: icon = "P"; break;  //drawThunderstorm
    case 212: icon = "P"; break;  //drawThunderstorm
    case 221: icon = "P"; break;  //drawThunderstorm
    case 230: icon = "P"; break;  //drawThunderstorm
    case 231: icon = "P"; break;  //drawThunderstorm
    case 232: icon = "P"; break;  //drawThunderstorm

    case 300: icon = "Q"; break;  //drawLightRain
    case 301: icon = "Q"; break;  //drawLightRain
    case 302: icon = "Q"; break;  //drawLightRain
    case 310: icon = "Q"; break;  //drawLightRain
    case 311: icon = "Q"; break;  //drawLightRain
    case 312: icon = "Q"; break;  //drawLightRain
    case 313: icon = "Q"; break;  //drawLightRain
    case 314: icon = "Q"; break;  //drawLightRain
    case 321: icon = "Q"; break;  //drawLightRain

    case 500: icon = "T"; break;  //drawLightRainWithSunOrMoon
    case 501: icon = "T"; break;  //drawLightRainWithSunOrMoon
    case 502: icon = "T"; break;  //drawLightRainWithSunOrMoon
    case 503: icon = "T"; break;  //drawLightRainWithSunOrMoon
    case 504: icon = "T"; break;  //drawLightRainWithSunOrMoon
    case 511: icon = "Q"; break;  //drawLightRain
    case 520: icon = "T"; break;  //drawModerateRain
    case 521: icon = "T"; break;  //drawModerateRain
    case 522: icon = "R"; break;  //drawHeavyRain
    case 531: icon = "R"; break;  //drawHeavyRain

    case 600: icon = "V"; break;  //drawLightSnowfall
    case 601: icon = "W"; break;  //drawModerateSnowfall
    case 602: icon = "X"; break;  //drawHeavySnowfall
    case 611: icon = "V"; break;  //drawLightSnowfall
    case 612: icon = "V"; break;  //drawLightSnowfall
    case 615: icon = "V"; break;  //drawLightSnowfall
    case 616: icon = "V"; break;  //drawLightSnowfall
    case 620: icon = "V"; break;  //drawLightSnowfall
    case 621: icon = "W"; break;  //drawModerateSnowfall
    case 622: icon = "X"; break;  //drawHeavySnowfall

    case 701: icon = "M"; break;  //drawFog
    case 711: icon = "M"; break;  //drawFog
    case 721: icon = "M"; break;  //drawFog
    case 731: icon = "M"; break;  //drawFog
    case 741: icon = "M"; break;  //drawFog
    case 751: icon = "M"; break;  //drawFog
    case 761: icon = "M"; break;  //drawFog
    case 762: icon = "M"; break;  //drawFog
    case 771: icon = "M"; break;  //drawFog
    case 781: icon = "M"; break;  //drawFog

    default: icon = ")";
  }

  return icon;
}

//TRIGGER1 NEXTION DISPLEJA - PREPINA ZDROJE VONKAJSICH UDAJOV
void trigger1() {
  if (outNb == 1) {                          // Ak bol zvoleny AM1 SERVER tak prepne na AM 2
    myNex.writeStr("amname.txt", "AM-2");   // Aktualizacia nazvu servera/udajov
    outNb = 2;                               // Vyber udajov: 2=AM 2
  }
  else {                                     // Ak bol zvoleny AirMonitor 2 tak prepne na AM 1
    myNex.writeStr("amname.txt", "AM-1");    // Aktualizacia nazvu servera/udajov
    outNb = 1;                               // Vyber udajov: 1=AM 1
  }
  sendExtValuesToDisplay();                  // Aktualizacia ext udajov z OWM a AM na displeji
}

//TRIGGER2 NEXTION DISPLEJA - obnova grafickej predpovede teplot
void trigger2() {
  sendForecastGraphToDisplay();              // AKTUALIZACIA GRAFU PREDPOVEDE POCASIA na displeji
}

//TRIGGER3 NEXTION DISPLEJA - aktualizacia IP a MAC adresy pri prechode na page1
void trigger3() {
  IPAddress ip = WiFi.localIP();
  String ipStr = (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]));
  myNex.writeStr("page1.ip.txt", ipStr);    // Pridelená IP adresa ESP32
  String macStr = WiFi.macAddress();
  myNex.writeStr("page1.mac.txt", macStr);  // MAC adresa ESP32
}

//TRIGGER4 NEXTION DISPLEJA - aktualizacia casu a zapnutia budika
void trigger4() {
  if (digitalRead(LED_BUILTIN) == HIGH) {              // Vykona ak budik prave zvoni
    digitalWrite(LED_BUILTIN, LOW);                    // Vypnutie budika/alarmu
    myNex.writeStr("alarmh.txt", alarmH);
    myNex.writeStr("alarmm.txt", alarmM);
  }
  else                                                 // Inak iba prevezme nove/aktualne udaje nastavenia budika
  {
    alarmH = myNex.readStr("alarmh.txt");
    alarmM = myNex.readStr("alarmm.txt");
    alarmONOFF = myNex.readStr("alarmonoff.txt");
  }
}

// Spojenie s WIFI sietou
void wifiConnect() {
  Serial.println();
  Serial.print("Spajam s WIFI sietou ...");
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Async_AutoConnect");
  //ESPAsync_wifiManager.resetSettings();    // Vymazanie WiFi nastaveni
  ESPAsync_wifiManager.autoConnect("CwaStation");
}

// START A OBSLUHA ASYNCHRONNEHO WEBOVEHO SERVERA A OTA UPDATE
void handleRequests() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  webServer.on("/my.css", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/my.css", "text/css");
  });
  webServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest * request) { //pre nahranie suborov v adresari Data spustit: "ESP32 Sketch Data Upload" v zalozke "Nastroje" (...Documents/Arduino/tools/ESP32FS/tool/esp32fs.jar)
    request->send(SPIFFS, "/favicon.ico", "image/ico"); // moze byt ico, png ...
  });
  webServer.onNotFound(handle_NotFound);

  AsyncElegantOTA.begin(&webServer);                    // Start AsyncElegantOTA ... http://IPadresaESP32/update
  webServer.begin();                                    // Start weboveho servera
  Serial.println();
  Serial.println("Webovy server je nastartovany, OTA update funkcny.");
  Serial.println();
}

// OBSLUHA NEEXISTUJUCEJ ADRESY WEBOVEHO SERVERA
void handle_NotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Nenajdene!!!");
}

// DOPLNENIE HODNOT PREMENNYCH NA URCENE MIESTA V HTML KODE WEBOVEJ STRANKY
String processor(const String& var) {
  //Serial.println(var);
  if (var == "tempIN") {
    String insert = String(Temperature, 1);
    return insert;
  }
  else if (var == "humiIN") {
    String insert = String(Humidity, 0);
    return insert;
  }
  else if (var == "presIN") {
    String insert = String(Pressure, 0);
    return insert;
  }
  else if (var == "aqiIN") {
    String insert = String(Aqi, 0);
    return insert;
  }
  else if (var == "aqIN") {
    return Aq;
  }
  else if (var == "co2IN") {
    String insert = String(CO2);
    return insert;
  }
  else if (var == "tvocIN") {
    String insert = String(TVOC);
    return insert;
  }
  else if (var == "tempAM1") {
    return tempExt[1];
  }
  else if (var == "humiAM1") {
    return humiExt[1];
  }
  else if (var == "presAM1") {
    return presExt[1];
  }
  else if (var == "aqiAM1") {
    return aqiExt[1];
  }
  else if (var == "aqAM1") {
    return aqExt[1];
  }
  else if (var == "co2AM1") {
    return co2Ext[1];
  }
  else if (var == "tvocAM1") {
    return tvocExt[1];
  }
  else if (var == "tempAM2") {
    return tempExt[2];
  }
  else if (var == "humiAM2") {
    return humiExt[2];
  }
  else if (var == "presAM2") {
    return presExt[2];
  }
  else if (var == "aqiAM2") {
    return aqiExt[2];
  }
  else if (var == "aqAM2") {
    return aqExt[2];
  }
  else if (var == "co2AM2") {
    return co2Ext[2];
  }
  else if (var == "tvocAM2") {
    return tvocExt[2];
  }
  else if (var == "tempOWM") {
    return tempExt[0];
  }
  else if (var == "humiOWM") {
    return humiExt[0];
  }
  else if (var == "presOWM") {
    return presExt[0];
  }
  else if (var == "sunrise") {
    String insert = String(sunrise);
    return insert;
  }
  else if (var == "sunset") {
    String insert = String(sunset);
    return insert;
  }

  return String();
}
