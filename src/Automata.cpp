#include "Automata.h"

Automata *Automata::instance = nullptr;

Automata::Automata(String deviceName, const char *HOST, int PORT)
    : deviceName(deviceName),
      HOST(HOST),
      PORT(PORT),
      server(80),
      events("/events"),
      mqttClient(espClient)
{
    instance = this;
    Serial.println("[Automata] Constructor called");
}

String Automata::convertToLowerAndUnderscore(String input)
{
    Serial.println("[Automata] convertToLowerAndUnderscore()");
    String output = "";
    for (int i = 0; i < input.length(); i++)
    {
        char ch = input.charAt(i);
        if (ch == ' ')
            output += '_';
        else
            output += toLowerCase(ch);
    }
    return output;
}

char Automata::toLowerCase(char c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

void Automata::configureWiFi()
{
    Serial.println("[Automata] configureWiFi()");

    JsonDocument req;
    req["wifi"] = "get";
    String jsonString;
    serializeJson(req, jsonString);
    String res;

    if (sendHttp(jsonString, "wifiList", res))
    {
        Serial.println("[Automata] WiFi list fetched from server and stored");
        preferences.putString("wifiList", res);
    }
    else
    {
        Serial.println("[Automata] Failed to fetch WiFi list");
    }

    String config = preferences.getString("wifiList", "");
    if (config == "")
    {
        Serial.println("[Automata] No WiFi config found in preferences");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, config))
    {
        Serial.println("[Automata] Failed to parse WiFi config JSON");
        return;
    }

    for (int i = 1; i <= 5; ++i)
    {
        String keySsid = "wn" + String(i);
        String keyPass = "wp" + String(i);

        if (doc.containsKey(keySsid) && doc[keySsid].as<String>() != "")
        {
            String ssid = doc[keySsid].as<String>();
            String password = doc.containsKey(keyPass) ? doc[keyPass].as<String>() : "";
            wifiMulti.addAP(ssid.c_str(), password.c_str());
            Serial.printf("[Automata] Added AP: %s\n", ssid.c_str());
        }
    }
}

void Automata::begin()
{
    Serial.println("[Automata] begin()");

    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    preferences.begin("my-app", false);
    wifiMulti.addAP("LAN-D", "Jio@12345");
    wifiMulti.addAP("Net2.4", "12345678");
    wifiMulti.addAP("JioFiber-x5hnq", "12341234");
    wifiMulti.addAP("wifi_NET", "444555666");
    macAddr = getMacAddress();
    getConfig();

    xTaskCreate([](void *params)
                { static_cast<Automata *>(params)->keepWiFiAlive(); },
                "keepWiFiAlive", 4096, this, 2, NULL);
}

void Automata::getConfig()
{
    Serial.println("[Automata] getConfig()");
    String sv = preferences.getString("config", "");
    if (sv != "")
    {
        Serial.println("[Automata] Config found in preferences:");
        Serial.println(sv);
        JsonDocument resp;
        deserializeJson(resp, sv);
    }
    else
    {
        Serial.println("[Automata] No config in preferences");
    }
}

void Automata::handleWebServer()
{
    Serial.println("[Automata] handleWebServer()");
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", index_html); });

    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  Serial.println("[Automata] Restart requested from web");
                  ESP.restart();
                  request->send(200, "text/html", "ok"); });

    events.onConnect([](AsyncEventSourceClient *client)
                     {
                         if (client->lastId())
                         {
                             Serial.printf("[Automata] SSE client reconnected, last ID: %u\n", client->lastId());
                         }
                         else
                         {
                             Serial.println("[Automata] New SSE client connected");
                         } });

    server.addHandler(&events);
    server.begin();
    Serial.println("[Automata] Web server started");
}

Preferences Automata::getPreferences()
{
    return preferences;
}

