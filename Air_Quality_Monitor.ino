// Libraries
#include <WiFi.h>
#include <IotWebConf.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "Free_Fonts.h" // Include the header file attached to this sketch


#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <SPI.h>


// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "AirQuality";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "AirQuality";

DNSServer dnsServer;
WebServer server(80);

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

// WiFi parameters
//#define WLAN_SSID       "DT_LAB"
//#define WLAN_PASS       "fthu@050318"

// Adafruit IO
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "siddheshCG"
#define AIO_KEY         "5e0217bf2f53446983193bc050996163"  // Obtained from account info on io.adafruit.com

#define TFT_GREY 0x5AEB // New colour

TFT_eSPI tft = TFT_eSPI();  // Invoke library


/* Connect the DSM501 sensor as follow 
 * https://www.elektronik.ropla.eu/pdf/stock/smy/dsm501.pdf
 * 1 green vert - Not used
 * 2 yellow jaune - Vout2 - 1 microns (PM1.0)
 * 3 white blanc - Vcc
 * 4 red rouge - Vout1 - 2.5 microns (PM2.5)
 * 5 black noir - GND
*/
#define DUST_SENSOR_DIGITAL_PIN_PM10  17        // DSM501 Pin 2 of DSM501 (jaune / Yellow)
#define DUST_SENSOR_DIGITAL_PIN_PM25  16       // DSM501 Pin 4 (rouge / red) 
#define COUNTRY                       3        // 0. France, 1. Europe, 2. USA/China
#define EXCELLENT                     "Excellent"
#define GOOD                          "Good"
#define ACCEPTABLE                    "Satisfactory"
#define MODERATE                      "Moderate"
#define HEAVY                         "Poor"
#define SEVERE                        "Very Poor"
#define HAZARDOUS                     "Severe"

unsigned long   duration;
unsigned long   starttime;
unsigned long   endtime;
unsigned long   lowpulseoccupancy = 0;
float           ratio = 0;
unsigned long   SLEEP_TIME    = 2 * 1000;       // Sleep time between reads (in milliseconds)
unsigned long   sampletime_ms = 5 * 60 * 100;  // Durée de mesure - sample time (ms)

struct structAQI{
  // variable enregistreur - recorder variables
  unsigned long   durationPM10;
  unsigned long   lowpulseoccupancyPM10 = 0;
  unsigned long   durationPM25;
  unsigned long   lowpulseoccupancyPM25 = 0;
  unsigned long   starttime;
  unsigned long   endtime;
  // Sensor AQI data
  float         concentrationPM25 = 0;
  float         concentrationPM10  = 0;
  int           AqiPM10            = -1;
  int           AqiPM25            = -1;
  // Indicateurs AQI - AQI display
  int           AQI                = 0;
  String        AqiString          = "";
  int           AqiColor           = 0;
};
struct structAQI AQI;

String sensor_line1;
String sensor_line2;
String sensor_line3;
// Last known good values for air quality and
// gas concentration (through air conductivity)
String prevQuality = "";
int prevConductivity = 0;
int prevGas = -1;
bool need_redraw = false;
int gas=0;
int gasReadings=0;

int prevPM10=-1;
int prevPM25=-1;
int pm10_level=1;
int pm25_level=1;

int gas2 = 34; //mq7
float m1 = -0.6527; //Slope 
float b1 = 1.30; //Y-Intercept 
float R01 = 7.22; //Sensor Resistance
double co_ppm=0;
int co_level=1;
double prevCO=1;
WiFiClient client;
 
// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
 
/****************************** Feeds ***************************************/
 
Adafruit_MQTT_Publish PM10 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/PM10");
Adafruit_MQTT_Publish PM25 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/PM25");
Adafruit_MQTT_Publish AirQualityIndex = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/AirQualityIndex");
Adafruit_MQTT_Publish aqiMessage = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/aqiMessage");
Adafruit_MQTT_Publish co = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/co");


void updateAQILevel(){
  AQI.AQI = AQI.AqiPM10;
}

