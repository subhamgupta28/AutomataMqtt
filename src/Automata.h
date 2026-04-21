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
#include "MQTTWebSocket.h" // ← replaces SimpleStomp.h

#define USE_WEBSERVER 1
#define USE_REGISTER_DEVICE 1

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

enum PubSubTransport
{
    TRANSPORT_MQTT,
    TRANSPORT_WSS
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
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Automata</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Barlow:wght@300;400;600&display=swap" rel="stylesheet" />
  <style>
    :root {
      --bg:        #0a0c0f;
      --surface:   #111519;
      --border:    #272310;
      --accent:    #ffd821;
      --accent2:   #ffb800;
      --danger:    #ff4c4c;
      --text:      #e8ddb5;
      --muted:     #5a5030;
      --mono:      'Share Tech Mono', monospace;
      --sans:      'Barlow', sans-serif;
    }

    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      background: var(--bg);
      color: var(--text);
      font-family: var(--sans);
      font-weight: 400;
      min-height: 100vh;
      overflow-x: hidden;
    }

    /* Scanline overlay */
    body::before {
      content: '';
      position: fixed; inset: 0;
      background: repeating-linear-gradient(
        0deg,
        transparent,
        transparent 2px,
        rgba(0,0,0,0.08) 2px,
        rgba(0,0,0,0.08) 4px
      );
      pointer-events: none;
      z-index: 1000;
    }

    /* ── Header ── */
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 18px 28px;
      border-bottom: 1px solid var(--border);
      background: var(--surface);
    }

    .logo {
      display: flex;
      align-items: center;
      gap: 12px;
    }

    .logo-icon {
      width: 32px; height: 32px;
      border: 2px solid var(--accent);
      border-radius: 6px;
      display: grid;
      place-items: center;
      position: relative;
    }

    .logo-icon::after {
      content: '';
      width: 10px; height: 10px;
      background: var(--accent);
      border-radius: 2px;
      animation: pulse 2s ease-in-out infinite;
    }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50%       { opacity: 0.4; transform: scale(0.75); }
    }

    .logo-name {
      font-family: var(--mono);
      font-size: 18px;
      letter-spacing: 4px;
      color: #fff;
      text-transform: uppercase;
    }

    .status-pill {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 5px 14px;
      border: 1px solid var(--border);
      border-radius: 100px;
      font-family: var(--mono);
      font-size: 13px;
      letter-spacing: 1px;
      color: #a09060;
    }

    .status-dot {
      width: 7px; height: 7px;
      border-radius: 50%;
      background: var(--accent);
      box-shadow: 0 0 6px var(--accent);
      animation: pulse 2s ease-in-out infinite;
    }

    /* ── Main layout ── */
    main {
      padding: 24px 28px;
      max-width: 1100px;
      margin: 0 auto;
    }

    .section-label {
      font-family: var(--mono);
      font-size: 12px;
      letter-spacing: 3px;
      color: #a09060;
      text-transform: uppercase;
      margin-bottom: 14px;
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .section-label::after {
      content: '';
      flex: 1;
      height: 1px;
      background: var(--border);
    }

    /* ── Data grid ── */
    #data-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
      gap: 12px;
      margin-bottom: 32px;
    }

    .card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 16px 18px;
      position: relative;
      overflow: hidden;
      transition: border-color 0.2s, transform 0.2s;
    }

    .card:hover {
      border-color: var(--accent);
      transform: translateY(-2px);
    }

    .card::before {
      content: '';
      position: absolute;
      top: 0; left: 0; right: 0;
      height: 2px;
      background: linear-gradient(90deg, var(--accent), transparent);
      opacity: 0;
      transition: opacity 0.2s;
    }

    .card:hover::before { opacity: 1; }

    .card-key {
      font-size: 12px;
      letter-spacing: 2px;
      text-transform: uppercase;
      color: #a09060;
      margin-bottom: 10px;
      font-family: var(--mono);
    }

    .card-value {
      font-family: var(--mono);
      font-size: 22px;
      font-weight: 700;
      color: #fff;
      line-height: 1;
      word-break: break-all;
      transition: color 0.3s;
    }

    .card-value.updated {
      color: var(--accent);
      text-shadow: 0 0 12px rgba(255, 216, 33, 0.5);
    }

    .card-unit {
      font-family: var(--mono);
      font-size: 13px;
      color: #a09060;
      margin-top: 6px;
    }

    /* ── Actions ── */
    #actions-section { margin-bottom: 28px; }

    #actions {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: center;
    }

    .action-group {
      display: flex;
      flex-direction: column;
      gap: 6px;
    }

    .action-label {
      font-size: 12px;
      letter-spacing: 2px;
      text-transform: uppercase;
      color: #a09060;
      font-family: var(--mono);
    }

    /* Button */
    .btn {
      font-family: var(--mono);
      font-size: 14px;
      font-weight: 600;
      letter-spacing: 2px;
      text-transform: uppercase;
      padding: 10px 22px;
      border-radius: 5px;
      border: 1px solid var(--accent);
      background: transparent;
      color: var(--accent);
      cursor: pointer;
      transition: background 0.15s, color 0.15s, box-shadow 0.15s;
    }

    .btn:hover {
      background: var(--accent);
      color: #000;
      box-shadow: 0 0 16px rgba(255, 216, 33, 0.35);
    }

    .btn:active { transform: scale(0.97); }

    /* Toggle switch */
    .toggle-wrap {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 8px 14px;
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 6px;
    }

    .toggle-label-text {
      font-family: var(--mono);
      font-size: 14px;
      font-weight: 600;
      letter-spacing: 1px;
      color: var(--text);
    }

    .toggle {
      position: relative;
      width: 38px;
      height: 20px;
      flex-shrink: 0;
    }

    .toggle input { display: none; }

    .toggle-track {
      position: absolute; inset: 0;
      background: var(--border);
      border-radius: 20px;
      cursor: pointer;
      transition: background 0.2s;
    }

    .toggle-track::after {
      content: '';
      position: absolute;
      top: 3px; left: 3px;
      width: 14px; height: 14px;
      background: var(--muted);
      border-radius: 50%;
      transition: transform 0.2s, background 0.2s;
    }

    .toggle input:checked + .toggle-track { background: rgba(255, 216, 33, 0.15); border: 1px solid var(--accent); }
    .toggle input:checked + .toggle-track::after { transform: translateX(18px); background: var(--accent); }

    /* Slider */
    .slider-wrap {
      display: flex;
      flex-direction: column;
      gap: 6px;
    }

    .slider {
      -webkit-appearance: none;
      appearance: none;
      width: 200px;
      height: 4px;
      background: var(--border);
      border-radius: 4px;
      outline: none;
      cursor: pointer;
    }

    .slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 14px; height: 14px;
      background: var(--accent);
      border-radius: 50%;
      box-shadow: 0 0 8px rgba(255, 216, 33, 0.5);
    }

    .slider-val {
      font-family: var(--mono);
      font-size: 14px;
      font-weight: 600;
      color: var(--accent);
      text-align: right;
      width: 200px;
    }

    /* ── Loading skeleton ── */
    .skeleton {
      background: linear-gradient(90deg, var(--surface) 25%, var(--border) 50%, var(--surface) 75%);
      background-size: 200% 100%;
      animation: shimmer 1.4s infinite;
      border-radius: 4px;
      height: 26px;
    }

    @keyframes shimmer {
      0%   { background-position: 200% 0; }
      100% { background-position: -200% 0; }
    }

    /* ── Footer ── */
    footer {
      text-align: center;
      padding: 20px;
      font-family: var(--mono);
      font-size: 12px;
      letter-spacing: 2px;
      color: #a09060;
      border-top: 1px solid var(--border);
      margin-top: 40px;
    }

    .restart-btn {
      margin-left: 16px;
      font-family: var(--mono);
      font-size: 10px;
      letter-spacing: 1px;
      padding: 4px 10px;
      border: 1px solid var(--danger);
      border-radius: 4px;
      background: transparent;
      color: var(--danger);
      cursor: pointer;
      vertical-align: middle;
      transition: background 0.15s;
    }

    .restart-btn:hover { background: rgba(255,76,76,0.15); }

    /* ── Responsive ── */
    @media (max-width: 500px) {
      header { padding: 14px 16px; }
      main   { padding: 16px; }
      #data-grid { grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); }
      .slider { width: 140px; }
      .slider-val { width: 140px; }
    }
  </style>
