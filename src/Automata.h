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
#define MQTT_MAX_PACKET_SIZE 2048

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
        body { background-color: #1a1a1a; color: #ffffff; font-family: Arial, sans-serif; }
        h2 { text-align: center; color: #ffffff; }
        table { border-collapse: collapse; width: 80%; margin: auto; background-color: #333333; color: #ffffff; }
        th, td { border: 2px solid #212121; padding: 10px; text-align: left; }
        th { background-color: #222222; }
        tr:nth-child(even) { background-color: #444444; }
        footer { text-align: center; margin-top: 20px; color: #ffffff; }
    </style>
</head>
<body>
    <h2>Automata</h2>
    <div id="data">Loading...</div>
    <script>
        if (!!window.EventSource) {
            var source = new EventSource('/events');
            source.addEventListener('open', function () { console.log("Events Connected"); });
            source.addEventListener('error', function (e) {
                if (e.target.readyState !== EventSource.OPEN) { console.log("Events Disconnected"); }
            });
            source.addEventListener('live', function (e) {
                console.log("Received Data: ", e.data);
                try {
                    let jsonData = JSON.parse(e.data);
                    let table = `<table><tr><th>Key</th><th>Value</th></tr>`;
                    Object.entries(jsonData).forEach(([key, value]) => {
                        table += `<tr><td>${key}</td><td>${value}</td></tr>`;
                    });
                    table += '</table>';
                    document.getElementById('data').innerHTML = table;
                } catch (error) {
                    console.error("Error parsing JSON: ", error);
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

    Automata(String deviceName, const char *HOST, int PORT);

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
    String deviceName;
    const char *HOST;
    int PORT;
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

    // MQTT
    void mqttConnect();
    void mqttCallback(char *topic, byte *payload, unsigned int length);
    String makeTopic(const String &subtopic);
    void subscribeToDeviceTopics();
    String serializeJsonDoc(JsonDocument &doc);
    JsonDocument parseString(String str);
};

#endif
