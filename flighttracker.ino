
#include <TFT_eSPI.h>
#include <WiFiMulti.h>
#include <WString.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <esp_littlefs.h>
#include <TJpg_Decoder.h>

#include "env.h"

// #include <lvgl.h>

WiFiMulti wiFiMulti;
NetworkClientSecure* client = new NetworkClientSecure;
String token;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

String icao24;
JsonDocument flight;  //opensky
JsonDocument aircraft;
JsonDocument route;


void scaleUp(TFT_eSprite& orig) {
  const int origWidth = orig.width();
  const int origHeight = orig.height();
  const int destWidth = 320;
  const int destHeight = 240;

  const uint32_t xStep = ((uint32_t)origWidth << 16) / destWidth;
  const uint32_t yStep = ((uint32_t)origHeight << 16) / destHeight;

  uint16_t lineBuffer[320];

  uint32_t yAcc = 0;

  for (int y = 0; y < destHeight; y++) {
    uint32_t xAcc = 0;
    int srcY = yAcc >> 16;

    for (int x = 0; x < destWidth; x++) {
      int srcX = xAcc >> 16;
      lineBuffer[x] = orig.readPixel(srcX, srcY);
      xAcc += xStep;
    }

    tft.pushImage(0, y, destWidth, 1, lineBuffer);

    yAcc += yStep;
  }
}


/*
TODO

1. Font. Non ascii characters are displayed funny. It seems like the screen can display them, the data is wrong maybe? We need bigger font also.
2. Images scaling to 240x320. [x]
3. Displaying. Slideshow? Touch? Getting data? [x]
4. Loading the image to RAM instead of flash.
5. Putting the CPU in sleep
6. File removal logic is not really 100%, don't need to do all that.
7. Do not re-download the image...
8. Spinner :)

*/

// void printSpinner(){
//   const chars[4] = {-,\,|,/};
// move back the cursor?
// }


// Not sure if NetworkClientSecure checks the validity date of the certificate.
// Setting clock just to be sure...
void setClock() {
  configTime(0, 0, "pool.ntp.org");

  tft.print("Waiting for NTP time sync: ");
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    tft.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  tft.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  tft.print(F("Current time: "));
  char buf[26];
  tft.print(asctime_r(&timeinfo, buf));
}

bool getFile(String url) {
  // 1. Check if file exists
  if (LittleFS.exists("/aircraft.jpg")) {
    Serial.println("Found /aircraft.jpg");
    return false;  // Return false because we didn't need to fetch it
  }

  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient https;
  https.setTimeout(5000);
  https.begin(url);

  int httpCode = https.GET();

  if (httpCode == HTTP_CODE_OK) {
    // 2. Open file for writing ('w')
    File f = LittleFS.open("/aircraft.jpg", "w");
    if (!f) {
      Serial.println("File open failed");
      return false;
    }

    // 3. Stream the data directly to the file
    https.writeToStream(&f);
    f.flush();
    Serial.printf("File size in Flash: %d bytes\n", f.size());
    f.close();
    Serial.println("Download complete.");
    https.end();
    return true;
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    https.end();
    return false;
  }
}

