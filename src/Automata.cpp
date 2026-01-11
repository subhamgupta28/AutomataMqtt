#include "Automata.h"

Automata *Automata::instance = nullptr;

Automata::Automata(String deviceName, String category, const char *HOST, int PORT)
    : deviceName(deviceName),
      category(category),
      HOST(HOST),
      PORT(PORT),
      MQTT_HOST(HOST),
      MQTT_PORT(1883),
      server(80),
      events("/events"),
      mqttClient(espClient)
{
    instance = this;
}

Automata::Automata(String deviceName, String category, const char *HOST, int PORT, const char *MQTT_HOST, int MQTT_PORT)
    : deviceName(deviceName),
      category(category),
      HOST(HOST),
      PORT(PORT),
      MQTT_HOST(MQTT_HOST),
      MQTT_PORT(MQTT_PORT),
      server(80),
      events("/events"),
      mqttClient(espClient)
{
    instance = this;
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
    bool ret;
    if (USE_HTTPS)
    {
        ret = sendHttps(jsonString, "wifiList", res);
    }
    else
    {
        ret = sendHttp(jsonString, "wifiList", res);
    }

    if (ret)
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
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    preferences.begin("my-app", false);
    wifiMulti.addAP("LAN-D", "Jio@12345");
    wifiMulti.addAP("Net2.4", "12345678");
    // wifiMulti.addAP("akhil_b204", "204204204");
    wifiMulti.addAP("wifi_NET", "444555666");
    macAddr = getMacAddress();
    getConfig();

    xTaskCreate([](void *params)
                { static_cast<Automata *>(params)->keepWiFiAlive(); },
                "keepWiFiAlive", 6096, this, 3, NULL);
}

void Automata::getConfig()
{
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

bool Automata::sendHttps(const String &output, const String &endpoint, String &result)
{
    static WiFiClientSecure client;
    static HTTPClient http;
    result = "";

    String url = "https://" + String(HOST) + "/api/v1/main/" + endpoint;
    Serial.printf("[HTTP] Sending to: %s\n", url.c_str());
    Serial.printf("[MEM] Free heap before: %u\n", ESP.getFreeHeap());

    client.setInsecure();
    // client.setBufferSizes(512, 512);

    if (!http.begin(client, url))
    {
        Serial.println("[HTTP] http.begin() failed");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    int httpCode = http.POST(output);
    if (httpCode > 0)
    {
        result = http.getString();
        Serial.printf("[HTTP] Code: %d, Result length: %u\n", httpCode, result.length());
    }
    else
    {
        Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
    Serial.printf("[MEM] Free heap after: %u\n", ESP.getFreeHeap());
    return (httpCode >= 200 && httpCode < 300);
}

bool Automata::sendHttp(const String &output, const String &endpoint, String &result)
{

    HTTPClient http;
    result = "";

    http.begin("http://" + String(HOST) + ":" + String(PORT) + "/api/v1/main/" + endpoint);
    // http.begin("http://" + String(HOST) + "/api/v1/main/" + endpoint);
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
                if (USE_REGISTER_DEVICE)
                {
                    configTime((int)(5.5 * 3600), 0, ntpServer);
                    registerDevice();
                }

                Serial.printf("IP address: ");
                Serial.println(WiFi.localIP());
                if (!MDNS.begin(convertToLowerAndUnderscore(deviceName).c_str()))
                {
                    Serial.println("Error starting mDNS");
                    return;
                }

                // Advertise a custom service
                MDNS.addService("esp32", "tcp", 8080); // <--- your service
                MDNS.addServiceTxt("esp32", "tcp", "deviceId", deviceId);
                MDNS.addServiceTxt("esp32", "tcp", "ip", WiFi.localIP().toString());
                if (USE_WEBSERVER)
                {
                    handleWebServer();
                }

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
            loop();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
long regTime = millis();
long regWait = 30000;
void Automata::loop()
{

    unsigned long currentMillis = millis();
    if (!mqttClient.connected() && isDeviceRegistered && USE_REGISTER_DEVICE)
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
    if (!isDeviceRegistered && (currentMillis - regTime) >= regWait && USE_REGISTER_DEVICE)
    {
        registerDevice();
        regTime = currentMillis;
    }
}

void Automata::setOTA()
{
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

    JsonDocument doc;
    doc["name"] = deviceName;
    doc["deviceId"] = deviceId;
    doc["type"] = "sensor";
    doc["category"] = category;
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
    bool ret;
    if (USE_HTTPS)
    {
        ret = sendHttps(jsonString, "register", res);
    }
    else
    {
        ret = sendHttp(jsonString, "register", res);
    }

    if (ret)
    {
        JsonDocument resp;
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

void Automata::useServerCreds()
{
    JsonDocument doc;
    doc["mqtt"] = true;
    doc["deviceId"] = deviceId;
    String jsonString;
    serializeJson(doc, jsonString);
    String res;

    bool ret;
    if (USE_HTTPS)
    {
        ret = sendHttps(jsonString, "serverCreds", res);
    }
    else
    {
        ret = sendHttp(jsonString, "serverCreds", res);
    }
    JsonDocument resp;
    Serial.print(res);
    if (deserializeJson(resp, res) == DeserializationError::Ok)
    {
        MQTT_HOST = resp["MQTT_HOST"].as<String>().c_str();
        MQTT_PORT = resp["MQTT_PORT"].as<int>();
    }
}

void Automata::mqttConnect()
{
    if (USE_SERVER_CREDS)
    {
        useServerCreds();
    }
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback([](char *topic, byte *payload, unsigned int length)
                           { Automata::instance->mqttCallback(topic, payload, length); });

    mqttClient.setBufferSize(2048);
    mqttClient.setKeepAlive(30);
    if (!mqttClient.connected())
    {
        String clientId = "automata-" + convertToLowerAndUnderscore(deviceName) + "-" + macAddr;
        Serial.printf("[Automata] Connecting as clientId: %s\n", clientId.c_str());
        if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword))
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
        JsonDocument doc;
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
    String updateTopic = makeTopic("update/" + deviceId);
    String actionTopic = makeTopic("action/" + deviceId);
    mqttClient.subscribe(updateTopic.c_str(), 1);
    mqttClient.subscribe(actionTopic.c_str(), 1);
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