void updateAQI() {
  // Actualise les mesures - update measurements
  AQI.endtime = millis();
  float ratio = AQI.lowpulseoccupancyPM10 / (sampletime_ms * 10.0);
  float concentration = 1.1 * pow( ratio, 3) - 3.8 *pow(ratio, 2) + 520 * ratio + 0.62; // using spec sheet curve
  if ( sampletime_ms < 3600000 ) { concentration = concentration * ( sampletime_ms / 3600000.0 ); }
  AQI.lowpulseoccupancyPM10 = 0;
  AQI.concentrationPM10 = concentration;
  
  ratio = AQI.lowpulseoccupancyPM25 / (sampletime_ms * 10.0);
  concentration = 1.1 * pow( ratio, 3) - 3.8 *pow(ratio, 2) + 520 * ratio + 0.62;
  if ( sampletime_ms < 3600000 ) { concentration = concentration * ( sampletime_ms / 3600000.0 ); }
  AQI.lowpulseoccupancyPM25 = 0;
  AQI.concentrationPM25 = concentration;

  Serial.print("Concentrations => PM2.5: "); Serial.print(AQI.concentrationPM25); Serial.print(" | PM10: "); Serial.println(AQI.concentrationPM10);
  
  AQI.starttime = millis();
      
  // Actualise l'AQI de chaque capteur - update AQI for each sensor 
  if ( COUNTRY == 0 ) {
    // France
    AQI.AqiPM25 = getATMO( 0, AQI.concentrationPM25 );
    AQI.AqiPM10 = getATMO( 1, AQI.concentrationPM10 );
  } else if ( COUNTRY == 1 ) {
    // Europe
    AQI.AqiPM25 = getACQI( 0, AQI.concentrationPM25 );
    AQI.AqiPM10 = getACQI( 1, AQI.concentrationPM10 );
  } else {
    // USA / China
    AQI.AqiPM25 = getAQI( 0, AQI.concentrationPM25 );
    AQI.AqiPM10 = getAQI( 0, AQI.concentrationPM10 );
  }

  // Actualise l'indice AQI - update AQI index
  updateAQILevel();
  updateAQIDisplay();
  
  Serial.print("AQIs => PM25: "); Serial.print(AQI.AqiPM25); Serial.print(" | PM10: "); Serial.println(AQI.AqiPM10);
  Serial.print(" | AQI: "); Serial.println(AQI.AQI); Serial.print(" | Message: "); Serial.println(AQI.AqiString);
  

}

