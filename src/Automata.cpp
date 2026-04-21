#include "Automata.h"

Automata *Automata::instance = nullptr;

// ─────────────────────────────────────────────────────────────
//  Constructors
//  Previously created a SimpleStomp* here — now we create a
//  MQTTWebSocket* only when wsConnect() is actually called so
//  we know the host/port/ssl settings are finalised.
// ─────────────────────────────────────────────────────────────
Automata::Automata(String deviceName, String category,
                   const char *HOST, int PORT)
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

Automata::Automata(String deviceName, String category,
                   const char *HOST, int PORT,
                   const char *MQTT_HOST, int MQTT_PORT)
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

// ─── Transport selection ─────────────────────────────────────
void Automata::useHTTPS() { USE_HTTPS = true; }
void Automata::useCreds() { USE_SERVER_CREDS = true; }
void Automata::useMQTT() { transport = TRANSPORT_MQTT; }
void Automata::useWSS() { transport = TRANSPORT_WSS; }

// ─── Publish (unified) ───────────────────────────────────────
void Automata::publish(const String &topic, const String &payload, bool retained)
{
    if (transport == TRANSPORT_MQTT)
    {
        mqttClient.publish(topic.c_str(), payload.c_str(), retained);
    }
    else
    {
        // MQTT-WS: topic is already a flat MQTT topic (e.g. "topic/sendData")
        if (mqttWS)
            mqttWS->publish(topic, payload, retained);
    }
}

// ─── Error handler ───────────────────────────────────────────
void Automata::handleError(String error)
{
    Serial.println("[Automata] ERROR: " + error);
}

// ─────────────────────────────────────────────────────────────
//  WSS (MQTT-over-WebSocket) — replaces SimpleStomp connect
// ─────────────────────────────────────────────────────────────
void Automata::wsConnect()
{
    Serial.println("[Automata] Connecting via MQTT-over-WebSocket");

    // Lazily create the client the first time
    if (!mqttWS)
        mqttWS = new MQTTWebSocket();

    // Build a unique clientId
    String clientId = "automata-" + convertToLowerAndUnderscore(deviceName) + "-" + macAddr;
    mqttWS->setCredentials(clientId.c_str(), mqttUser, mqttPassword);

    // ── Callbacks ────────────────────────────
    mqttWS->onConnect([this]()
                      {
                          Serial.println("[Automata] MQTT-WS connected");
                          wsSubscribed = false; // force re-subscribe
                      });

    mqttWS->onDisconnect([this]()
                         {
        Serial.println("[Automata] MQTT-WS disconnected");
        wsSubscribed = false; });

    mqttWS->onMessage([this](const String &topic, const String &payload)
                      {
        Serial.println("[Automata] MQTT-WS ← [" + topic + "] " + payload);

        // Mirror the same routing logic that used to live in handleWSSMessage()
        // but now driven by topic string, not STOMP destination path.
        if (topic.indexOf("update") != -1)
            handleUpdate(payload);
        else if (topic.indexOf("action") != -1)
            handleAction(payload); });

    // ssl=true for wss:// through Cloudflare tunnel (port 443)
    // ssl=false for plain ws:// on a local network (port 9001)
    bool ssl = USE_HTTPS;
    int port = ssl ? 443 : MQTT_PORT;
    mqttWS->begin(MQTT_HOST, port, "/mqtt", ssl);
}

// ─── Subscribe device topics (MQTT flat topics) ──────────────
//  Old STOMP version used: /exchange/amq.topic/action.update.<id>
//  New MQTT version uses:  topic/update/<id>  and  topic/action/<id>
// ─────────────────────────────────────────────────────────────
void Automata::wsSubscribeTopics()
{
    if (!mqttWS)
        return;

    String updateTopic = makeTopic("update/" + deviceId);
    String actionTopic = makeTopic("action/" + deviceId);

    mqttWS->subscribe(updateTopic, 1); // QoS 1 for reliability
    mqttWS->subscribe(actionTopic, 1);

    Serial.println("[Automata] MQTT-WS subscribed to:");
    Serial.println("  " + updateTopic);
    Serial.println("  " + actionTopic);

    wsSubscribed = true;
}

// ─── Shared message handlers (unchanged logic) ───────────────
void Automata::handleUpdate(const String &msg)
{
    String res = msg;
    JsonDocument resp = parseString(res);
    String output;
    serializeJson(resp, output);
    Serial.println("[Automata] Update received: " + output);

    deviceId = resp["id"].as<String>();
    preferences.putString("deviceId", deviceId);
    preferences.putString("config", output);
    getConfig();
}

