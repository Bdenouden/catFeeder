//
// A simple server implementation showing how to:
//  * serve static messages
//  * read GET and POST parameters
//  * handle missing pages / 404s
//

#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#endif
#include <WiFiUdp.h>
// #include <ArduinoOTA.h> // not enough space for OTA on the esp M3
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <config.h>
#include <FastLED.h>

#define USE_LittleFS
#ifdef USE_LittleFS
#include <FS.h>
#define SPIFFS LittleFS
#include <LittleFS.h>
#endif

AsyncWebServer server(80);
const char *missingParams = "Missing/invalid parameter(s)";
const char *typeJson = "application/json";
const char *mOK = "OK";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Servo S1;
Servo S2;

const uint8_t openPos = 0;
const uint8_t closePos = 170;
const uint8_t servoIncrement = 3;
const uint8_t servoDelay = 10;

CRGB leds[NUM_LEDS];

struct Gate
{
    uint8_t id;             // gate number
    uint8_t state;          // 1 = open, 0 = closed
    unsigned long schedule; // stores epoch time
    Servo *servo;
    uint8_t powerPin;
    unsigned long t_move;
    uint8_t target;
    bool isPowered;
};

char ssid[30];
char password[30];

struct Gate g1 = {1, 0, 0, &S1, SERVO_EN_1, 0, closePos, false};
struct Gate g2 = {2, 0, 0, &S2, SERVO_EN_2, 0, closePos, false};

void getWiFiCredentials()
{
    File f = SPIFFS.open("cred.dat", "r");
    if (!f || f.isDirectory())
        Serial.println("Could not load wifi credentials from SPIFFS");
    else
    {
        Serial.print("\nGetting credentials from memory...");
        uint16_t i = 0;
        char *ptr = ssid;
        while (f.available())
        {
            char c = f.read();
            if (c == '\n')
            {
                ptr[i] = '\0';
                ptr = password;
                i = 0;
            }
            else
            {
                ptr[i] = c;
                i++;
            }
        }
        Serial.println("Done!");
    }
    f.close();
}

void saveWifiCredentials(const char *_ssid, const char *_password)
{
    File f = SPIFFS.open("cred.dat", "w+");
    if (!f)
        Serial.println("Could not load wifi credentials from SPIFFS");
    else
    {
        Serial.println("wifi_cred stored");
        Serial.println(*_ssid);
        Serial.println(*_password);
        f.printf("%s\n%s\n", _ssid, _password);
        // unsigned char *data = reinterpret_cast<unsigned char *>(wifi_cred);
        // f.write(data, sizeof(Cred));
        f.close();
        Serial.println("Credentials written to file!");
    }
}

void saveConfig()
{
    File f = SPIFFS.open("/data.dat", "w+");
    Gate g12[] = {g1, g2};
    if (!f)
    {
        Serial.println("Could not write data to SPIFFS");
    }
    else
    {
        unsigned char *data = reinterpret_cast<unsigned char *>(g12);
        f.write(data, sizeof(Gate) * 2);
        f.close();
        Serial.println("Data written to file!");
    }
    f.close();
}

void getConfig()
{
    File f = SPIFFS.open("/data.dat", "r");
    Gate g12[2] = {};
    uint8_t data[sizeof(Gate) * 2];
    if (!f)
    {
        Serial.println("Could not load data from SPIFFS");
    }
    else
    {
        for (unsigned int i = 0; i < sizeof(data); i++)
        {
            data[i] = f.read();
        }
        memcpy(&g12, &data, sizeof(Gate) * 2);
        g1 = g12[0];
        g2 = g12[1];

        // persistent storage of pointers is not possible -> S1 and S2 should be reassigned after reading from memory
        g1.servo = &S1;
        g2.servo = &S2;

        Serial.println("Gate config has been loaded from memory");
    }
    f.close();
}

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

void listSFcontent()
{
    String str = "\r\n";
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
        str += dir.fileName();
        str += " / ";
        str += dir.fileSize();
        str += "\r\n";
    }
    Serial.print(str);
}