</head>
<body>

<header>
  <div class="logo">
    <div class="logo-icon"></div>
    <span class="logo-name">Automata</span>
  </div>
  <div class="status-pill">
    <div class="status-dot"></div>
    <span id="status-text">LIVE</span>
  </div>
</header>

<main>
  <div id="actions-section" style="display:none">
    <div class="section-label">Controls</div>
    <div id="actions"></div>
  </div>

  <div class="section-label">Telemetry</div>
  <div id="data-grid">
    <div class="card"><div class="card-key">Loading</div><div class="skeleton"></div></div>
    <div class="card"><div class="card-key">Loading</div><div class="skeleton"></div></div>
    <div class="card"><div class="card-key">Loading</div><div class="skeleton"></div></div>
  </div>
</main>

<footer>
  MADE BY SUBHAM
  <button class="restart-btn" onclick="if(confirm('Restart device?')) fetch('/restart')">RESTART</button>
</footer>

<script>
  // ── SSE live data ──────────────────────────────────────────
  const grid = document.getElementById('data-grid');

  if (window.EventSource) {
    const src = new EventSource('/events');

    src.addEventListener('open', () => {
      document.getElementById('status-text').textContent = 'LIVE';
    });

    src.addEventListener('error', () => {
      document.getElementById('status-text').textContent = 'RECONNECTING';
    });

    src.addEventListener('live', e => {
      try {
        const data = JSON.parse(e.data);
        Object.entries(data).forEach(([key, value]) => {
          const valEl  = document.getElementById('val_' + key);
          const toggle = document.getElementById('sw_' + key);
          const slider = document.getElementById('sl_' + key);

          // 1. Data card (telemetry)
          if (valEl) {
            if (valEl.textContent !== String(value)) {
              valEl.textContent = value;
              valEl.classList.remove('updated');
              void valEl.offsetWidth;
              valEl.classList.add('updated');
              setTimeout(() => valEl.classList.remove('updated'), 800);
            }
          }

          // 2. Toggle / SWITCH — reflect live state into checkbox
          if (toggle) {
            const checked = value === true || value === 'true' || value === '1' || value === 1;
            if (toggle.checked !== checked) toggle.checked = checked;
          }

          // 3. Slider — reflect live value (0-255) into range input; show as 0-100%
          if (slider) {
            const num = parseFloat(value);
            if (!isNaN(num) && parseFloat(slider.value) !== num) {
              slider.value = num;
              const sv = document.getElementById('sv_' + key);
              if (sv) sv.textContent = Math.round(num / 255 * 100) + '%';
            }
          }

          // 4. Unknown key — auto-create a telemetry card
          if (!valEl && !toggle && !slider) {
            const card = document.createElement('div');
            card.className = 'card';
            card.innerHTML = '<div class="card-key">' + key + '</div>'
              + '<div class="card-value updated" id="val_' + key + '">' + value + '</div>';
            grid.appendChild(card);
          }
        });
      } catch(err) { console.warn('SSE parse error', err); }
    });
  }

  // ── Action sender ─────────────────────────────────────────
  function sendAction(key, value) {
    const payload = {};
    payload[key] = value;
    fetch('/action', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
  }

  // ── Load UI from /config ───────────────────────────────────
  async function loadUI() {
    try {
      const res  = await fetch('/config');
      const data = await res.json();

      let actionHTML = '';
      let dataHTML   = '';

      data.attributes.forEach(attr => {
        const types = attr.type.split('|');

        if (types.includes('ACTION')) {
          let widget = '';
          if (types.includes('SWITCH')) {
            widget = `
              <div class="toggle-wrap">
                <span class="toggle-label-text">${attr.label}</span>
                <label class="toggle">
                  <input type="checkbox" id="sw_${attr.key}" onchange='sendAction("${attr.key}", this.checked)' />
                  <span class="toggle-track"></span>
                </label>
              </div>`;
          } else if (types.includes('BTN')) {
            widget = `<button class="btn" onclick='sendAction("${attr.key}", true)'>${attr.label}</button>`;
          } else if (types.includes('SLIDER')) {
            widget = `
              <div class="slider-wrap">
                <span class="action-label">${attr.label}</span>
                <input type="range" min="0" max="255" class="slider" id="sl_${attr.key}"
                  oninput='document.getElementById("sv_${attr.key}").textContent=Math.round(this.value/255*100)+"%"'
                  onchange='sendAction("${attr.key}", this.value)' />
                <span class="slider-val" id="sv_${attr.key}">50%</span>
              </div>`;
          }
          actionHTML += widget;
        } else if (types.includes('DATA')) {
          dataHTML += `
            <div class="card">
              <div class="card-key">${attr.label}</div>
              <div class="card-value" id="val_${attr.key}">--</div>
              ${attr.unit ? `<div class="card-unit">${attr.unit}</div>` : ''}
            </div>`;
        }
      });

      // Render data grid
      if (dataHTML) grid.innerHTML = dataHTML;

      // Render actions section
      if (actionHTML) {
        document.getElementById('actions').innerHTML = actionHTML;
        document.getElementById('actions-section').style.display = '';
      }

    } catch(e) {
      grid.innerHTML = '<p style="color:var(--muted);font-family:var(--mono);font-size:12px">Could not load config.</p>';
    }
  }

  loadUI();
</script>
</body>
</html>
)rawliteral";

