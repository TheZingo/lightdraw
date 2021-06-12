#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LITTLEFS.h>
#include <ArduinoJson.h>
#define USE_STATIC_IP_CONFIG_IN_CP false
#include <ESPAsync_WiFiManager.h>
#define ESP_DRD_USE_LITTLEFS true
#define DOUBLERESETDETECTOR_DEBUG true
#include <ESP_DoubleResetDetector.h>

#define LED_TYPE WS2812
#define COLOR_ORDER GRB
#define VOLTS 5
#define MAX_MA 250
#define DELAYVAL 100
#define NUM_LEDS 50
#define DATA_PIN 12
#define DRD_TIMEOUT 10
#define DRD_ADDRESS 0

CRGBArray<NUM_LEDS> leds;

extern const uint8_t files_favicon_start[] asm("_binary_html_favicon_ico_start");
extern const uint8_t files_favicon_end[] asm("_binary_html_favicon_ico_end");
extern const uint8_t files_interface_start[] asm("_binary_html_interface_html_start");
extern const uint8_t files_interface_end[] asm("_binary_html_interface_html_end");

const char *PARAM_ID = "id";
const char *PARAM_RED = "red";
const char *PARAM_GREEN = "green";
const char *PARAM_BLUE = "blue";

void clearLEDs();

DoubleResetDetector *drd;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
        client->ping();
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
    }
    else if (type == WS_EVT_ERROR)
    {
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
    }
    else if (type == WS_EVT_PONG)
    {
        Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
    }
    else if (type == WS_EVT_DATA)
    {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        String msg = "";
        if (info->final && info->index == 0 && info->len == len)
        {
            Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

            for (size_t i = 0; i < info->len; i++)
            {
                msg += (char)data[i];
            }

            Serial.printf("%s\n", msg.c_str());

            int idIndex = msg.indexOf("id=");
            int redIndex = msg.indexOf("&r=");
            int greenIndex = msg.indexOf("&g=");
            int blueIndex = msg.indexOf("&b=");
            int ledId = msg.substring(idIndex + 3, redIndex).toInt();
            int ledRed = msg.substring(redIndex + 3, greenIndex).toInt();
            int ledGreen = msg.substring(greenIndex + 3, blueIndex).toInt();
            int ledBlue = msg.substring(blueIndex + 3).toInt();
            leds[ledId] = CRGB(ledRed, ledGreen, ledBlue);
            FastLED.show();
            // ws.textAll(msg);
        }
    }
}

void setup()
{

    delay(3000);
    FastLED.setMaxPowerInVoltsAndMilliamps(VOLTS, MAX_MA);
    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip);

    Serial.begin(115200);
    if (!LITTLEFS.begin(true))
    {
        Serial.println("LITTLEFS Mount Failed");
        return;
    }

    drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
    ESPAsync_WiFiManager ESPAsync_wifiManager(&server, &dnsServer, "LightDraw");

    WiFi_AP_IPConfig WM_AP_IPconfig;
    WM_AP_IPconfig._ap_static_ip = IPAddress(192, 168, 100, 1);
    WM_AP_IPconfig._ap_static_gw = IPAddress(192, 168, 100, 1);
    WM_AP_IPconfig._ap_static_sn = IPAddress(255, 255, 255, 0);

    String ssid = "LightDraw_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    const char *password = "light_draw";

    ESPAsync_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
    ESPAsync_wifiManager.setMinimumSignalQuality(-1);
    ESPAsync_wifiManager.setConfigPortalChannel(0);

    String Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
    String Router_Pass = ESPAsync_wifiManager.WiFi_Pass();

    if (drd->detectDoubleReset())
    {

        if (!ESPAsync_wifiManager.startConfigPortal((const char *)ssid.c_str(), password))
        {
            Serial.println(F("Not connected to WiFi but continuing anyway."));
        }
        else
        {
            Serial.println(F("WiFi connected..."));
        }
    }
    else
    {
        if (!ESPAsync_wifiManager.autoConnect())
        {
            Serial.println(F("Not connected to WiFi but continuing anyway. (autoConnect)"));
        }
        else
        {
            Serial.println(F("WiFi connected...(autoConnect)"));
        }
    }

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", (const char *)files_interface_start); });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  AsyncWebServerResponse *response = request->beginResponse_P(200, "image/x-icon", files_favicon_start, files_favicon_end - files_favicon_start);
                  request->send(response);
              });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  AsyncResponseStream *response = request->beginResponseStream("application/json");
                  response->print("{\"data\": [");
                  for (int i = 0; i < NUM_LEDS; i++)
                  {
                      CRGB color = leds[i];
                      response->printf("{\"id\": %i, \"r\": %i, \"g\": %i, \"b\": %i}", i, color.red, color.green, color.blue);
                      if (i != NUM_LEDS - 1)
                      {
                          response->print(",");
                      }
                  }
                  response->print("]}");
                  request->send(response);
              });

    server.on("/led", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  if (request->hasParam(PARAM_ID, true) && request->hasParam(PARAM_RED, true) && request->hasParam(PARAM_GREEN, true) && request->hasParam(PARAM_BLUE, true))
                  {
                      int ledId = request->getParam(PARAM_ID, true)->value().toInt();
                      int ledRed = request->getParam(PARAM_RED, true)->value().toInt();
                      int ledGreen = request->getParam(PARAM_GREEN, true)->value().toInt();
                      int ledBlue = request->getParam(PARAM_BLUE, true)->value().toInt();
                      if (ledId >= NUM_LEDS)
                      {
                          request->send(500, "text/plain", "wrong LED index");
                          return;
                      }
                      if (ledRed < 0 || ledRed > 255)
                      {
                          request->send(500, "text/plain", "wrong red value");
                          return;
                      }
                      if (ledGreen < 0 || ledGreen > 255)
                      {
                          request->send(500, "text/plain", "wrong red value");
                          return;
                      }
                      if (ledBlue < 0 || ledBlue > 255)
                      {
                          request->send(500, "text/plain", "wrong red value");
                          return;
                      }
                      leds[ledId] = CRGB(ledRed, ledGreen, ledBlue);
                      FastLED.show();
                  }
                  else
                  {
                      request->send(500, "text/plain", "set LED id, red, blue, green parameter");
                  }
              });
    server.on("/settings", HTTP_GET, [&](AsyncWebServerRequest *request)
              {
                  AsyncResponseStream *response = request->beginResponseStream("application/json");
                  response->printf("{");
                  response->printf("\"chip id\": \"%s\", ", (const char *)(String((uint32_t)ESP.getEfuseMac(), HEX)).c_str());
                  response->printf("\"ssid\": \"%s\", ", (const char *)WiFi.SSID().c_str());
                  response->printf("\"ip\": \"%s\",", (const char *)WiFi.localIP().toString().c_str());
                  response->printf("\"mac\": \"%s\",", (const char *)WiFi.macAddress().c_str());
                  response->printf("\"uptime\": \"%lu\",", (unsigned long)(esp_timer_get_time() / 1000));
                  response->printf("\"leds\": \"%u\",", NUM_LEDS);
                  response->printf("\"data pin\": \"%u\"", DATA_PIN);
                  response->printf("}");
                  request->send(response);
              });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    server.begin();

    clearLEDs();
    delay(1000);
}

void loop()
{
    drd->loop();
    ws.cleanupClients();
}

void clearLEDs()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = CRGB::Black;
    }
    FastLED.show();
}