void setup() {
  
  // put your setup code here, to run once:
  Serial.begin(115200);
   tft.init();
  tft.setRotation(3);
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.setFreeFont(FF18);
  tft.setTextColor(TFT_RED);
  tft.drawString("Air Quality Monitor", 5, 20);
  delay(2000);
  pinMode(DUST_SENSOR_DIGITAL_PIN_PM10,INPUT);
  pinMode(DUST_SENSOR_DIGITAL_PIN_PM25,INPUT);
  // Connect to WiFi access point.
  Serial.println(); Serial.println();
  delay(10);
  Serial.print(F("Connecting to "));
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.setFreeFont(FF18);
  tft.setTextColor(TFT_RED);
  tft.drawString("Connecting to WiFi", 5, 20);
      
 /* Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
*/
// -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });

  Serial.println("Ready.");
  Serial.println(F("WiFi connected"));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.setFreeFont(FF18);
  tft.setTextColor(TFT_RED);
  tft.drawString("Preheating Sensors", 5, 20);
  tft.drawString(".", 275, 19);
  tft.drawString(".", 275, 20);
  // wait 60s for DSM501 to warm up
  for (int i = 1; i <= 60; i++)
  {
    delay(1000); // 1s
    Serial.print(i);
    Serial.println(" s (wait 60s for DSM501 to warm up)");
    tft.drawString(". ", i+215, 20);tft.drawString(". ", i+215, 19);
  }
  
  Serial.println("Ready!");
  // connect to adafruit io
  mqtt.connect();
  displayLcd();
  AQI.starttime = millis();
  //timer.setInterval(sampletime_ms, updateAQI);
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 01 Minimal</title></head><body>Hello world!";
  s += "Go to <a href='config'>configure page</a> to change settings.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void loop() {
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
   // ping adafruit io a few times to make sure we remain connected
  if(! mqtt.ping(3)) {
    // reconnect to adafruit io
    if(! mqtt.connected())
      mqtt.connect();
  }
  detectGas();
  detectCO();
  AQI.lowpulseoccupancyPM10 += pulseIn(DUST_SENSOR_DIGITAL_PIN_PM10, LOW);
  AQI.lowpulseoccupancyPM25 += pulseIn(DUST_SENSOR_DIGITAL_PIN_PM25, LOW);
   if((millis()-AQI.starttime) > sampletime_ms)//if the sampel time == 30s
      {
       updateAQI();
       
       if (! PM25.publish(AQI.AqiPM25)) Serial.println(F("Failed to publish PM25"));
       else Serial.println(F("PM25 published!"));

       if (! PM10.publish(AQI.AqiPM10)) Serial.println(F("Failed to publish PM10"));
       else Serial.println(F("PM10 published!"));

       if (! AirQualityIndex.publish(AQI.AQI)) Serial.println(F("Failed to publish AirQualityIndex"));
       else Serial.println(F("AirQualityIndex published!"));

       if (! aqiMessage.publish(AQI.AqiString.c_str())) Serial.println(F("Failed to publish aqiMessage"));
       else Serial.println(F("aqiMessage published!"));

       if (! co.publish(co_ppm)) Serial.println(F("Failed to publish co ppm"));
       else Serial.println(F("co ppm published!"));
      }
       
     tft.setTextColor(TFT_BLACK);
     tft.setFreeFont(FF18);
     if(prevPM10 != AQI.AqiPM10){
      tft.setTextColor(TFT_WHITE);
      tft.drawString(String(prevPM10), 120, 86);
      tft.setTextColor(TFT_BLACK);
      tft.drawString(String(AQI.AqiPM10), 120, 86);
     }
     if(prevPM25 != AQI.AqiPM25){
      tft.setTextColor(TFT_WHITE);
      tft.drawString(String(prevPM25), 120, 116);
      tft.setTextColor(TFT_BLACK);
      tft.drawString(String(AQI.AqiPM25), 120, 116);
     }
     if(prevCO != co_ppm){
      tft.setTextColor(TFT_WHITE);
      tft.drawString(String(prevCO), 110, 146);
      tft.setTextColor(TFT_BLACK);
      tft.drawString(String(co_ppm), 110, 146);
     }
     
     tft.setFreeFont(FF23);
     if(AQI.AqiString=="Good"){
       tft.fillRect(6, 200, 309, 35, TFT_GREEN);
       tft.drawString(AQI.AqiString, 110, 205);
     }
     if(AQI.AqiString=="Satisfactory"){
       tft.fillRect(6, 200, 309, 35, TFT_YELLOW);
       tft.drawString(AQI.AqiString, 45, 205);
     }
     if(AQI.AqiString=="Moderate"){
       tft.fillRect(6, 200, 309, 35, TFT_ORANGE);
       tft.drawString(AQI.AqiString, 55, 205);
     }
     if(AQI.AqiString=="Poor"){
       tft.fillRect(6, 200, 309, 35, TFT_RED);
       tft.drawString(AQI.AqiString, 110, 205);
     }
     if(AQI.AqiString=="Very Poor"){
       tft.fillRect(6, 200, 309, 35, TFT_RED);
       tft.drawString(AQI.AqiString, 55, 205);
     }
     if(AQI.AqiString=="Severe"){
       tft.fillRect(6, 200, 310, 35, TFT_WHITE);
       tft.drawString(AQI.AqiString, 75, 205);
     }
     
     prevPM10=AQI.AqiPM10;
     prevPM25=AQI.AqiPM25;
     prevCO=co_ppm;
     pm10_level=getPM10(AQI.AqiPM10);
     pm25_level=getPM25(AQI.AqiPM25);
     co_level=getCO(co_ppm);
     tft.setFreeFont(FF18);
     switch (pm10_level) {
        case 1:
        tft.fillRect(181, 81, 134, 29, TFT_GREEN);
        tft.drawString("Good", 205, 87);
        break;
        case 2:
        tft.fillRect(181, 81, 134, 29, TFT_YELLOW);
        tft.drawString("Moderate", 195, 87);
        break;
        case 3:
        tft.fillRect(181, 81, 134, 29, TFT_ORANGE);
        tft.drawString("Poor", 205, 87);
        break;
        case 4:
        tft.fillRect(181, 81, 134, 29, TFT_RED);
        tft.drawString("Very Poor", 195, 87);
        break;
        case 5:
        tft.fillRect(181, 81, 134, 29, TFT_RED);
        tft.drawString("Severe", 200, 87);
        break;
     }  
     switch (pm25_level) {
        case 1:
        tft.fillRect(181, 111, 134, 29, TFT_GREEN);
        tft.drawString("Good", 205, 117);
        break;
        case 2:
        tft.fillRect(181, 111, 134, 29, TFT_YELLOW);
        tft.drawString("Moderate", 195, 117);
        break;
        case 3:
        tft.fillRect(181, 111, 134, 29, TFT_ORANGE);
        tft.drawString("Poor",205, 117);
        break;
        case 4:
        tft.fillRect(181, 111, 134, 29, TFT_RED);
        tft.drawString("Very Poor", 190, 117);
        break;
        case 5:
        tft.fillRect(181, 111, 134, 29, TFT_RED);
        tft.drawString("Severe", 200, 117);
        break;
     }  

     switch (co_level) {
        case 1:
        tft.fillRect(181, 141, 134, 29, TFT_GREEN);
        tft.drawString("Good", 205, 146);
        break;
        case 2:
        tft.fillRect(181, 141, 134, 29, TFT_YELLOW);
        tft.drawString("Moderate", 195, 146);
        break;
        case 3:
        tft.fillRect(181, 141, 134, 29, TFT_ORANGE);
        tft.drawString("Poor",205, 146);
        break;
        case 4:
        tft.fillRect(181, 141, 134, 29, TFT_RED);
        tft.drawString("Very Poor", 190, 146);
        break;
        case 5:
        tft.fillRect(181, 141, 134, 29, TFT_RED);
        tft.drawString("Severe", 200, 146);
        break;
     }  
     
  //timer.run(); 
  
}