void Automata::handleAction(const String &msg)
{
    String res = msg;
    String topic = makeTopic("action/" + deviceId);

    // Reuse the same mqttCallback path so action handling is unified
    mqttCallback(
        (char *)topic.c_str(),
        (byte *)res.c_str(),
        res.length());
}

// ─── begin() ─────────────────────────────────────────────────
void Automata::begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    preferences.begin("my-app", false);
    wifiMulti.addAP("LAN-D", "Jio@12345");
    wifiMulti.addAP("Net2.4", "12345678");
    wifiMulti.addAP("Ganda6969", "mohit@12345");
    wifiMulti.addAP("wifi_NET", "444555666");
    macAddr = getMacAddress();
    getConfig();

    xTaskCreate([](void *params)
                { static_cast<Automata *>(params)->keepWiFiAlive(); },
                "keepWiFiAlive", 12384, this, 3, NULL);
}

void Automata::getConfig()
{
    String sv = preferences.getString("config", "");
    if (sv != "")
    {
        Serial.println("[Automata] Config found in preferences: " + sv);
        JsonDocument resp;
        deserializeJson(resp, sv);
    }
    else
    {
        Serial.println("[Automata] No config in preferences");
    }
}

// ─── loop() ──────────────────────────────────────────────────
void Automata::loop()
{
    unsigned long currentMillis = millis();

    // ── TCP MQTT path ─────────────────────────
    if (transport == TRANSPORT_MQTT)
    {
        if (!mqttClient.connected() && isDeviceRegistered && USE_REGISTER_DEVICE)
            mqttConnect();
        else if (mqttClient.connected())
            mqttClient.loop();
    }

    // ── MQTT-over-WebSocket path ───────────────
    if (transport == TRANSPORT_WSS && mqttWS)
    {
        mqttWS->loop();

        // Subscribe once the MQTT session is established
        if (mqttWS->connected() && !wsSubscribed)
        {
            Serial.println("[Automata] MQTT-WS session ready, subscribing topics...");
            wsSubscribeTopics();
        }

        if (!mqttWS->connected())
            wsSubscribed = false;
    }

    ArduinoOTA.handle();

    if (currentMillis - previousMillis >= (unsigned long)getDelay())
    {
        if (_handleDelay)
            _handleDelay();
        previousMillis = millis();
    }

    static long regTime = millis();
    static long regWait = 30000;
    if (!isDeviceRegistered &&
        (currentMillis - regTime) >= regWait &&
        USE_REGISTER_DEVICE)
    {
        registerDevice();
        regTime = currentMillis;
    }
}

// ─── keepWiFiAlive (FreeRTOS task) ───────────────────────────
void Automata::keepWiFiAlive()
{
    const TickType_t delayConnected = pdMS_TO_TICKS(30000);
    const TickType_t delayDisconnected = pdMS_TO_TICKS(5000);

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[Automata] WiFi not connected, trying...");
            if (wifiMulti.run() == WL_CONNECTED)
            {
                Serial.println("[Automata] WiFi connected: " + WiFi.localIP().toString());

                if (USE_REGISTER_DEVICE)
                {
                    configTime((int)(5.5 * 3600), 0, ntpServer);
                    registerDevice();
                }

                if (!MDNS.begin(convertToLowerAndUnderscore(deviceName).c_str()))
                    Serial.println("[Automata] mDNS error");
                MDNS.addService("esp32", "tcp", 8080);
                MDNS.addServiceTxt("esp32", "tcp", "deviceId", deviceId);
                MDNS.addServiceTxt("esp32", "tcp", "ip", WiFi.localIP().toString());

                if (USE_WEBSERVER)
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
            loop();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─── registerDevice ──────────────────────────────────────────
void Automata::registerDevice()
{
    static uint8_t retryCount = 0;
    static unsigned long lastAttempt = 0;

    Serial.printf("[Automata] Registering device (attempt %d)...\n", retryCount + 1);

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
    bool ret = USE_HTTPS ? sendHttps(jsonString, "register", res)
                         : sendHttp(jsonString, "register", res);

    if (ret)
    {
        JsonDocument resp;
        if (deserializeJson(resp, res) == DeserializationError::Ok)
        {
            deviceId = resp["id"].as<String>();
            isDeviceRegistered = true;
            retryCount = 0;
            preferences.putString("deviceId", deviceId);
            Serial.println("[Automata] Device registered, id=" + deviceId);

            vTaskDelay(pdMS_TO_TICKS(200));

            if (transport == TRANSPORT_WSS)
                wsConnect();
            else
                mqttConnect();
        }
    }
    else
    {
        retryCount++;
        Serial.printf("[Automata] Registration failed (attempt %d)\n", retryCount);
        if (retryCount > 8)
            Serial.println("[Automata] Max retries reached");
    }

    lastAttempt = millis();
}

// ─── TCP MQTT ─────────────────────────────────────────────────
void Automata::mqttConnect()
{
    if (USE_SERVER_CREDS)
        useServerCreds();

    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback([](char *topic, byte *payload, unsigned int length)
                           { Automata::instance->mqttCallback(topic, payload, length); });
    mqttClient.setBufferSize(2048);
    mqttClient.setKeepAlive(30);

    if (!mqttClient.connected())
    {
        String clientId = "automata-" + convertToLowerAndUnderscore(deviceName) + "-" + macAddr;
        Serial.printf("[Automata] MQTT connecting as: %s\n", clientId.c_str());

        if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword))
        {
            Serial.println("[Automata] MQTT connected");
            subscribeToDeviceTopics();
        }
        else
        {
            Serial.printf("[Automata] MQTT failed, state=%d\n", mqttClient.state());
        }
    }
}