String getToken() {
  HTTPClient https;
  Serial.print("[HTTPS] Connecting to OpenSky Auth...\n");
  if (https.begin(*client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token")) {

    // 1. Set the Required Headers
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // 2. Construct the POST body
    String httpRequestData = "grant_type=client_credentials&client_id=" OPENSKY_CLIENT_ID "&client_secret=" OPENSKY_CLIENT_SECRET;

    // 3. Send the POST request with the data
    int httpCode = https.POST(httpRequestData);

    if (httpCode > 0) {
      Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

      if (httpCode == HTTP_CODE_OK) {
        String payload = https.getString();

        // 4. Parse the JSON response
        JsonDocument doc;  // Adjust size if needed
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          const char* token = doc["access_token"];
          Serial.print("SUCCESS! Token: ");
          Serial.println(token);
          https.end();
          return token;
        } else {
          Serial.print("JSON Parse failed: ");
          Serial.println(error.f_str());
        }
      }
    } else {
      Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }

  return String();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  wiFiMulti.addAP(SSID, PASSWORD);

  // wait for WiFi connection
  tft.print("Waiting for WiFi to connect...");
  while ((wiFiMulti.run() != WL_CONNECTED)) {
    tft.print(".");
  }
  tft.println(" connected");
}

void display_data() {

  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  String callsign = flight[1];

  tft.print("Callsign: ");
  tft.println(callsign);

  String country = flight[2];
  tft.print("Country: ");
  tft.println(country);

  float altitude = flight[7];
  tft.print("Altitude: ");
  if (altitude < 1.0) {
    tft.println("Unknown");
  } else {
    tft.println(flight[7].as<String>() + "m");
  }

  tft.println("Speed: " + flight[9].as<String>() + "m/s");

  tft.println("Direction: " + flight[10].as<String>() + " deg");

  // this is almost never in the data
  // int category = flight[17];
  // switch (category) {
  //   case 0: Serial.println("No information at all"); break;
  //   case 1: Serial.println("No ADS-B Emitter Category Information"); break;
  //   case 2: Serial.println("Light (< 15500 lbs)"); break;
  //   case 3: Serial.println("Small (15500 to 75000 lbs)"); break;
  //   case 4: Serial.println("Large (75000 to 300000 lbs)"); break;
  //   case 5: Serial.println("High Vortex Large (aircraft such as B-757)"); break;
  //   case 6: Serial.println("Heavy (> 300000 lbs)"); break;
  //   case 7: Serial.println("High Performance (> 5g acceleration and 400 kts)"); break;
  //   case 8: Serial.println("Rotorcraft"); break;
  //   case 9: Serial.println("Glider / sailplane"); break;
  //   case 10: Serial.println("Lighter-than-air"); break;
  //   case 11: Serial.println("Parachutist / Skydiver"); break;
  //   case 12: Serial.println("Ultralight / hang-glider / paraglider"); break;
  //   case 13: Serial.println("Reserved"); break;
  //   case 14: Serial.println("Unmanned Aerial Vehicle"); break;
  //   case 15: Serial.println("Space / Trans-atmospheric vehicle"); break;
  //   case 16: Serial.println("Surface Vehicle - Emergency Vehicle"); break;
  //   case 17: Serial.println("Surface Vehicle - Service Vehicle"); break;
  //   case 18: Serial.println("Point Obstacle (includes tethered balloons)"); break;
  //   case 19: Serial.println("Cluster Obstacle"); break;
  //   case 20: Serial.println("Line Obstacle"); break;
  //   default: Serial.println("Unknown Category"); break;
  // }

  tft.println("Type: " + aircraft["type"].as<String>());

  tft.println("Manufacturer: " + aircraft["manufacturer"].as<String>());

  tft.println("Owner: " + aircraft["registered_owner"].as<String>());
  //TODO: URL photo

  tft.println("Airline:" + route["airline"]["name"].as<String>());

  tft.println("Origin: " + route["origin"]["name"].as<String>());

  tft.println("Destination: " + route["destination"]["name"].as<String>());





  //TODO: load the image to RAM instead.

  // tft.decodeUTF8()
  // tft.drawString()
  // tft.fontHeight()
  // tft.setFreeFont()
}

void display_image() {
  tft.setCursor(0,0);
  tft.fillScreen(TFT_BLACK);
  if (!aircraft["url_photo_thumbnail"].isNull()) {
    if (getFile(aircraft["url_photo_thumbnail"].as<String>())) {
      uint16_t w = 0, h = 0;
      TJpgDec.getFsJpgSize(&w, &h, "/aircraft.jpg", LittleFS);
      if (spr.createSprite(w, h) != nullptr) {

        int result = TJpgDec.drawFsJpg(0, 0, "/aircraft.jpg", LittleFS);
        switch (result) {
          case 0:  // JDR_OK
            Serial.println("Successfully drawn image");
            scaleUp(spr);
            break;
          case 1:  // JDR_INTR
            Serial.println("Interrupted by output function");
            break;
          case 2:  // JDR_INP
            Serial.println("Device error or wrong termination of input stream (File empty?)");
            break;
          case 3:  // JDR_MEM1
            Serial.println("Insufficient memory pool for the image");
            break;
          case 4:  // JDR_MEM2
            Serial.println("Insufficient stream input buffer");
            break;
          case 5:  // JDR_PAR
            Serial.println("Parameter error");
            break;
          case 6:  // JDR_FMT1
            Serial.println("Data format error");
            break;
          case 7:  // JDR_FMT2
            Serial.println("Right format but not supported");
            break;
          case 8:  // JDR_FMT3
            Serial.println("Not supported JPEG standard");
            break;
          default:
            Serial.printf("Unknown error: %d\n", result);
            break;
        }
      } else {
        Serial.println("Not enough RAM for original sprite!");
      }
    } else {
      tft.println("Unable to download image");
    }

    LittleFS.remove("/aircraft.jpg");


  } else {
    tft.println("Image not available");
  }
}

JsonDocument get_opensky() {
  /*
    0
    icao24
    string
    Unique ICAO 24-bit address of the transponder in hex string representation.
    1
    callsign
    string
    Callsign of the vehicle (8 chars). Can be null if no callsign has been received.
    2
    origin_country
    string
    Country name inferred from the ICAO 24-bit address.
    3
    time_position
    int
    Unix timestamp (seconds) for the last position update. Can be null if no position report was received by OpenSky within the past 15s.
    4
    last_contact
    int
    Unix timestamp (seconds) for the last update in general. This field is updated for any new, valid message received from the transponder.
    5
    longitude
    float
    WGS-84 longitude in decimal degrees. Can be null.
    6
    latitude
    float
    WGS-84 latitude in decimal degrees. Can be null.
    7
    baro_altitude
    float
    Barometric altitude in meters. Can be null.
    8
    on_ground
    boolean
    Boolean value which indicates if the position was retrieved from a surface position report.
    9
    velocity
    float
    Velocity over ground in m/s. Can be null.
    10
    true_track
    float
    True track in decimal degrees clockwise from north (north=0°). Can be null.
    11
    vertical_rate
    float
    Vertical rate in m/s. A positive value indicates that the airplane is climbing, a negative value indicates that it descends. Can be null.
    12
    sensors
    int[]
    IDs of the receivers which contributed to this state vector. Is null if no filtering for sensor was used in the request.
    13
    geo_altitude
    float
    Geometric altitude in meters. Can be null.
    14
    squawk
    string
    The transponder code aka Squawk. Can be null.
    15
    spi
    boolean
    Whether flight status indicates special purpose indicator.
    16
    position_source
    int
    Origin of this state’s position.    0 = ADS-B
      1 = ASTERIX
      2 = MLAT
      3 = FLARM
    17
    category
    int
    Aircraft category.   
        0 = No information at all
        1 = No ADS-B Emitter Category Information
        2 = Light (< 15500 lbs)
        3 = Small (15500 to 75000 lbs)
        4 = Large (75000 to 300000 lbs)
        5 = High Vortex Large (aircraft such as B-757)
        6 = Heavy (> 300000 lbs)
        7 = High Performance (> 5g acceleration and 400 kts)
        8 = Rotorcraft
        9 = Glider / sailplane
        10 = Lighter-than-air
        11 = Parachutist / Skydiver
        12 = Ultralight / hang-glider / paraglider
        13 = Reserved
        14 = Unmanned Aerial Vehicle
        15 = Space / Trans-atmospheric vehicle
        16 = Surface Vehicle – Emergency Vehicle
        17 = Surface Vehicle – Service Vehicle
        18 = Point Obstacle (includes tethered balloons)
        19 = Cluster Obstacle
        20 = Line Obstacle
  */
  HTTPClient https;

  String authHeader = "Bearer " + token;
  // Area above east Budapest
  // Serial.println("https://opensky-network.org/api/states/all?lamin=47.4613&lomin=19.1186&lamax=47.5320&lomax=19.2962&extended=1");
  // if (https.begin(*client, "https://opensky-network.org/api/states/all?lamin=47.4613&lomin=19.1186&lamax=47.5320&lomax=19.2962&extended=1")) {
    // Area above Switzerland (test)
    Serial.println("https://opensky-network.org/api/states/all?lamin=45.8389&lomin=5.9962&lamax=47.8229&lomax=10.5226&extended=1");
    if (https.begin(*client, "https://opensky-network.org/api/states/all?lamin=45.8389&lomin=5.9962&lamax=47.8229&lomax=10.5226&extended=1")) {
    https.addHeader("Authorization", authHeader);

    int httpCode = https.GET();

    if (httpCode == 200) {
      Serial.printf("[HTTPS] GET %d\n", httpCode);
      String payload = https.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        Serial.println(payload);
        return doc;
      } else {
        Serial.println(error.c_str());
      }
      https.end();
    } else if (httpCode == 401) {
      Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
      Serial.println("Token likely expired! Trying to re-acquire...");
      token.clear();
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
  } else {
    Serial.printf("[HTTPS] begin() failed)");
    //todo handle
  }

  return JsonDocument{};
}

JsonDocument get_aircraft(const String& aircraft) {
  /*
    {
        "response": {
            "aircraft": {
                "type": "DA20-C1",
                "icao_type": "DV20",
                "manufacturer": "Diamond",
                "mode_s": "A7EE69",
                "registration": "N61WR",
                "registered_owner_country_iso_name": "US",
                "registered_owner_country_name": "United States",
                "registered_owner_operator_flag_code": null,
                "registered_owner": "INDIANA STATE UNIVERSITY",
                "url_photo": null,
                "url_photo_thumbnail": null
            }
        }
    }
  */
  HTTPClient https;
  Serial.println("https://api.adsbdb.com/v0/aircraft/" + aircraft);
  if (https.begin(*client, "https://api.adsbdb.com/v0/aircraft/" + aircraft)) {

    int httpCode = https.GET();

    if (httpCode == 200) {
      Serial.printf("[HTTPS] GET %d\n", httpCode);
      String payload = https.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        Serial.println(payload);
        return doc;
      } else {
        Serial.println(error.c_str());
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.printf("[HTTPS] begin() failed)");
  }
  return JsonDocument{};
}

JsonDocument get_route(const String& callsign) {
  /*
    {
      "response": {
          "flightroute": {
              "callsign": "SWA3652",
              "callsign_icao": "SWA3652",
              "callsign_iata": "WN3652",
              "airline": {
                  "name": "Southwest Airlines",
                  "icao": "SWA",
                  "iata": "WN",
                  "country": "United States",
                  "country_iso": "US",
                  "callsign": "SOUTHWEST"
              },
              "origin": {
                  "country_iso_name": "US",
                  "country_name": "United States",
                  "elevation": 96,
                  "iata_code": "MCO",
                  "icao_code": "KMCO",
                  "latitude": 28.429399490356445,
                  "longitude": -81.30899810791016,
                  "municipality": "Orlando",
                  "name": "Orlando International Airport"
              },
              "destination": {
                  "country_iso_name": "US",
                  "country_name": "United States",
                  "elevation": 266,
                  "iata_code": "MHT",
                  "icao_code": "KMHT",
                  "latitude": 42.932598,
                  "longitude": -71.435699,
                  "municipality": "Manchester",
                  "name": "Manchester-Boston Regional Airport"
              }
          }
      }
  */
  HTTPClient https;
  Serial.println("https://api.adsbdb.com/v0/callsign/" + callsign);
  if (https.begin(*client, "https://api.adsbdb.com/v0/callsign/" + callsign)) {

    int httpCode = https.GET();

    if (httpCode == 200) {
      Serial.printf("[HTTPS] GET %d\n", httpCode);
      String payload = https.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        Serial.println(payload);
        return doc;
      } else {
        Serial.println(error.c_str());
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.printf("[HTTPS] begin() failed)");
  }
  return JsonDocument{};
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {

  // This function will clip the image block rendering automatically at the TFT boundaries
  // tft.pushImage(x, y, w, h, bitmap);
  spr.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}



void setup() {
  Serial.begin(115200);

  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(1);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS initialisation failed!");
  }

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);

  connectWiFi();

  setClock();

  tft.print("Waiting for new flight");

  if (client) {
    client->setInsecure();
  } else {
    Serial.print("Client init unsuccessful! Reset required.");
  }
  token = getToken();

  LittleFS.remove("/aircraft.jpg");



  TJpgDec.setCallback(tft_output);
}



void loop() {
  if (token.isEmpty()) {
    token = getToken();
    delay(15000);
    return;
  }

  auto opensky_data = get_opensky();
  if (!opensky_data["states"].isNull()) {
    // new data coming in
    flight = opensky_data["states"][0];  // use only the first plane returned for now
    // if (icao24.equals(flight[0].as<String>())) {
    // do something with the file here to not download it again...
    //   return delay(30000);
    // }
    icao24 = flight[0].as<String>();
    auto callsign = flight[1];
    aircraft = get_aircraft(icao24)["response"]["aircraft"];
    route = get_route(callsign)["response"]["flightroute"];
  }

  display_data();
  delay(15000);
  display_image();
  delay(15000);
}