/*
 * Calcul l'indice de qualité de l'air français ATMO
 * Calculate French ATMO AQI indicator
 */
int getATMO( int sensor, float density ){
  if ( sensor == 0 ) { //PM2,5
    if ( density <= 11 ) {
      return 1; 
    } else if ( density > 11 && density <= 24 ) {
      return 2;
    } else if ( density > 24 && density <= 36 ) {
      return 3;
    } else if ( density > 36 && density <= 41 ) {
      return 4;
    } else if ( density > 41 && density <= 47 ) {
      return 5;
    } else if ( density > 47 && density <= 53 ) {
      return 6;
    } else if ( density > 53 && density <= 58 ) {
      return 7;
    } else if ( density > 58 && density <= 64 ) {
      return 8;
    } else if ( density > 64 && density <= 69 ) {
      return 9;
    } else {
      return 10;
    }
  } else {
    if ( density <= 6 ) {
      return 1; 
    } else if ( density > 6 && density <= 13 ) {
      return 2;
    } else if ( density > 13 && density <= 20 ) {
      return 3;
    } else if ( density > 20 && density <= 27 ) {
      return 4;
    } else if ( density > 27 && density <= 34 ) {
      return 5;
    } else if ( density > 34 && density <= 41 ) {
      return 6;
    } else if ( density > 41 && density <= 49 ) {
      return 7;
    } else if ( density > 49 && density <= 64 ) {
      return 8;
    } else if ( density > 64 && density <= 79 ) {
      return 9;
    } else {
      return 10;
    }  
  }
}

