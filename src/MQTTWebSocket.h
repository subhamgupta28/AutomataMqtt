#pragma once
#include <WebSocketsClient.h>
#include <functional>
#include <vector>
#include <map>

// ─────────────────────────────────────────────
//  MQTT v3.1.1 Packet Types
// ─────────────────────────────────────────────
#define MQTT_CONNECT     0x10
#define MQTT_CONNACK     0x20
#define MQTT_PUBLISH     0x30
#define MQTT_PUBACK      0x40
#define MQTT_SUBSCRIBE   0x82
#define MQTT_SUBACK      0x90
#define MQTT_PINGREQ     0xC0
#define MQTT_PINGRESP    0xD0
#define MQTT_DISCONNECT  0xE0

#define MQTT_QOS0        0x00
#define MQTT_QOS1        0x02

// ── Debug toggle ──────────────────────────────
#define MQTT_WS_DEBUG 1
#if MQTT_WS_DEBUG
  #define MQTTLOG(fmt, ...) Serial.printf("[MQTT-WS] " fmt "\n", ##__VA_ARGS__)
#else
  #define MQTTLOG(...)
#endif

typedef std::function<void(const String& topic, const String& payload)> MQTTMessageCallback;
typedef std::function<void()>                                            MQTTConnectCallback;
typedef std::function<void()>                                            MQTTDisconnectCallback;

class MQTTWebSocket {
public:

  // ─── Config ───────────────────────────────
  void begin(const char* host, uint16_t port, const char* path = "/mqtt",
             bool ssl = true, uint16_t keepAlive = 60) {
    _host      = host;
    _port      = port;
    _path      = path;
    _ssl       = ssl;
    _keepAlive = keepAlive;

    // FIX 1: Set Host + Origin headers
    // Cloudflare validates these; missing = immediate WS close
    String headers = "Host: " + String(host) + "\r\n" +
                     "Origin: " + String(ssl ? "https" : "http") + "://" + String(host);
    _ws.setExtraHeaders(headers.c_str());

    if (ssl) {
      _ws.beginSSL(host, port, path, "", "mqtt");
    } else {
      _ws.begin(host, port, path, "mqtt");
    }

    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
      _onWsEvent(type, payload, length);
    });

    _ws.setReconnectInterval(5000);

    // FIX 2: WS-level heartbeat well under Cloudflare's 100s idle timeout
    _ws.enableHeartbeat(20000, 3000, 2);
  }

  void setCredentials(const char* clientId, const char* user = nullptr,
                      const char* pass = nullptr) {
    _clientId = clientId;
    _user     = user ? String(user) : "";
    _pass     = pass ? String(pass) : "";
  }

  void onMessage(MQTTMessageCallback cb)       { _msgCb = cb; }
  void onConnect(MQTTConnectCallback cb)       { _conCb = cb; }
  void onDisconnect(MQTTDisconnectCallback cb) { _disCb = cb; }

  // ─── Loop ─────────────────────────────────
  void loop() {
    _ws.loop();

    // FIX 3: Deferred MQTT CONNECT — wait one full loop() after WStype_CONNECTED
    // so the SSL layer is fully flushed before writing MQTT bytes
    if (_pendingConnect) {
      _pendingConnect = false;
      _sendConnect();
    }

    // MQTT-level keepalive (independent of WS heartbeat)
    if (_connected && _keepAlive > 0) {
      uint32_t now = millis();
      if (now - _lastPing > (_keepAlive * 1000UL)) {
        _sendPingReq();
        _lastPing = now;
      }
    }
  }

  // ─── Publish ──────────────────────────────
  bool publish(const String& topic, const String& payload,
               bool retain = false, uint8_t qos = 0) {
    if (!_connected) return false;

    std::vector<uint8_t> body;
    _appendString(body, topic);

    if (qos == 1) {
      uint16_t pid = _nextPacketId++;
      body.push_back(pid >> 8);
      body.push_back(pid & 0xFF);
    }

    for (char c : payload) body.push_back((uint8_t)c);

    uint8_t fixedHeader = MQTT_PUBLISH;
    if (retain)   fixedHeader |= 0x01;
    if (qos == 1) fixedHeader |= MQTT_QOS1;

    std::vector<uint8_t> pkt;
    pkt.push_back(fixedHeader);
    _appendVarInt(pkt, body.size());
    pkt.insert(pkt.end(), body.begin(), body.end());

    MQTTLOG("PUBLISH → %s (%d bytes)", topic.c_str(), pkt.size());
    return _wsSend(pkt);
  }

  // ─── Subscribe ────────────────────────────
  bool subscribe(const String& topic, uint8_t qos = 0) {
    _subscriptions[topic] = qos;        // always store for re-sub on reconnect
    if (!_connected) return false;
    return _sendSubscribe(topic, qos);
  }

  bool connected() { return _connected; }

  void disconnect() {
    if (_connected) {
      std::vector<uint8_t> pkt = { MQTT_DISCONNECT, 0x00 };
      _wsSend(pkt);
    }
    _connected      = false;
    _wsReady        = false;
    _pendingConnect = false;
    _ws.disconnect();
  }

