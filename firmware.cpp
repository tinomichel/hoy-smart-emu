#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <FS.h>
#include <ArduinoJson.h>

// --- Platzhalter für spezifische Bibliotheken ---
// Du musst hier eine Bibliothek für die serielle Kommunikation mit dem Stromzähler (z.B. SML) einfügen.
// Zum Beispiel: #include <SML_Parser.h>

// --- Konfigurationsvariablen ---
// Diese werden über die Web-Oberfläche gespeichert
struct Config {
  char ssid[32] = "dein_WLAN_SSID";
  char password[64] = "dein_WLAN_PASSWORT";
  char mqtt_server[64] = "dein_MQTT_SERVER";
  int mqtt_port = 1883;
  char device_id[32] = "unimeter_esp8266";
};

Config config;
bool shouldSaveConfig = false;

// --- GPIO-Pins ---
const int SML_RX_PIN = D5; // Pin für den seriellen IR-Lesekopf
const int SML_TX_PIN = D6; // Nicht unbedingt nötig, aber zur Vollständigkeit

// --- Objekte ---
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Globale Variablen für die Zählerstände ---
float total_consumption = 0.0;
float current_power = 0.0;
float phase_a_power = 0.0;
float phase_b_power = 0.0;
float phase_c_power = 0.0;

// --- Funktion zur Konfiguration über das Web ---

// Liest die Konfiguration aus der Datei
void loadConfig() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, buf.get());
      if (!error) {
        if (doc.containsKey("ssid")) strncpy(config.ssid, doc["ssid"], sizeof(config.ssid));
        if (doc.containsKey("password")) strncpy(config.password, doc["password"], sizeof(config.password));
        if (doc.containsKey("mqtt_server")) strncpy(config.mqtt_server, doc["mqtt_server"], sizeof(config.mqtt_server));
        if (doc.containsKey("mqtt_port")) config.mqtt_port = doc["mqtt_port"];
        if (doc.containsKey("device_id")) strncpy(config.device_id, doc["device_id"], sizeof(config.device_id));
      }
      configFile.close();
    }
  }
}

// Speichert die Konfiguration in der Datei
void saveConfigCallback() {
  shouldSaveConfig = true;
}

void saveConfig() {
  DynamicJsonDocument doc(1024);
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["mqtt_server"] = config.mqtt_server;
  doc["mqtt_port"] = config.mqtt_port;
  doc["device_id"] = config.device_id;

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
  }
}

// Handler für die Hauptseite
void handleRoot() {
  String content = "<!DOCTYPE html><html lang='de'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  content += "<title>Unimeter Konfiguration</title>";
  content += "<style>body{font-family:sans-serif;background-color:#f0f4f8;padding:20px;}h1{color:#334155;}form{background-color:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}label{display:block;margin-top:10px;font-weight:bold;}input[type='text'],input[type='password'],input[type='number']{width:100%;padding:8px;margin-top:4px;border:1px solid #ccc;border-radius:4px;}button{background-color:#2563eb;color:#fff;padding:10px 15px;border:none;border-radius:4px;cursor:pointer;margin-top:20px;}button:hover{background-color:#1e40af;}p{margin-top:20px;color:#6b7280;}</style>";
  content += "</head><body><h1>Unimeter Konfiguration</h1><p>Status: " + String(WiFi.status() == WL_CONNECTED ? "Verbunden" : "Nicht verbunden") + "</p>";
  content += "<form action='/save' method='post'>";
  content += "<label for='ssid'>WLAN SSID:</label><input type='text' id='ssid' name='ssid' value='" + String(config.ssid) + "'><br>";
  content += "<label for='password'>WLAN Passwort:</label><input type='password' id='password' name='password' value='" + String(config.password) + "'><br>";
  content += "<label for='mqtt_server'>MQTT Server:</label><input type='text' id='mqtt_server' name='mqtt_server' value='" + String(config.mqtt_server) + "'><br>";
  content += "<label for='mqtt_port'>MQTT Port:</label><input type='number' id='mqtt_port' name='mqtt_port' value='" + String(config.mqtt_port) + "'><br>";
  content += "<label for='device_id'>Geräte ID:</label><input type='text' id='device_id' name='device_id' value='" + String(config.device_id) + "'><br>";
  content += "<button type='submit'>Speichern</button></form></body></html>";
  server.send(200, "text/html", content);
}