void updateAQIDisplay(){
  /*
   * 1 EXCELLENT                    
   * 2 GOOD                         
   * 3 ACCEPTABLE               
   * 4 MODERATE            
   * 5 HEAVY               
   * 6 SEVERE
   * 7 HAZARDOUS
   */
  if ( COUNTRY == 0 ) {
    // Système ATMO français - French ATMO AQI system 
    switch ( AQI.AQI) {
      case 10: 
        AQI.AqiString = SEVERE;
        break;
      case 9:
        AQI.AqiString = HEAVY;
        break;
      case 8:
        AQI.AqiString = HEAVY;
        break;  
      case 7:
        AQI.AqiString = MODERATE;
        break;
      case 6:
        AQI.AqiString = MODERATE;
        break;   
      case 5:
        AQI.AqiString = ACCEPTABLE;
        break;
      case 4:
        AQI.AqiString = GOOD;
        break;
      case 3:
        AQI.AqiString = GOOD;
        break;
      case 2:
        AQI.AqiString = EXCELLENT;
        break;
      case 1:
        AQI.AqiString = EXCELLENT;
        break;           
      }
  } else if ( COUNTRY == 1 ) {
    // European CAQI
    switch ( AQI.AQI) {
      case 25: 
        AQI.AqiString = GOOD;
        break;
      case 50:
        AQI.AqiString = ACCEPTABLE;
        break;
      case 75:
        AQI.AqiString = MODERATE;
        break;
      case 100:
        AQI.AqiString = HEAVY;
        break;         
      default:
        AQI.AqiString = SEVERE;
      }  
  } else if ( COUNTRY == 2 ) {
    // USA / CN
    if ( AQI.AQI <= 50 ) {
        AQI.AqiString = GOOD;
    } else if ( AQI.AQI > 50 && AQI.AQI <= 100 ) {
        AQI.AqiString = ACCEPTABLE;
    } else if ( AQI.AQI > 100 && AQI.AQI <= 150 ) {
        AQI.AqiString = MODERATE;
    } else if ( AQI.AQI > 150 && AQI.AQI <= 200 ) {
        AQI.AqiString = HEAVY;
    } else if ( AQI.AQI > 200 && AQI.AQI <= 300 ) {  
        AQI.AqiString = SEVERE;
    } else {    
       AQI.AqiString = HAZARDOUS;
    }  
  }
  else if ( COUNTRY == 3 ) {
    // USA / CN
    if ( AQI.AQI <= 50 ) {
        AQI.AqiString = GOOD;
    } else if ( (AQI.AQI > 50 && AQI.AQI <= 100) || gas <= 500 ) {
        AQI.AqiString = ACCEPTABLE;
    } else if ( (AQI.AQI > 100 && AQI.AQI <= 200) || (gas > 700 && gas <= 900) ) {
        AQI.AqiString = MODERATE;
    } else if ( (AQI.AQI > 200 && AQI.AQI <= 300) || (gas > 1100 && gas <= 1100) ) {
        AQI.AqiString = HEAVY;
    } else if ( (AQI.AQI > 300 && AQI.AQI <= 400) || (gas > 1300 && gas <= 1300) ) {  
        AQI.AqiString = SEVERE;
    } else {    
       AQI.AqiString = HAZARDOUS;
    }  
  }
}
/*
 * CAQI Européen - European CAQI level 
 * source : http://www.airqualitynow.eu/about_indices_definition.php
 */
 
int getACQI( int sensor, float density ){  
  if ( sensor == 0 ) {  //PM2,5
    if ( density == 0 ) {
      return 0; 
    } else if ( density <= 15 ) {
      return 25 ;
    } else if ( density > 15 && density <= 30 ) {
      return 50;
    } else if ( density > 30 && density <= 55 ) {
      return 75;
    } else if ( density > 55 && density <= 110 ) {
      return 100;
    } else {
      return 150;
    }
  } else {              //PM10
    if ( density == 0 ) {
      return 0; 
    } else if ( density <= 25 ) {
      return 25 ;
    } else if ( density > 25 && density <= 50 ) {
      return 50;
    } else if ( density > 50 && density <= 90 ) {
      return 75;
    } else if ( density > 90 && density <= 180 ) {
      return 100;
    } else {
      return 150;
    }
  }
}