private:
  WebSocketsClient _ws;
  const char* _host      = nullptr;
  uint16_t    _port      = 443;
  const char* _path      = "/mqtt";
  bool        _ssl       = true;
  uint16_t    _keepAlive = 60;

  String   _clientId = "esp32";
  String   _user     = "";
  String   _pass     = "";

  bool     _connected      = false;
  bool     _wsReady        = false;
  bool     _pendingConnect = false;
  uint32_t _lastPing       = 0;
  uint16_t _nextPacketId   = 1;

  MQTTMessageCallback    _msgCb;
  MQTTConnectCallback    _conCb;
  MQTTDisconnectCallback _disCb;

  std::map<String, uint8_t> _subscriptions;

  // ─── WebSocket events ─────────────────────
  void _onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

      case WStype_CONNECTED:
        MQTTLOG("WebSocket connected — deferring MQTT CONNECT by one loop()");
        _wsReady        = true;
        _pendingConnect = true;
        break;

      case WStype_DISCONNECTED:
        MQTTLOG("WebSocket disconnected");
        _wsReady        = false;
        _connected      = false;
        _pendingConnect = false;
        if (_disCb) _disCb();
        break;

      // FIX 4: Accept MQTT data on both BIN and TEXT frames
      // Cloudflare may forward CONNACK as a text frame
      case WStype_BIN:
        MQTTLOG("← BIN %d bytes  [0]=0x%02X", length, length ? payload[0] : 0);
        _handlePacket(payload, length);
        break;

      case WStype_TEXT:
        MQTTLOG("← TEXT %d bytes [0]=0x%02X", length, length ? payload[0] : 0);
        _handlePacket(payload, length);
        break;

      case WStype_ERROR:
        MQTTLOG("WebSocket error");
        break;

      case WStype_PING:
        MQTTLOG("WS PING");
        break;

      case WStype_PONG:
        MQTTLOG("WS PONG");
        break;

      default:
        break;
    }
  }

  // ─── MQTT CONNECT ─────────────────────────
  void _sendConnect() {
    MQTTLOG("Sending CONNECT  clientId=%s user=%s",
            _clientId.c_str(), _user.length() ? _user.c_str() : "(none)");

    std::vector<uint8_t> body;
    _appendString(body, "MQTT");    // protocol name
    body.push_back(0x04);           // protocol level 3.1.1

    uint8_t flags = 0x02;           // clean session
    if (_user.length()) flags |= 0x80;
    if (_pass.length()) flags |= 0x40;
    body.push_back(flags);

    body.push_back(_keepAlive >> 8);
    body.push_back(_keepAlive & 0xFF);

    _appendString(body, _clientId);
    if (_user.length()) _appendString(body, _user);
    if (_pass.length()) _appendString(body, _pass);

    std::vector<uint8_t> pkt;
    pkt.push_back(MQTT_CONNECT);
    _appendVarInt(pkt, body.size());
    pkt.insert(pkt.end(), body.begin(), body.end());

    MQTTLOG("CONNECT packet dump (%d bytes):", pkt.size());
    for (size_t i = 0; i < pkt.size(); i++) Serial.printf("%02X ", pkt[i]);
    Serial.println();

    _wsSend(pkt);
  }

  // ─── MQTT SUBSCRIBE ───────────────────────
  bool _sendSubscribe(const String& topic, uint8_t qos) {
    std::vector<uint8_t> body;
    uint16_t pid = _nextPacketId++;
    body.push_back(pid >> 8);
    body.push_back(pid & 0xFF);
    _appendString(body, topic);
    body.push_back(qos);

    std::vector<uint8_t> pkt;
    pkt.push_back(MQTT_SUBSCRIBE);
    _appendVarInt(pkt, body.size());
    pkt.insert(pkt.end(), body.begin(), body.end());

    MQTTLOG("SUBSCRIBE → %s QoS%d pid=%d", topic.c_str(), qos, pid);
    return _wsSend(pkt);
  }

  // ─── Incoming packet parser ───────────────
  void _handlePacket(uint8_t* data, size_t len) {
    if (len < 2) {
      MQTTLOG("Frame too short (%d bytes)", len);
      return;
    }

    uint8_t pktType = data[0] & 0xF0;

    switch (pktType) {

      // ── CONNACK ─────────────────────────
      case MQTT_CONNACK: {
        if (len < 4) { MQTTLOG("CONNACK truncated"); return; }
        uint8_t rc = data[3];
        if (rc == 0x00) {
          MQTTLOG("✅ CONNACK OK");
          _connected = true;
          _lastPing  = millis();
          for (auto& sub : _subscriptions) _sendSubscribe(sub.first, sub.second);
          if (_conCb) _conCb();
        } else {
          const char* reasons[] = {
            "Accepted", "Unacceptable protocol", "Client ID rejected",
            "Server unavailable", "Bad credentials", "Not authorised"
          };
          MQTTLOG("❌ CONNACK refused 0x%02X: %s", rc, rc <= 5 ? reasons[rc] : "Unknown");
        }
        break;
      }

      // ── PUBLISH ─────────────────────────
      case MQTT_PUBLISH: {
        uint8_t qos = (data[0] & 0x06) >> 1;
        size_t  pos = 1;

        // Remaining length varint
        uint32_t remLen = 0, shift = 0;
        do {
          if (pos >= len) return;
          remLen |= (uint32_t)(data[pos] & 0x7F) << shift;
          shift  += 7;
        } while (data[pos++] & 0x80);

        // Topic string
        if (pos + 2 > len) return;
        uint16_t topicLen = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (pos + topicLen > len) return;
        String topic((char*)(data + pos), topicLen);
        pos += topicLen;

        // Packet ID (QoS 1)
        if (qos == 1) {
          if (pos + 2 > len) return;
          uint16_t pid = ((uint16_t)data[pos] << 8) | data[pos + 1];
          pos += 2;
          _sendPubAck(pid);
        }

        // Payload
        String payload((char*)(data + pos), len > pos ? len - pos : 0);
        MQTTLOG("PUBLISH ← [%s] %s", topic.c_str(), payload.c_str());
        if (_msgCb) _msgCb(topic, payload);
        break;
      }

      case MQTT_PUBACK:
        MQTTLOG("PUBACK ←");
        break;

      case MQTT_SUBACK:
        MQTTLOG("✅ SUBACK ← QoS granted=0x%02X", len > 2 ? data[len - 1] : 0xFF);
        break;

      case MQTT_PINGRESP:
        MQTTLOG("🏓 PINGRESP");
        _lastPing = millis();
        break;

      default:
        MQTTLOG("Unknown type=0x%02X len=%d — hex dump:", data[0], len);
        for (size_t i = 0; i < len && i < 16; i++) Serial.printf("%02X ", data[i]);
        Serial.println();
        break;
    }
  }

  // ─── PUBACK ───────────────────────────────
  void _sendPubAck(uint16_t pid) {
    std::vector<uint8_t> pkt = {
      MQTT_PUBACK, 0x02,
      (uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)
    };
    _wsSend(pkt);
  }

  // ─── PINGREQ ──────────────────────────────
  void _sendPingReq() {
    std::vector<uint8_t> pkt = { MQTT_PINGREQ, 0x00 };
    _wsSend(pkt);
    MQTTLOG("🏓 PINGREQ sent");
  }

  // ─── Encode UTF-8 string (2-byte length prefix) ───
  void _appendString(std::vector<uint8_t>& buf, const String& str) {
    uint16_t len = str.length();
    buf.push_back(len >> 8);
    buf.push_back(len & 0xFF);
    for (char c : str) buf.push_back((uint8_t)c);
  }

  // ─── MQTT variable-length integer ─────────
  void _appendVarInt(std::vector<uint8_t>& buf, uint32_t val) {
    do {
      uint8_t b = val & 0x7F;
      val >>= 7;
      if (val) b |= 0x80;
      buf.push_back(b);
    } while (val);
  }

  // ─── Send binary WebSocket frame ──────────
  bool _wsSend(const std::vector<uint8_t>& pkt) {
    if (!_wsReady) {
      MQTTLOG("Send skipped — WS not ready");
      return false;
    }
    bool ok = _ws.sendBIN(pkt.data(), pkt.size());
    if (!ok) MQTTLOG("sendBIN FAILED");
    return ok;
  }
};