void Automata::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.printf("[Automata] mqttCallback() topic=%s\n", topic);
    String msg;
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    String topicStr = String(topic);
    if (topicStr.endsWith("update/" + deviceId))
    {
        handleUpdate(msg);
    }
    else if (topicStr.endsWith("action/" + deviceId))
    {
        Serial.println("[Automata] Action received");
        JsonDocument resp = parseString(msg);
        Action action{resp};
        if (_handleAction)
            _handleAction(action);

        bool rebootFlag = action.data["reboot"] | false;

        JsonDocument ack;
        ack["key"] = "actionAck";
        ack["actionAck"] = "Success";
        ack["status"] = "ok";
        ack["_cid"] = action.data["_cid"] | "";
        String ackStr;
        serializeJson(ack, ackStr);

        // ACK back via whichever transport is active
        publish(makeTopic("ackAction"), ackStr);
        Serial.println("[Automata] Action ACK sent");

        if (rebootFlag)
        {
            Serial.println("[Automata] Reboot flag — restarting");
            if (transport == TRANSPORT_MQTT)
                mqttClient.disconnect();
            else if (mqttWS)
                mqttWS->disconnect();
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
    Serial.printf("[Automata] Subscribed: %s, %s\n",
                  updateTopic.c_str(), actionTopic.c_str());

    // LWT — mark device offline on unexpected disconnect
    JsonDocument doc;
    doc["status"] = "offline";
    String payload = serializeJsonDoc(doc);
    mqttClient.publish("status", payload.c_str(), true);
}

// ─── makeTopic ───────────────────────────────────────────────
//  TCP MQTT:  subtopic as-is  (broker routes by topic directly)
//  MQTT-WS:   "topic/<subtopic>"
//
//  Old STOMP mapping for reference:
//    STOMP publish:   /topic/<subtopic>
//    STOMP subscribe: /exchange/amq.topic/action.update.<id>
//  These are gone — HiveMQ CE uses flat MQTT topics natively.
// ─────────────────────────────────────────────────────────────
String Automata::makeTopic(const String &subtopic)
{
    return subtopic;
}

// ─── sendLive / sendData / sendAction ────────────────────────
void Automata::sendLive(JsonDocument data)
{
    String payload = serializeJsonDoc(data);
    publish(makeTopic("sendLiveData"), payload, false);

    String json;
    serializeJson(data, json);
    events.send(json.c_str(), "live", millis());
}

void Automata::sendData(JsonDocument doc)
{
    publish(makeTopic("sendData"), serializeJsonDoc(doc));
}

void Automata::sendAction(JsonDocument doc)
{
    Serial.print("[Automata] sendAction(): ");
    publish(makeTopic("action"), serializeJsonDoc(doc));
}

// ─── Misc helpers ─────────────────────────────────────────────
void Automata::addAttribute(String key, String displayName,
                            String unit, String type, JsonDocument extras)
{
    attributeList.push_back({key, displayName, unit, type, extras});
}

void Automata::onActionReceived(HandleAction cb) { _handleAction = cb; }
void Automata::delayedUpdate(HandleDelay hd) { _handleDelay = hd; }
int Automata::getDelay() { return d; }

String Automata::getMacAddress() { return WiFi.macAddress(); }
Preferences Automata::getPreferences() { return preferences; }

String Automata::convertToLowerAndUnderscore(String input)
{
    String output = "";
    for (int i = 0; i < (int)input.length(); i++)
    {
        char ch = input.charAt(i);
        output += (ch == ' ') ? '_' : toLowerCase(ch);
    }
    return output;
}

char Automata::toLowerCase(char c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

String Automata::serializeJsonDoc(JsonDocument &doc)
{
    doc["device_id"] = deviceId;
    String output;
    serializeJson(doc, output);
    return output;
}

JsonDocument Automata::parseString(String str)
{
    JsonDocument resp;
    str.trim();
    str.replace("\\", "");
    DeserializationError err = deserializeJson(resp, str);
    if (err)
        Serial.printf("[Automata] JSON parse error: %s\n", err.c_str());
    return resp;
}

void Automata::configureWiFi()
{
    JsonDocument req;
    req["wifi"] = "get";
    String jsonString;
    serializeJson(req, jsonString);
    String res;
    bool ret = USE_HTTPS ? sendHttps(jsonString, "wifiList", res)
                         : sendHttp(jsonString, "wifiList", res);

    if (ret)
        preferences.putString("wifiList", res);
    else
        Serial.println("[Automata] Failed to fetch WiFi list");

    String config = preferences.getString("wifiList", "");
    if (config == "")
        return;

    JsonDocument doc;
    if (deserializeJson(doc, config))
        return;

    for (int i = 1; i <= 5; ++i)
    {
        String keySsid = "wn" + String(i);
        String keyPass = "wp" + String(i);
        if (doc.containsKey(keySsid) && doc[keySsid].as<String>() != "")
        {
            String ssid = doc[keySsid].as<String>();
            String password = doc.containsKey(keyPass) ? doc[keyPass].as<String>() : "";
            wifiMulti.addAP(ssid.c_str(), password.c_str());
        }
    }
}

void Automata::useServerCreds()
{
    JsonDocument doc;
    doc["mqtt"] = true;
    doc["deviceId"] = deviceId;
    String jsonString;
    serializeJson(doc, jsonString);
    String res;
    bool ret = USE_HTTPS ? sendHttps(jsonString, "serverCreds", res)
                         : sendHttp(jsonString, "serverCreds", res);

    JsonDocument resp;
    if (deserializeJson(resp, res) == DeserializationError::Ok)
    {
        MQTT_HOST = resp["MQTT_HOST"].as<String>().c_str();
        MQTT_PORT = resp["MQTT_PORT"].as<int>();
    }
}

bool Automata::sendHttps(const String &output, const String &endpoint, String &result)
{
    static WiFiClientSecure client;
    static HTTPClient http;
    result = "";

    String url = "https://" + String(HOST) + "/api/v1/main/" + endpoint;
    client.setInsecure();
    if (!http.begin(client, url))
        return false;

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    int code = http.POST(output);
    if (code > 0)
        result = http.getString();
    else
        Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(code).c_str());
    http.end();
    return (code >= 200 && code < 300);
}

bool Automata::sendHttp(const String &output, const String &endpoint, String &result)
{
    HTTPClient http;
    result = "";
    http.begin("http://" + String(HOST) + ":" + String(PORT) + "/api/v1/main/" + endpoint);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int code = http.POST(output);
    if (code > 0)
        result = http.getString();
    else
        Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(code).c_str());
    http.end();
    return (code >= 200 && code < 300);
}