class Automata
{
public:
    using HandleAction = std::function<void(Action)>;
    using HandleDelay = std::function<void(void)>;

    Automata(String deviceName, String category = "", const char *HOST = "", int PORT = 0);
    Automata(String deviceName, String category = "", const char *HOST = "", int PORT = 0,
             const char *MQTT_HOST = "", int MQTT_PORT = 0);

    void begin();
    Preferences getPreferences();
    void addAttribute(String key, String displayName, String unit,
                      String type = "INFO", JsonDocument extras = JsonDocument());
    void registerDevice();
    void sendLive(JsonDocument data);
    void sendData(JsonDocument doc);
    void sendAction(JsonDocument doc);
    void onActionReceived(HandleAction cb);
    void handleError(String error);
    void wsSubscribeTopics();
    void delayedUpdate(HandleDelay hd);
    void handleUpdate(const String &msg);
    void handleAction(const String &msg);
    bool isConnected();
    void useMQTT();
    void useWSS();
    void useCreds();
    void useHTTPS();
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

    String mqttBaseTopic = "topic";
    const char *mqttUser = "admin";
    const char *mqttPassword = "admin";

    HandleAction _handleAction = nullptr;
    HandleDelay _handleDelay = nullptr;

    std::vector<Attribute> attributeList;
    unsigned long previousMillis = 0;
    int d = 60000;
    const char *ntpServer = "pool.ntp.org";

    // ── Helpers ──────────────────────────────
    String convertToLowerAndUnderscore(String input);
    char toLowerCase(char c);
    void keepWiFiAlive();
    void setOTA();
    bool sendHttp(const String &output, const String &endpoint, String &result);
    bool sendHttps(const String &output, const String &endpoint, String &result);
    void getConfig();

    // ── TCP MQTT (PubSubClient) ───────────────
    void mqttConnect();
    void mqttCallback(char *topic, byte *payload, unsigned int length);
    void subscribeToDeviceTopics();

    // ── MQTT-over-WebSocket (MQTTWebSocket) ───
    MQTTWebSocket *mqttWS = nullptr; // ← replaces SimpleStomp*
    bool wsSubscribed = false;
    void wsConnect();

    // ── Shared helpers ────────────────────────
    void publish(const String &topic, const String &payload, bool retained = false);
    String makeTopic(const String &subtopic);
    String serializeJsonDoc(JsonDocument &doc);
    JsonDocument parseString(String str);

    PubSubTransport transport = TRANSPORT_MQTT;
    bool USE_HTTPS = false;
    bool USE_SERVER_CREDS = false;
};

#endif