#ifndef AUTOMATA_H
#define AUTOMATA_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <vector>
#include <ESPmDNS.h>

#define USE_HTTPS 1
#define USE_WEBSERVER 1
#define USE_REGISTER_DEVICE 1
#define USE_SERVER_CREDS 1

#ifndef USE_WEBSERVER
#define USE_WEBSERVER 1
#endif

#ifndef USE_REGISTER_DEVICE
#define USE_REGISTER_DEVICE 1
#endif

struct Action
{
    JsonDocument data;
};

struct Attribute
{
    String key;
    String displayName;
    String unit;
    String type;
    JsonDocument extras;
};

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            background-color: #111;
            color: #fff;
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
        }

        h2 {
            text-align: center;
            margin-bottom: 20px;
            font-size: 28px;
            letter-spacing: 2px;
        }

        #grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
            gap: 18px;
            width: 90%;
            max-width: 900px;
            margin: auto;
        }

        .item {
            background: #222;
            padding: 15px;
            border-radius: 10px;
            text-align: center;
            box-shadow: 0 0 10px rgba(0,0,0,0.4);
        }

        .key {
            font-size: 14px;
            color: #bbbbbb;
        }

        .value {
            font-size: 20px;
            margin-top: 5px;
            font-weight: bold;
        }

        footer {
            text-align: center;
            margin-top: 30px;
            color: #777;
        }
    </style>
</head>

<body>
    <h2>Automata</h2>
    <div id="data">Loading...</div>

    <script>
        if (!!window.EventSource) {
            var source = new EventSource('/events');

            source.addEventListener('open', function() {
                console.log("Events Connected");
            });

            source.addEventListener('error', function(e) {
                if (e.target.readyState !== EventSource.OPEN) {
                    console.log("Events Disconnected");
                }
            });

            source.addEventListener('live', function(e) {
                console.log("Received Data:", e.data);

                try {
                    let jsonData = JSON.parse(e.data);
                    let grid = `<div id='grid'>`;

                    Object.entries(jsonData).forEach(([key, value]) => {
                        grid += `
                            <div class='item'>
                                <div class='key'>${key}</div>
                                <div class='value'>${value}</div>
                            </div>`;
                    });

                    grid += `</div>`;
                    document.getElementById('data').innerHTML = grid;

                } catch (err) {
                    console.error("JSON Parse Error:", err);
                    document.getElementById('data').innerHTML = "<p>Error loading data</p>";
                }
            });
        }
    </script>
</body>

<footer><p>Made by Subham</p></footer>
</html>
)rawliteral";

class Automata
{
public:
    using HandleAction = std::function<void(Action)>;
    using HandleDelay = std::function<void(void)>;

    Automata(String deviceName, String category = "", const char *HOST = "", int PORT = 0);
    Automata(String deviceName, String category = "", const char *HOST = "", int PORT = 0, const char *MQTT_HOST = "", int MQTT_PORT = 0);
    void begin();

    Preferences getPreferences();
    void addAttribute(String key, String displayName, String unit, String type = "INFO", JsonDocument extras = JsonDocument());
    void registerDevice();
    void sendLive(JsonDocument data);
    void sendData(JsonDocument doc);
    void sendAction(JsonDocument doc);
    void onActionReceived(HandleAction cb);
    void delayedUpdate(HandleDelay hd);
    

    int getDelay();

    static Automata *instance;

private:
    void loop();
    void configureWiFi();
    String getMacAddress();
    void handleWebServer();
    void useServerCreds();
    String deviceName;
    String category;
    const char *HOST;
    int PORT;
    const char *MQTT_HOST;
    int MQTT_PORT;
    String deviceId;
    String macAddr;
    bool isDeviceRegistered = false;

    Preferences preferences;
    WiFiMulti wifiMulti;
    AsyncWebServer server;
    AsyncEventSource events;
    WiFiClient espClient;
    PubSubClient mqttClient;
    String mqttBaseTopic = "automata";
    const char *mqttUser = "mqttadmin";
    const char *mqttPassword = "12345678";

    HandleAction _handleAction;
    HandleDelay _handleDelay;
    std::vector<Attribute> attributeList;
    unsigned long previousMillis = 0;
    int d = 60000; // default delay
    const char *ntpServer = "pool.ntp.org";

    // Helpers
    String convertToLowerAndUnderscore(String input);
    char toLowerCase(char c);
    void keepWiFiAlive();
    void setOTA();
    bool sendHttp(const String &output, const String &endpoint, String &result);
    void getConfig();
    String getIdByName(const String &input, const String &searchName);
    bool sendHttps(const String &output, const String &endpoint, String &result);
    // MQTT
    void mqttConnect();
    void mqttCallback(char *topic, byte *payload, unsigned int length);
    String makeTopic(const String &subtopic);
    void subscribeToDeviceTopics();
    String serializeJsonDoc(JsonDocument &doc);
    JsonDocument parseString(String str);
};

#endif
