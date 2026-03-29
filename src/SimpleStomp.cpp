#include "SimpleStomp.h"

SimpleStomp *SimpleStomp::instance = nullptr;

SimpleStomp::SimpleStomp(const char *host, uint16_t port, const char *path,
                         const char *user, const char *pass)
{
  _host = host;
  _port = port;
  _path = path;
  _user = user;
  _pass = pass;

  instance = this;
}

void SimpleStomp::begin()
{
  webSocket.beginSSL(_host, _port, _path);
  webSocket.onEvent(webSocketEventStatic);
  webSocket.setReconnectInterval(5000);
}

void SimpleStomp::loop()
{
  webSocket.loop();
}

bool SimpleStomp::isConnected()
{
  return _connected;
}

void SimpleStomp::onMessage(std::function<void(String, String)> callback)
{
  _messageCallback = callback;
}

void SimpleStomp::connect()
{
  String frame = "CONNECT\n";
  frame += "accept-version:1.2\n";
  frame += "host:/\n";
  frame += "login:" + String(_user) + "\n";
  frame += "passcode:" + String(_pass) + "\n\n";
  frame += '\0';

  sendFrame(frame);
}

void SimpleStomp::subscribe(const String &destination)
{
  String frame = "SUBSCRIBE\n";
  frame += "id:" + destination + "\n";
  frame += "destination:" + destination + "\n\n";
  frame += '\0';

  sendFrame(frame);
}

void SimpleStomp::send(const String &destination, const String &message)
{
  String frame = "SEND\n";
  frame += "destination:" + destination + "\n";
  frame += "content-type:text/plain\n\n";
  frame += message;
  frame += '\0';

  sendFrame(frame);
}

void SimpleStomp::sendFrame(String frame)
{
  webSocket.sendTXT(frame);
}

String SimpleStomp::extractBody(const String &frame)
{
  int index = frame.indexOf("\n\n");
  if (index == -1)
    return "";

  String body = frame.substring(index + 2);
  body.replace("\0", "");
  return body;
}

String SimpleStomp::extractHeader(const String &frame, const String &key)
{
  int start = frame.indexOf(key + ":");
  if (start == -1)
    return "";

  int end = frame.indexOf("\n", start);
  return frame.substring(start + key.length() + 1, end);
}

void SimpleStomp::webSocketEventStatic(WStype_t type, uint8_t *payload, size_t length)
{
  if (instance)
  {
    instance->webSocketEvent(type, payload, length);
  }
}

void SimpleStomp::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{

  switch (type)
  {

  case WStype_DISCONNECTED:
    Serial.println("[STOMP] Disconnected");
    _connected = false;
    break;

  case WStype_CONNECTED:
    Serial.println("[STOMP] WebSocket Connected");
    connect();
    break;

  case WStype_TEXT:
  {
    String msg = String((char *)payload);

    if (msg.startsWith("CONNECTED"))
    {
      Serial.println("[STOMP] Connected!");
      _connected = true;
    }
    else if (msg.startsWith("MESSAGE"))
    {

      String body = extractBody(msg);
      String destination = extractHeader(msg, "destination");
      Serial.println("[STOMP] MESSAGE received from: " + destination + ", body: " + body);
      if (_messageCallback)
      {
        _messageCallback(destination, body); // ✅ FIXED
      }
    }
    break;
  }

  case WStype_ERROR:
    Serial.println("[STOMP] Error");
    break;

  default:
    break;
  }
}