/*
 * AQI formula: https://en.wikipedia.org/wiki/Air_Quality_Index#United_States
 * Arduino code https://gist.github.com/nfjinjing/8d63012c18feea3ed04e
 * On line AQI calculator https://www.airnow.gov/index.cfm?action=resources.conc_aqi_calc
 */
float calcAQI(float I_high, float I_low, float C_high, float C_low, float C) {
  return (I_high - I_low) * (C - C_low) / (C_high - C_low) + I_low;
}

int getAQI(int sensor, float density) {
  int d10 = (int)(density * 10);
  if ( sensor == 0 ) {
    if (d10 <= 0) {
      return 0;
    }
    else if(d10 <= 120) {
      return calcAQI(50, 0, 120, 0, d10);
    }
    else if (d10 <= 354) {
      return calcAQI(100, 51, 354, 121, d10);
    }
    else if (d10 <= 554) {
      return calcAQI(150, 101, 554, 355, d10);
    }
    else if (d10 <= 1504) {
      return calcAQI(200, 151, 1504, 555, d10);
    }
    else if (d10 <= 2504) {
      return calcAQI(300, 201, 2504, 1505, d10);
    }
    else if (d10 <= 3504) {
      return calcAQI(400, 301, 3504, 2505, d10);
    }
    else if (d10 <= 5004) {
      return calcAQI(500, 401, 5004, 3505, d10);
    }
    else if (d10 <= 10000) {
      return calcAQI(1000, 501, 10000, 5005, d10);
    }
    else {
      return 1001;
    }
  } else {
    if (d10 <= 0) {
      return 0;
    }
    else if(d10 <= 540) {
      return calcAQI(50, 0, 540, 0, d10);
    }
    else if (d10 <= 1540) {
      return calcAQI(100, 51, 1540, 541, d10);
    }
    else if (d10 <= 2540) {
      return calcAQI(150, 101, 2540, 1541, d10);
    }
    else if (d10 <= 3550) {
      return calcAQI(200, 151, 3550, 2541, d10);
    }
    else if (d10 <= 4250) {
      return calcAQI(300, 201, 4250, 3551, d10);
    }
    else if (d10 <= 5050) {
      return calcAQI(400, 301, 5050, 4251, d10);
    }
    else if (d10 <= 6050) {
      return calcAQI(500, 401, 6050, 5051, d10);
    }
    else {
      return 1001;
    }
  }   
}