bool Automata::sendHttp(const String &output, const String &endpoint, String &result)
{

    HTTPClient http;
    result = "";

    http.begin("http://" + String(HOST) + ":" + String(PORT) + "/api/v1/main/" + endpoint);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000); // 5 seconds per request

    int httpCode = http.POST(output);

    if (httpCode > 0)
    {
        Serial.printf("[HTTP] POST code: %d\n", httpCode);

        result = http.getString(); // always capture response

        if (httpCode >= 200 && httpCode < 300)
        {
            http.end();
            return true; // success, exit early
        }
        else
        {
            Serial.printf("[HTTP] POST unexpected code: %d, body: %s\n",
                          httpCode, result.c_str());
        }
    }
    else
    {
        Serial.printf("[HTTP] POST attempt failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();

    return false;
}

void Automata::keepWiFiAlive()
{
    Serial.println("[Automata] keepWiFiAlive() task started");

    const TickType_t delayConnected = 30000 / portTICK_PERIOD_MS;
    const TickType_t delayDisconnected = 5000 / portTICK_PERIOD_MS;

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[Automata] WiFi not connected, trying...");
            if (wifiMulti.run() == WL_CONNECTED)
            {
                Serial.println("[Automata] WiFi connected");
                configTime(5.5 * 3600, 0, ntpServer);
                registerDevice();

                handleWebServer();
                setOTA();
            }
            else
            {
                Serial.println("[Automata] WiFi retry...");
                vTaskDelay(delayDisconnected);
            }
        }
        else
        {
            if (!mqttClient.connected() && isDeviceRegistered)
            {
                Serial.println("[Automata] MQTT not connected, reconnecting...");
                mqttConnect();
            }
            vTaskDelay(delayConnected);
        }
    }
}
long regTime = millis();
long regWait = 30000;
void Automata::loop()
{
    unsigned long currentMillis = millis();

    if (wifiMulti.run() == WL_CONNECTED)
    {
        if (!mqttClient.connected() && isDeviceRegistered)
        {
            mqttConnect();
        }
        else
        {
            mqttClient.loop();
        }

        ArduinoOTA.handle();

        if (currentMillis - previousMillis >= getDelay())
        {
            _handleDelay();
            previousMillis = millis();
        }
        // Handle device registration retry
        if (!isDeviceRegistered && (currentMillis - regTime) >= regWait)
        {
            registerDevice();
            regTime = currentMillis;
        }
    }
}

void Automata::setOTA()
{
    Serial.println("[Automata] setOTA()");
    ArduinoOTA.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    ArduinoOTA.setPassword("");
    ArduinoOTA.onStart([]()
                       {
                           String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
                           Serial.println("[Automata] OTA Start updating " + type); })
        .onEnd([]()
               { Serial.println("[Automata] OTA End"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("[Automata] OTA Progress: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 {
                     Serial.printf("[Automata] OTA Error[%u]: ", error);
                     if (error == OTA_AUTH_ERROR)
                         Serial.println("Auth Failed");
                     else if (error == OTA_BEGIN_ERROR)
                         Serial.println("Begin Failed");
                     else if (error == OTA_CONNECT_ERROR)
                         Serial.println("Connect Failed");
                     else if (error == OTA_RECEIVE_ERROR)
                         Serial.println("Receive Failed");
                     else if (error == OTA_END_ERROR)
                         Serial.println("End Failed"); });

    ArduinoOTA.begin();
    Serial.println("[Automata] OTA Ready");
}

void Automata::sendLive(JsonDocument data)
{
    String payload = serializeJsonDoc(data);
    mqttClient.publish(makeTopic("sendLiveData").c_str(), payload.c_str(), 0);

    String json;
    serializeJson(data, json);
    events.send(json.c_str(), "live", millis());
}

void Automata::sendData(JsonDocument doc)
{
    Serial.print("[Automata] sendData(): ");
    String payload = serializeJsonDoc(doc);
    Serial.println(mqttClient.publish(makeTopic("sendData").c_str(), payload.c_str()));
}

void Automata::sendAction(JsonDocument doc)
{
    Serial.print("[Automata] sendAction(): ");
    String payload = serializeJsonDoc(doc);
    Serial.println(mqttClient.publish(makeTopic("action").c_str(), payload.c_str()));
}

String Automata::serializeJsonDoc(JsonDocument &doc)
{
    doc["device_id"] = deviceId;
    String output;
    serializeJson(doc, output);
    return output;
}

void Automata::addAttribute(String key, String displayName, String unit, String type, JsonDocument extras)
{
    Serial.printf("[Automata] addAttribute(%s)\n", key.c_str());
    Attribute atb{key, displayName, unit, type, extras};
    attributeList.push_back(atb);
}

void Automata::registerDevice()
{
    static uint8_t retryCount = 0;
    static unsigned long lastAttempt = 0;

    // if (isDeviceRegistered)
    //     return;

    unsigned long now = millis();
    unsigned long backoff = min(60000UL, (1UL << retryCount) * 1000); // max 60s

    // if (retryCount > 0 && now - lastAttempt < backoff)
    //     return;

    Serial.printf("Registering Device (attempt %d)...\n", retryCount + 1);

    StaticJsonDocument<1024> doc;
    doc["name"] = deviceName;
    doc["deviceId"] = deviceId;
    doc["type"] = "sensor";
    doc["updateInterval"] = d;
    doc["status"] = "ONLINE";
    doc["host"] = String(WiFi.getHostname());
    doc["macAddr"] = macAddr;
    doc["reboot"] = false;
    doc["sleep"] = false;
    doc["accessUrl"] = "http://" + WiFi.localIP().toString();

    JsonArray attributes = doc.createNestedArray("attributes");
    for (auto &attribute : attributeList)
    {
        JsonObject attr = attributes.createNestedObject();
        attr["value"] = "";
        attr["displayName"] = attribute.displayName;
        attr["key"] = attribute.key;
        attr["units"] = attribute.unit;
        attr["type"] = attribute.type;
        attr["extras"] = attribute.extras;
        attr["visible"] = true;
        attr["valueDataType"] = "String";
    }

    String jsonString;
    serializeJson(doc, jsonString);
    String res;

    if (sendHttp(jsonString, "register", res))
    {
        DynamicJsonDocument resp(1024);
        if (deserializeJson(resp, res) == DeserializationError::Ok)
        {
            deviceId = resp["id"].as<String>();
            preferences.putString("deviceId", deviceId);
            isDeviceRegistered = true;
            retryCount = 0;
            Serial.println("Device Registered");

            vTaskDelay(200);
            mqttConnect();
        }
    }
    else
    {
        retryCount++;
        Serial.printf("Device registration failed (attempt %d)\n", retryCount);
        if (retryCount > 8)
        {
            Serial.println("Max retries reached, rebooting...");
            // ESP.restart();
        }
    }

    lastAttempt = now;
}
void Automata::mqttConnect()
{
    Serial.println("[Automata] mqttConnect()");
    mqttClient.setServer(HOST, 1883);
    mqttClient.setCallback([](char *topic, byte *payload, unsigned int length)
                           { Automata::instance->mqttCallback(topic, payload, length); });

    mqttClient.setBufferSize(2048);
    mqttClient.setKeepAlive(30);
    if (!mqttClient.connected())
    {
        String clientId = "automata-" + convertToLowerAndUnderscore(deviceName) + "-" + macAddr;
        Serial.printf("[Automata] Connecting as clientId: %s\n", clientId.c_str());
        JsonDocument doc;
        doc["status"] = "offline";
        String payload = serializeJsonDoc(doc);
        if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword, "automata/status", 0, true, payload.c_str()))
        {
            Serial.println("[Automata] MQTT Connected");
            subscribeToDeviceTopics();
        }
        else
        {
            Serial.printf("[Automata] MQTT connection failed, state=%d\n", mqttClient.state());
        }
    }
    else
    {
        Serial.println("[Automata] Already connected to MQTT");
    }
}

