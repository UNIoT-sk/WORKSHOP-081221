/* I2C */
const uint8_t PIN_SDA = 21, PIN_SCL = 22;     // SDA, SCL piny mikrokontrolera

/* OWM API servera pocasia */
const char* WeatherHost = "api.openweathermap.org";        // OWM API adresa servera pocasia
const String Latitude = "XXXXXXXXXXXXXXXXX";               // Vasa zemepisna sirka
const String Longitude = "XXXXXXXXXXXXXXXXX";              // Vasa zemepisna dlzka
const String APIKEY = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";  // Vas api_key (ziskate zdarma po zaregistrovani sa na API serveri openweathermap.org)
const String WeatherUrl = "/data/2.5/onecall?lat=" + Latitude + "&lon=" + Longitude + "&units=metric&exclude=minutely,hourly&lang=sk&appid=" + APIKEY;

/* Adresy: 0 = OWM API servera pocasia, 1 = Monitora vzduchu 1, 2 = Monitora vzduchu 2, ... */
const uint8_t airMonitorsNb = 2;                                            // Pocet Monitorov vzduchu
const char* ServerHost[] = {WeatherHost, "192.168.1.101", "192.168.1.102"}; // Pole - API adresy
const String ServerUrl[] = {WeatherUrl, "/api", "/api"};                    // Pole - URL 

/* Premenne pre update pocasia z OWM a Monitorov vzduchu */
uint16_t updateInterval[] = {1800, 600};                  // Pole - Update interval {Pocasie z OWM, Monitory vzduchu} v sekundach