void detectCO(){
  float sensor_volt1; //Define variable for sensor voltage 
  float RS_gas1; //Define variable for sensor resistance  
  float ratio1; //Define variable for ratio
  float sensorValue1 = analogRead(gas2); //Read analog values of sensor  
  sensor_volt1 = sensorValue1*(3.3/4095.0); //Convert analog values to voltage 
  RS_gas1 = ((3.3*10.0)/sensor_volt1)-10.0; //Get value of RS in a gas
  ratio1 = RS_gas1/R01;  // Get ratio RS_gas/RS_air
  double ppm_log1 = (log10(ratio1)-b1)/m1; //Get ppm value in linear scale according to the the ratio value  
  co_ppm = pow(10, ppm_log1); //Convert ppm value to log scale 
  Serial.print("CO PPM = ");
  Serial.println(co_ppm);
  Serial.print("Sensor Value= ");
  Serial.println(sensorValue1);
}
void detectGas()
{
  
  for(int i=0;i<100;i++){
      gasReadings += analogRead(32);
      delay(10);
  }
  gasReadings = gasReadings/100;
  gas = gasReadings;
  gasReadings=0;
  // Calculate conductivity in pecents
  // The gas concetration depends on the coductivity
  // If the analog MQ sensor detects more gases
  // the conductivity will be higher
  int conductivity = round(((float)gas/4095)*100);
  Serial.print("Gas value: ");
  Serial.println(gas);
  String quality = "Good";
  String gasState = "off";
  if (gas <= 800)
  {
    //setColor(LOW, LOW, HIGH);
    gasState = "OFF";
  }
  else if (gas <= 1200)
  {
    quality="Moderate";
    //setColor(LOW, HIGH, LOW);
    gasState = "OFF";
  }
  else
  {
    quality="Poor";
    //setColor(HIGH, LOW, LOW);
    gasState = "ON";
  }

  // Do not print or draw on the display if there is no
  // change of the state from the MQ gas sensor
  // or if the change of the value for ADC is minimal
  if ( (5 > abs(gas - prevGas)) || ((prevConductivity == conductivity) && (prevQuality == quality)) )
  {
    return;
  }

  // Update the detected gas values
  prevConductivity = conductivity;
  prevQuality = quality;
  prevGas = gas;

  sensor_line2 = "Quality: " + quality;
  sensor_line3 = "Conductivity: ";
  sensor_line3 += conductivity;
  sensor_line3 += "%";

 
  // Print values in the serial output
  Serial.print("Gas value: ");
  Serial.println(gas);
  Serial.println(sensor_line1);
  Serial.println(sensor_line2);
  Serial.println(sensor_line3);
  
  need_redraw = true;
}

int getPM10(int pm10Value){
  if(pm10Value>0 && pm10Value<=100) return 1;
  if(pm10Value>100 && pm10Value<=250) return 2;
  if(pm10Value>250 && pm10Value<=350) return 3;
  if(pm10Value>350 && pm10Value<=430) return 4;
  if(pm10Value>430 && pm10Value<=800) return 5;
}
int getPM25(int pm25Value){
  if(pm25Value>0 && pm25Value<=60) return 1;
  if(pm25Value>60 && pm25Value<=90) return 2;
  if(pm25Value>90 && pm25Value<=120) return 3;
  if(pm25Value>120 && pm25Value<=250) return 4;
  if(pm25Value>250 && pm25Value<=500) return 5;
}
int getCO(int coValue){
  if(coValue>=0.0 && coValue<=2.0) return 1;
  if(coValue>2.0 && coValue<=9.0) return 2;
  if(coValue>9.0 && coValue<=15.0) return 3;
  if(coValue>15.0 && coValue<=30.0) return 4;
  if(coValue>30.0 && coValue<=40.0) return 5;
}
void displayLcd(){
  tft.fillRect(0, 0, 320, 240, TFT_GREY);
  tft.fillRect(5, 5, 310, 230, TFT_WHITE);
  tft.fillRect(5, 5, 310, 40, TFT_GREY);
  tft.drawLine(5, 45, 315, 45, TFT_BLACK);
  tft.drawLine(5, 235, 315, 235, TFT_BLACK);
  tft.drawLine(5, 45, 5, 235, TFT_BLACK);
  tft.drawLine(315, 45, 315, 235, TFT_BLACK);
  tft.drawLine(5, 80, 315, 80, TFT_BLACK);
  tft.drawLine(5, 110, 315, 110, TFT_BLACK);
  tft.drawLine(5, 140, 315, 140, TFT_BLACK);
  tft.drawLine(5, 170, 315, 170, TFT_BLACK);
  tft.drawLine(100, 45, 100, 170, TFT_BLACK);
  tft.drawLine(180, 80, 180, 170, TFT_BLACK);
  
  tft.setTextColor(TFT_BLUE);
  tft.setFreeFont(FF23);
  tft.drawString("CuriosityGym", 50, 10);
  tft.setTextColor(TFT_RED);
  tft.setFreeFont(FF18);
  tft.drawString("Pollutant", 7, 55);
  tft.drawString("AQI", 180, 55);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("PM10", 15, 86);
  tft.drawString("PM2.5", 15, 116);
  tft.drawString("CO", 15, 146);
  tft.drawString("Air Quality:", 15, 175);
} 
