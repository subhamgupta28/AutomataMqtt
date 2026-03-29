#ifndef SIMPLE_STOMP_H
#define SIMPLE_STOMP_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>   // ✅ IMPORTANT

class SimpleStomp {
  public:
    SimpleStomp(const char* host, uint16_t port, const char* path,
                const char* user, const char* pass);

    void begin();
    void loop();

    void connect();
    void subscribe(const String& destination);
    void send(const String& destination, const String& message);

    bool isConnected();

    // ✅ FIXED: use std::function
    void onMessage(std::function<void(String destination, String msg)> callback);

  private:
    WebSocketsClient webSocket;

    const char* _host;
    uint16_t _port;
    const char* _path;
    const char* _user;
    const char* _pass;

    bool _connected = false;

    // ✅ FIXED
    std::function<void(String, String)> _messageCallback;

    static void webSocketEventStatic(WStype_t type, uint8_t * payload, size_t length);
    void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);

    void sendFrame(String frame);

    String extractBody(const String& frame);
    String extractHeader(const String& frame, const String& key);

    static SimpleStomp* instance;
};

#endif