void Automata::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.printf("[Automata] mqttCallback() -> topic: %s\n", topic);
    String msg;
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];
    Serial.printf("[Automata] Payload: %s\n", msg.c_str());

    String topicStr = String(topic);
    if (topicStr.endsWith("/update/" + deviceId))
    {
        Serial.println("[Automata] Update message received");
        JsonDocument resp = parseString(msg);
        serializeJson(resp, msg);
        deviceId = resp["id"].as<String>();
        preferences.putString("deviceId", deviceId);
        preferences.putString("config", msg);
        getConfig();
    }
    else if (topicStr.endsWith("/action/" + deviceId))
    {
        Serial.println("[Automata] Action message received");
        JsonDocument resp = parseString(msg);
        Action action{resp};
        _handleAction(action);

        bool rebootFlag = action.data["reboot"] | false;
        DynamicJsonDocument doc(256);
        doc["key"] = "actionAck";
        doc["actionAck"] = "Success";
        String ackStr;
        serializeJson(doc, ackStr);
        mqttClient.publish(makeTopic("ackAction").c_str(), ackStr.c_str());
        Serial.println("[Automata] Action ACK sent");

        if (rebootFlag)
        {
            Serial.println("[Automata] Reboot flag received, restarting...");
            mqttClient.disconnect();
            delay(200);
            ESP.restart();
        }
    }
}

void Automata::subscribeToDeviceTopics()
{
    Serial.println("[Automata] subscribeToDeviceTopics()");
    String updateTopic = makeTopic("update/" + deviceId);
    String actionTopic = makeTopic("action/" + deviceId);
    mqttClient.subscribe(updateTopic.c_str());
    mqttClient.subscribe(actionTopic.c_str());
    Serial.printf("[Automata] Subscribed to: %s, %s\n", updateTopic.c_str(), actionTopic.c_str());
    JsonDocument doc;
    doc["status"] = "offline";
    String payload = serializeJsonDoc(doc);
    mqttClient.publish("automata/status", payload.c_str(), true);
}

String Automata::makeTopic(const String &subtopic)
{
    return mqttBaseTopic + "/" + subtopic;
}

JsonDocument Automata::parseString(String str)
{
    JsonDocument resp;
    str.trim();
    DeserializationError err = deserializeJson(resp, str);
    if (err)
        Serial.printf("[Automata] JSON parse error: %s\n", err.c_str());
    return resp;
}

String Automata::getMacAddress()
{
    return WiFi.macAddress();
}

void Automata::onActionReceived(HandleAction cb)
{
    _handleAction = cb;
}

void Automata::delayedUpdate(HandleDelay hd)
{
    _handleDelay = hd;
}

int Automata::getDelay()
{

    return d;
}