// Handler für das Speichern der Konfiguration
void handleSave() {
  if (server.hasArg("ssid")) strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
  if (server.hasArg("password")) strncpy(config.password, server.arg("password").c_str(), sizeof(config.password));
  if (server.hasArg("mqtt_server")) strncpy(config.mqtt_server, server.arg("mqtt_server").c_str(), sizeof(config.mqtt_server));
  if (server.hasArg("mqtt_port")) config.mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("device_id")) strncpy(config.device_id, server.arg("device_id").c_str(), sizeof(config.device_id));

  saveConfigCallback();

  String content = "<!DOCTYPE html><html lang='de'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  content += "<title>Gespeichert</title>";
  content += "<style>body{font-family:sans-serif;background-color:#f0f4f8;padding:20px;}h1{color:#334155;}p{background-color:#d4edda;color:#155724;padding:15px;border-radius:5px;}</style>";
  content += "</head><body><h1>Konfiguration gespeichert!</h1><p>Das Gerät wird neu gestartet, um die neuen Einstellungen anzuwenden.</p></body></html>";
  server.send(200, "text/html", content);
  
  delay(1000);
  ESP.restart();
}

// --- Shelly EM3 Pro Emulation ---
void handleShellyStatus() {
  DynamicJsonDocument doc(1024);
  doc["power"] = current_power;
  doc["consumption"] = total_consumption;
  doc["voltage"] = 230.0; // Fester Wert, da der Zähler das meist nicht liefert
  
  // Die folgenden Werte müssen von deinem Zähler ausgelesen werden
  doc["emeters"][0]["power"] = phase_a_power;
  doc["emeters"][0]["energy"] = total_consumption / 3; // Vereinfacht
  doc["emeters"][1]["power"] = phase_b_power;
  doc["emeters"][1]["energy"] = total_consumption / 3;
  doc["emeters"][2]["power"] = phase_c_power;
  doc["emeters"][2]["energy"] = total_consumption / 3;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// --- MQTT Funktionen ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Hier könnten Befehle vom MQTT-Broker verarbeitet werden
  // Zum Beispiel: Konfiguration ändern, Neustart auslösen etc.
}

void reconnectMqtt() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(config.device_id)) {
      // Verbindung erfolgreich
    } else {
      delay(5000); // 5 Sekunden warten vor dem nächsten Versuch
    }
  }
}

// --- Zähler auslesen ---
void readSmartMeter() {
  // Diese Funktion muss implementiert werden, um den Stromzähler über die serielle
  // Schnittstelle auszulesen. Das SML-Protokoll ist komplex.
  // Du musst hier eine SML-Bibliothek integrieren.
  // Beispiel:
  // SML_Parser sml;
  // sml.readData();
  // total_consumption = sml.getObisValue(1.8.0);
  // current_power = sml.getObisValue(1.7.0);
  // phase_a_power = sml.getObisValue(1.7.1);
  // ...
  // Für diesen Rahmen simulieren wir die Werte:
  static unsigned long lastReading = 0;
  if (millis() - lastReading > 5000) { // Werte alle 5 Sekunden simulieren
    current_power = random(500, 3000) / 1000.0;
    phase_a_power = current_power / 3;
    phase_b_power = current_power / 3;
    phase_c_power = current_power / 3;
    total_consumption += current_power * 5.0 / 3600.0; // kWh
    lastReading = millis();
  }
}

// --- Setup-Funktion ---
void setup() {
  Serial.begin(115200);
  delay(100);

  // Starte SPIFFS Dateisystem
  SPIFFS.begin();
  loadConfig();
  
  // Konnektiere mit WLAN
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);

  // Versuche, dich zu verbinden
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 20) {
    delay(500);
    i++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Wenn keine Verbindung, starte den Access Point für die Konfiguration
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Unimeter_Config", "");
    IPAddress myIP = WiFi.softAPIP();
  }
  
  // Richte den Webserver ein
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/status", handleShellyStatus); // Endpunkt für die Shelly-Emulation
  server.begin();

  // Richte MQTT ein
  mqttClient.setServer(config.mqtt_server, config.mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // Richte die serielle Schnittstelle für den Stromzähler ein
  Serial.begin(9600); // Oder eine andere Baudrate, die der Zähler benötigt
}

// --- Loop-Funktion ---
void loop() {
  server.handleClient();
  
  if (shouldSaveConfig) {
    saveConfig();
    shouldSaveConfig = false;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMqtt();
    }
    mqttClient.loop();
    
    readSmartMeter();

    // Sende Daten an MQTT
    static unsigned long lastMqttPublish = 0;
    if (millis() - lastMqttPublish > 10000) { // Alle 10 Sekunden
      char payload[256];
      snprintf(payload, sizeof(payload), "{\"power\":%.2f,\"total_consumption\":%.2f}", current_power, total_consumption);
      mqttClient.publish("unimeter/status", payload);
      lastMqttPublish = millis();
    }
  }
}