// obtain a gate pointer from an id
struct Gate *id2g(uint8_t id)
{
    struct Gate *g = nullptr;
    if (id == 1)
    {
        g = &g1;
    }
    else if (id == 2)
    {
        g = &g2;
    }
    return g;
}

bool moveGate(uint8_t id, uint8_t pos)
{
    struct Gate *g = id2g(id);
    if (g == nullptr)
    {
        Serial.println("nullpointer found while moving gate!");
        leds[0] = CRGB::Red;
        FastLED.show();
        return false;
    }

    leds[0] = CRGB::Blue;
    FastLED.show();
    digitalWrite(g->powerPin, LOW); // turn on
    g->isPowered = true;
    g->target = pos;
    Serial.printf("\tservo id: %d\n", g->id);
    Serial.printf("\tservo powerpin: %d\n", g->powerPin);
    return true;
}

// open gate matching id
bool gOpen(uint8_t id)
{
    Serial.printf("opening gate %d\n", id);
    return moveGate(id, openPos);
}

// close gate matching id
bool gClose(uint8_t id)
{
    Serial.printf("closing gate %d\n", id);
    return moveGate(id, closePos);
}

void createWebPages()
{
    // server assets
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html"); });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/style.css"); });
    server.on("/skeleton.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/skeleton.min.css"); });
    server.on("/normalize.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/normalize.min.css"); });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/script.js"); });
    server.on("/cat.svg", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/cat.svg"); });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  saveWifiCredentials("", "");
                  request->send(200, "text/plain", "The wifi credentials have been reset");
                  ESP.restart(); });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/settings.html"); });
    server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam("ssid", true) &&
                      request->hasParam("password", true))
                  {
                      AsyncWebParameter *ssid = request->getParam("ssid", true);
                      AsyncWebParameter *pw = request->getParam("password", true);

                      Serial.println(ssid->value().c_str());
                      Serial.println(pw->value().c_str());
                      saveWifiCredentials(ssid->value().c_str(), pw->value().c_str());
                      Serial.println("Received new settings!");
                      request->redirect("/");
                      ESP.restart();
                  }

                  request->send(400, typeJson, missingParams); });

    // api get info endpoint
    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  AsyncResponseStream *response = request->beginResponseStream(typeJson);
                  StaticJsonDocument<256> doc;
                  doc["RSSI"] = WiFi.RSSI();
                  doc["ip"] = WiFi.localIP();
                  doc["time"] = timeClient.getEpochTime();
                  doc["isConnected"] = WiFi.isConnected();
                  JsonObject gate_1 = doc.createNestedObject("gate_1");
                  gate_1["state"] = g1.target == closePos ? 0 : 1;
                  gate_1["schedule"] = g1.schedule;
                  JsonObject gate_2 = doc.createNestedObject("gate_2");
                  gate_2["state"] = g2.target == closePos ? 0 : 1; // 1=open, 0=closed
                  gate_2["schedule"] = g2.schedule;
                  serializeJson(doc, *response);
                  request->send(response); });
    // api endpoint to open a gate
    server.on("/api/open", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  uint8_t gate = 0;
                  if (request->hasParam("g"))
                  {
                      gate = request->getParam("g")->value().toInt();
                      if (gate == 1 || gate == 2)
                      {
                          // TODO check if gate actually closed
                          gOpen(gate); // gate number as argument
                          request->send(200, typeJson, mOK);
                      }
                  }
                  request->send(400, typeJson, missingParams); });
    // api endpoint to close a gate
    server.on("/api/close", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  uint8_t gate = 0;
                  if (request->hasParam("g"))
                  {
                      gate = request->getParam("g")->value().toInt();
                      if (gate == 1 || gate == 2)
                      {
                          // TODO check if gate actually closed
                          gClose(gate); // gate number as argument
                          request->send(200, typeJson, mOK);
                      }
                  }
                  request->send(400, typeJson, missingParams); });

    // Send a GET request to <IP>/api/setdate?fdate=<message>
    server.on("/api/setdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam("t") && request->hasParam("g"))
                  {
                      Gate *g = id2g(request->getParam("g")->value().toInt());
                      if (g == nullptr)
                      {
                          Serial.println("nullpointer found while setting date!");
                          request->send(400, typeJson, missingParams);
                      }
                      g->schedule = request->getParam("t")->value().toInt();
                      g->state = true; // schedule active
                      request->send(200, typeJson, mOK);
                      saveConfig();
                  }
                  request->send(400, typeJson, missingParams); });

    // remove scheduled opening
    server.on("/api/cleardate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam("g"))
                  {
                      Gate *g = id2g(request->getParam("g")->value().toInt());
                      if (g == nullptr)
                      {
                          Serial.println("nullpointer found while setting date!");
                          request->send(400, typeJson, missingParams);
                      }
                      g->schedule = 0;
                      g->state = false; // schedule not active
                      request->send(200, typeJson, mOK);
                  }
                  request->send(400, typeJson, missingParams); });

    server.onNotFound(notFound);
}