void Automata::handleWebServer()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", index_html); });

    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
              { ESP.restart(); request->send(200, "text/html", "ok"); });

    events.onConnect([](AsyncEventSourceClient *client)
                     { Serial.println("[Automata] SSE client connected"); });

    server.on("/action", HTTP_POST, [](AsyncWebServerRequest *) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t)
              {
                  String body = "";
                  for (size_t i = 0; i < len; i++) body += (char)data[i];
                  DynamicJsonDocument doc(512);
                  deserializeJson(doc, body);
                  Action action;
                  action.data = doc;
                  if (Automata::instance->_handleAction)
                      Automata::instance->_handleAction(action);
                  request->send(200, "text/plain", "OK"); });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                  JsonDocument doc;
                  JsonArray arr = doc.createNestedArray("attributes");
                  for (auto &a : Automata::instance->attributeList)
                  {
                      JsonObject obj = arr.createNestedObject();
                      obj["key"]   = a.key;
                      obj["label"] = a.displayName;
                      obj["unit"]  = a.unit;
                      obj["type"]  = a.type;
                  }
                  String res;
                  serializeJson(doc, res);
                  request->send(200, "application/json", res); });

    server.addHandler(&events);
    server.begin();
    Serial.println("[Automata] Web server started");
}

void Automata::setOTA()
{
    ArduinoOTA.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    ArduinoOTA.setPassword("");
    ArduinoOTA.onStart([]()
                       { Serial.println("[Automata] OTA Start"); })
        .onEnd([]()
               { Serial.println("[Automata] OTA End"); })
        .onProgress([](unsigned int p, unsigned int t)
                    { Serial.printf("[Automata] OTA %u%%\r", p / (t / 100)); })
        .onError([](ota_error_t e)
                 { Serial.printf("[Automata] OTA Error[%u]\n", e); });
    ArduinoOTA.begin();
    Serial.println("[Automata] OTA Ready");
}