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
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <credentials.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <config.h>
#include <FastLED.h>
// #include <SPIFFS.h>

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
    const uint8_t id;       // gate number
    uint8_t state;          // 1 = open, 0 = closed
    unsigned long schedule; // stores epoch time
    Servo *servo;
    const uint8_t powerPin;
    unsigned long t_move;
    uint8_t target;
    bool isPowered;
};

// TODO add pointers to g1 and g2 to an array
struct Gate g1 = {1, 0, 0, &S1, SERVO_EN_1, 0, closePos, false};
struct Gate g2 = {2, 0, 0, &S2, SERVO_EN_2, 0, closePos, false};

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
        return false;
    }

    leds[0] = CRGB::Blue;
    digitalWrite(g->powerPin, LOW); // turn on
    g->isPowered = true;
    g->target = pos;
    Serial.printf("\tservo id: %d\n", g->id);
    Serial.printf("\tservo powerpin: %d\n", g->powerPin);
    return true;
}

// open gate matching id
bool open(uint8_t id)
{
    Serial.printf("opening gate %d\n", id);
    return moveGate(id, openPos);
}

// close gate matching id
bool close(uint8_t id)
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

    // api get info endpoint
    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  AsyncResponseStream *response = request->beginResponseStream(typeJson);
                  StaticJsonDocument<256> doc;
                  doc["RSSI"] = WiFi.RSSI();
                  doc["ip"] = WiFi.localIP();
                  doc["time"] = timeClient.getEpochTime();
                  JsonObject gate_1 = doc.createNestedObject("gate_1");
                  gate_1["state"] = g1.target == closePos ? 0 : 1;
                  gate_1["schedule"] = g1.schedule;
                  JsonObject gate_2 = doc.createNestedObject("gate_2");
                  gate_2["state"] = g2.target == closePos ? 0 : 1; // 1=open, 0=closed
                  gate_2["schedule"] = g2.schedule;
                  serializeJson(doc, *response);
                  request->send(response);
              });
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
                          open(gate); // gate number as argument
                          request->send(200, typeJson, mOK);
                      }
                  }
                  request->send(400, typeJson, missingParams);
              });
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
                          close(gate); // gate number as argument
                          request->send(200, typeJson, mOK);
                      }
                  }
                  request->send(400, typeJson, missingParams);
              });

    // Send a GET request to <IP>/api/setdate?fdate=<message>
    server.on("/api/setdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam("t") && request->hasParam("g"))
                  {
                      Gate *g = id2g(request->getParam("g")->value().toInt());
                      if (g == nullptr)
                      {
                          Serial.println("nullpointer found while setting date!");
                          leds[0] = CRGB::Red;
                          request->send(400, typeJson, missingParams);
                      }
                      g->schedule = request->getParam("t")->value().toInt();
                      g->state = true; // schedule active
                      request->send(200, typeJson, mOK);
                  }
                  request->send(400, typeJson, missingParams);
              });

    // remove scheduled opening
    server.on("/api/cleardate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam("g"))
                  {
                      Gate *g = id2g(request->getParam("g")->value().toInt());
                      if (g == nullptr)
                      {
                          Serial.println("nullpointer found while setting date!");
                          leds[0] = CRGB::Red;
                          request->send(400, typeJson, missingParams);
                      }
                      g->schedule = 0;
                      g->state = false; // schedule not active
                      request->send(200, typeJson, mOK);
                  }
                  request->send(400, typeJson, missingParams);
              });

    server.onNotFound(notFound);
}

void setup()
{
    Serial.begin(115200);

    if (!SPIFFS.begin())
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }

    Serial.println("SPIFFS content:");
    listSFcontent();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.printf("WiFi Failed!\n");
        return;
    }

    Serial.print("\n\nIP Address: ");
    Serial.println(WiFi.localIP());

    // attach servo pin to servo objects
    //    g1.servo.attach(SERVO_S1);
    //    g2.servo.attach(SERVO_S2);

    //test
    S1.attach(SERVO_S1);
    S2.attach(SERVO_S2);

    pinMode(SERVO_EN_1, OUTPUT);
    digitalWrite(g1.powerPin, HIGH);

    pinMode(SERVO_EN_2, OUTPUT);
    digitalWrite(g2.powerPin, HIGH);

    // attach LEDs
    FastLED.addLeds<WS2812B, LED, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(20);

    // Create endpoints for website
    createWebPages();
    server.begin();

    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(1000);

    leds[0] = CRGB::Green;
    FastLED.show();

    Serial.println("Setup complete");
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
                (!dir && g->target + servoIncrement > pos))
            {
                g->servo->write(g->target);
                Serial.printf("Turning off power from gate %d\n", g->id);
                delay(100);
                digitalWrite(g->powerPin, HIGH);
                g->isPowered = false;
                leds[0] = CRGB::Green;
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
        open(g->id);
        g->state = false; // schedule not active
    }
}

void loop()
{
    // check the schedule of a gate and open if time has elapsed
    checkSchedule(&g1);
    checkSchedule(&g2);

    // update the movement of a gate
    updateGate(&g1);
    updateGate(&g2);

    FastLED.show();
}