void updateGate(Gate *g)
{
    if (g->isPowered)
    {
        if (millis() - g->t_move >= servoDelay)
        {
            uint8_t pos = g->servo->read();
            bool dir = g->servo->read() < g->target; // increment position if position < target
            // end reached
            if ((dir && g->target - servoIncrement < pos) ||
                (!dir && g->target + servoIncrement > pos) ||
                !WiFi.isConnected())
            {
                g->servo->write(g->target);
                delay(1000);
                Serial.printf("Turning off power from gate %d\n", g->id);
                digitalWrite(g->powerPin, HIGH);
                g->isPowered = false;
                saveConfig();
                leds[0] = CRGB::Red;
                FastLED.show();
            }
            else
            {
                if (dir)
                    pos += servoIncrement;
                else if (pos >= servoIncrement)
                    pos -= servoIncrement; // prevent underflow
                else
                    pos = 0;

                // Serial.printf("\tServo read: %d \n", g->servo->read());
                g->servo->write(pos);
                g->t_move = millis();
            }
        }
    }
}

void checkSchedule(Gate *g)
{
    timeClient.update();
    unsigned long time = timeClient.getEpochTime();

    // check if schedule is set, time has exceeded and gate is not already open
    if (
        g->schedule != 0 &&
        g->state == true && // schedule active
        time >= g->schedule &&
        g->servo->read() != openPos)
    {
        gOpen(g->id);
        g->state = false; // schedule not active
        saveConfig();
    }
}

void setup()
{
    Serial.begin(115200);

    // attach LEDs
    FastLED.addLeds<WS2812B, LED, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(20);
    leds[0] = CRGB::Yellow;
    FastLED.show();

    if (!SPIFFS.begin())
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }

    Serial.println("SPIFFS content:");
    listSFcontent();

    getWiFiCredentials();
    Serial.printf("SSID: %s\n", ssid);
    Serial.printf("PW: %s\n", password);
    Serial.println("Connecting to wifi....");

    WiFi.hostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
    {
        Serial.printf("WiFi Failed!\n");

        // switch to AP mode to host login portal
        WiFi.mode(WIFI_AP);
        delay(100);
        WiFi.softAP(AP_SSID, AP_PASS);
    }
    Serial.print("IP Address: ");
    Serial.println(WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP());

    if (!MDNS.begin(HOSTNAME))
    { // Start the mDNS responder for esp8266.local
        Serial.println("Error setting up MDNS responder!");
    }
    Serial.println("mDNS responder started");

    // attach servo pin to servo objects
    S1.attach(SERVO_S1, 544, 2400);
    S2.attach(SERVO_S2, 544, 2400);

    pinMode(SERVO_EN_1, OUTPUT);
    digitalWrite(g1.powerPin, HIGH);

    pinMode(SERVO_EN_2, OUTPUT);
    digitalWrite(g2.powerPin, HIGH);

    // Get configurations from memory
    getConfig();

    // Create endpoints for website
    createWebPages();
    server.begin();

    leds[0] = CRGB::Black;
    FastLED.show();

    Serial.println("Setup complete");
}

void loop()
{
    // check the schedule of a gate and open if time has elapsed
    checkSchedule(&g1);
    checkSchedule(&g2);

    // update the movement of a gate
    updateGate(&g1);
    updateGate(&g2);
}
