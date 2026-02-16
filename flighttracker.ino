
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
#include "memory_optimizations.h"

// #include <lvgl.h>

WiFiMulti wiFiMulti;
NetworkClientSecure client;
String token;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

String icao24;
String icao24_old;
JsonDocument flight;  //opensky
JsonDocument aircraft;
JsonDocument route;

#define LED_RED 4
#define LED_GREEN 16
#define LED_BLUE 17

/*
TODO
1. Font. Non ascii characters are displayed funny. It seems like the screen can display them.
*/

void connect_wifi() {
  WiFi.mode(WIFI_STA);
  wiFiMulti.addAP(SSID, PASSWORD);

  tft.print("Waiting for WiFi to connect...");
  while ((wiFiMulti.run() != WL_CONNECTED)) {
    tft.print(".");
  }
  tft.println(" connected");
}

// Not sure if NetworkClientSecure checks the validity date of the certificate.
// Setting clock just to be sure...
void set_clock() {
  configTime(0, 0, "pool.ntp.org");

  tft.print("Waiting for NTP time sync: ");
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    tft.print(".");
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

bool draw_to_sprite() {
  // Draw to sprite
  uint16_t w = 0, h = 0;
  TJpgDec.getFsJpgSize(&w, &h, "/aircraft.jpg", LittleFS);
  // Serial.printf("Jpg size: w %d, h %d", w, h);

  uint16_t target_w, target_h;
  if (w / 4 <= TFT_HEIGHT && h / 4 <= TFT_WIDTH) {
    // Serial.println("Scaling by factor of 4");
    target_w = w / 4;
    target_h = h / 4;
    TJpgDec.setJpgScale(4);
    // Serial.println();
    // Serial.printf("Trying to create sprite of size w %d h %d", target_w, target_h);
    // Serial.println();
    if (spr.createSprite(target_w, target_h) != nullptr) {
      TJpgDec.drawFsJpg(0, 0, "/aircraft.jpg", LittleFS);
      return true;

    } else {
      // Serial.println("Not enough RAM for original sprite!");
    }
  }

  // Serial.println("Scaling by factor of 8");
  target_w = w / 8;
  target_h = h / 8;
  TJpgDec.setJpgScale(8);
  // Serial.println();
  // Serial.printf("Trying to create sprite of size w %d h %d", target_w, target_h);
  // Serial.println();
  if (spr.createSprite(target_w, target_h) != nullptr) {
    TJpgDec.drawFsJpg(0, 0, "/aircraft.jpg", LittleFS);
    return true;

  } else {
    // Serial.println("Not enough RAM for original sprite!");
    // tft.println("Image could not be displayed (out of memory)");
    return false;
  }
}

void get_image(String url) {

  // Serial.println("Trying to get image " + url);

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient https;
  https.setTimeout(10000);
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // The full image URL sends a 301
  https.begin(client, url);

  int httpCode = https.GET();

  if (httpCode == HTTP_CODE_OK) {

    File f = LittleFS.open("/aircraft.jpg", "w");
    if (!f) {
      // Serial.println("File open failed");
      return;
    }

    // Stream the data directly to the file
    https.writeToStream(&f);
    f.flush();
    // Serial.printf("File size in Flash: %d bytes\n", f.size());
    f.close();
    // Serial.println("Download complete.");
    https.end();
  } else {
    // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
    https.end();
  }
}

// Get token for auth to Opensky
String get_token() {
  HTTPClient https;
  // Serial.print("[HTTPS] Connecting to OpenSky Auth...\n");
  if (https.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token")) {

    // 1. Set the Required Headers
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // 2. Construct the POST body
    String httpRequestData = "grant_type=client_credentials&client_id=" OPENSKY_CLIENT_ID "&client_secret=" OPENSKY_CLIENT_SECRET;

    // 3. Send the POST request with the data
    int httpCode = https.POST(httpRequestData);

    if (httpCode > 0) {
      // Serial.printf("[HTTPS] POST... code: %d\n", httpCode);

      if (httpCode == HTTP_CODE_OK) {
        String payload = https.getString();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          const char* token = doc["access_token"];
          // Serial.print("Acquired token: ");
          // Serial.println(token);
          https.end();
          return token;
        } else {
          // Serial.print("JSON Parse failed: ");
          Serial.println(error.f_str());
        }
      }
    } else {
      // Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }

  return String{};
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
  if (https.begin(client, "https://opensky-network.org/api/states/all?lamin=47.4613&lomin=19.1186&lamax=47.5320&lomax=19.2962&extended=1")) {
    // Area above Switzerland (test)
    // Serial.println("https://opensky-network.org/api/states/all?lamin=45.8389&lomin=5.9962&lamax=47.8229&lomax=10.5226&extended=1");
    // if (https.begin(client, "https://opensky-network.org/api/states/all?lamin=45.8389&lomin=5.9962&lamax=47.8229&lomax=10.5226&extended=1")) {
    https.addHeader("Authorization", authHeader);

    int httpCode = https.GET();

    if (httpCode == 200) {
      // Serial.printf("[HTTPS] GET %d\n", httpCode);
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
      // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
      // Serial.println("Token likely expired! Trying to re-acquire...");
      token.clear();
    } else {
      // Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
  } else {
    // Serial.printf("[HTTPS] begin() failed)");
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
  // Serial.println("https://api.adsbdb.com/v0/aircraft/" + aircraft);
  if (https.begin(client, "https://api.adsbdb.com/v0/aircraft/" + aircraft)) {

    int httpCode = https.GET();

    if (httpCode == 200) {
      // Serial.printf("[HTTPS] GET %d\n", httpCode);
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
      // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    // Serial.printf("[HTTPS] begin() failed)");
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
                  "municipality": "Manchester",opensky_data
                  "name": "Manchester-Boston Regional Airport"
              }
          }
      }
  */
  HTTPClient https;
  // Serial.println("https://api.adsbdb.com/v0/callsign/" + callsign);
  if (https.begin(client, "https://api.adsbdb.com/v0/callsign/" + callsign)) {

    int httpCode = https.GET();

    if (httpCode == 200) {
      // Serial.printf("[HTTPS] GET %d\n", httpCode);
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
      // Serial.printf("[HTTPS] GET... failed, error: %d %s\n", httpCode, https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    // Serial.printf("[HTTPS] begin() failed)");
  }
  return JsonDocument{};
}

String to_string(const JsonDocument& doc) {
  if (doc.isNull()) {
    return "Unknown";
  }
  return doc.as<String>();
}

void display_data() {
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);

  tft.print("Callsign: ");
  tft.println(to_string(flight[1]));


  tft.print("Country: ");
  tft.println(to_string(flight[2]));

  float altitude = flight[7];
  tft.print("Altitude: ");
  if (altitude < 1.0) {
    tft.println("Unknown");
  } else {
    tft.println(to_string(flight[7]) + "m");
  }

  tft.println("Speed: " + to_string(flight[9]) + "m/s");

  tft.println("Direction: " + to_string(flight[10]) + " deg");

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

  tft.println("Type: " + to_string(aircraft["type"]));

  tft.println("Manufacturer: " + to_string(aircraft["manufacturer"]));

  tft.println("Owner: " + to_string(aircraft["registered_owner"]));

  tft.println("Airline:" + to_string(route["airline"]["name"]));

  tft.println("Origin: " + to_string(route["origin"]["name"]));

  tft.println("Destination: " + to_string(route["destination"]["name"]));
}

void scale_and_draw(TFT_eSprite& orig) {
  // Scales the sprite to target width/height, and pushes the image line-by-line to the display (saves memory)
  const int origWidth = orig.width();
  const int origHeight = orig.height();
  const int destWidth = TFT_HEIGHT;
  const int destHeight = TFT_WIDTH;

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

bool spr_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  spr.pushImage(x, y, w, h, bitmap);
  return true;
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

void display_image() {
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);

  if (LittleFS.exists("/aircraft.jpg")) {
    TJpgDec.setCallback(spr_output);
    if (draw_to_sprite()) {
      scale_and_draw(spr);
    }
    spr.deleteSprite();
  } else {
    TJpgDec.setCallback(tft_output);
    TJpgDec.drawFsJpg(0, 0, "/unavailable.jpg", LittleFS);
  }
}

void wait_for_first_flight() {
  while (get_opensky()["states"].isNull()) {
    delay(30000);
  };
}

void setup() {

  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  tft.begin();

  tft.fillScreen(TFT_BLACK);
  tft.setRotation(1);
  tft.setTextSize(2);

  if (!LittleFS.begin(true)) {
    // Serial.println("LittleFS initialisation failed!");
  }

  TJpgDec.setSwapBytes(false);

  spr.setColorDepth(16);  // TODO set to 8

  connect_wifi();

  set_clock();

  client.setInsecure();

  tft.println("Waiting for new flight...");

  token = get_token();

  LittleFS.remove("/aircraft.jpg");

  wait_for_first_flight();
}

// available images on flash:
// demo.jpg
// demo_thumb.jpg
// unavailable.jpg

void loop() {
  if (token.isEmpty()) {
    token = get_token();
    delay(15000);
    return;
  }

  // randomSeed(esp_random());
  // auto index = random(0, 16);  // only for testing, should be 0 normally.
  auto index = 0;

  auto opensky_data = get_opensky();

  if (!opensky_data["states"].isNull()) {
    // Serial.println("New data coming in...");

    flight = opensky_data["states"][0];  // use only the first plane returned for now
    icao24_old = icao24;
    icao24 = flight[0].as<String>();
    auto callsign = flight[1].as<String>();
    Serial.println(icao24);
    Serial.println(callsign);
    aircraft = get_aircraft(icao24)["response"]["aircraft"];
    route = get_route(callsign)["response"]["flightroute"];
    for (auto i = 0; i < 5; i++) {
      digitalWrite(LED_RED, LOW);
      delay(200);
      digitalWrite(LED_RED, HIGH);
      delay(200);
    }
  }

  display_data();
  delay(10000);
  if (!aircraft["url_photo"].isNull() && (icao24_old != icao24)) {
    // takes a while, so display fresh data as it processes.
    get_image(aircraft["url_photo"].as<String>());
  }
  display_image();

  delay(15000);
}
