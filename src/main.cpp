#include "Arduino.h"
#include "SoftwareSerial.h"

#define SerialMon Serial
SoftwareSerial SerialAT(2, 3); // RX, TX
// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS

// Define the serial console for debug prints, if needed
// #define TINY_GSM_DEBUG SerialMon

// Define how you're planning to connect to the internet
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

#define TINY_GSM_MODEM_SIM808
#include "TinyGsmClient.h"
#include "PubSubClient.h"

const char GPRS_APN[] = "internet";
// MQTT details
const char* MQTT_BROKER = "mqtt.putrasukarno.my.id";
const char* MQTT_USER = "mosquitto";
const char* MQTT_PASSWORD = "mqttuser";
const char* MQTT_TOPIC_LED = "tracker/led";
const char* MQTT_TOPIC_INIT = "tracker/init";
const char* MQTT_TOPIC_STATUS = "tracker/status";
const char* MQTT_TOPIC_GPS = "tracker/gps";

unsigned long lastReconnect;
const int MODEM_RST = 4;

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif
TinyGsmClient client(modem);
PubSubClient mqtt(client);

void mqttCallback(char* topic, byte* payload, unsigned int len);
bool mqttConnect(void);
void sendGPSLocation(void);

unsigned long previusUpload;
float latitude, longitude, speed;

void setup() {
    SerialMon.begin(115200);

    SerialMon.print(F("POWERING MODEM ON.. "));
    // Turn on modem
    pinMode(MODEM_RST, OUTPUT);
    digitalWrite(MODEM_RST, LOW);
    delay(200);
    digitalWrite(MODEM_RST, HIGH);
    delay(200);
    SerialMon.println(F("OK"));

    SerialAT.begin(4800);
    delay(6000);
    modem.restart();

    delay(1000);
    SerialMon.print(F("MODEM INFO "));
    SerialMon.println(modem.getModemInfo());

    modem.gprsConnect(GPRS_APN);
    SerialMon.print(F("CONNECTING TO INTERNET.. "));
    if (modem.waitForNetwork(10000) == false) {
        SerialMon.println("FAIL");
        while(true);
    }
    if (modem.isGprsConnected() == true) {
        SerialMon.println(F("CONNECTED"));
    }

    // MQTT Broker setup
    mqtt.setServer(MQTT_BROKER, 1883);
    mqtt.setCallback(mqttCallback);

    // Enable GPS
    SerialMon.println(F("GPS ENABLED"));
    modem.enableGPS();
    
    pinMode(13, OUTPUT);
    digitalWrite(13, LOW);
}

void loop(){
    if (mqtt.connected() == false) {
        SerialMon.println(F("MQTT Disconnected"));
        // Reconnect every 10 seconds
        unsigned long t = millis();
        if (t - lastReconnect > 10000L) {
            lastReconnect = t;
            if (mqttConnect() == true) {
                lastReconnect = 0;
            }
        }
        delay(100);
    }

    // Send GPS data every 10 seconds
    if (millis() - previusUpload > 10000L) {
        previusUpload = millis();
        sendGPSLocation();
    }

    mqtt.loop();
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    SerialMon.print("MESSAGE [");
    SerialMon.print(topic);
    SerialMon.print("]: ");
    SerialMon.write(payload, len);
    SerialMon.println();

    payload[len] = '\0';
    char* msg = (char*)payload;
    // Only proceed if incoming message's topic matches
    if (strcmp(topic, MQTT_TOPIC_LED) == 0) {
        if (strcmp(msg, "1") == 0) {
            digitalWrite(13, HIGH);
        } else {
            digitalWrite(13, LOW);
        }
        mqtt.publish(MQTT_TOPIC_STATUS, digitalRead(13) ? "1" : "0");
    }
}

bool mqttConnect(void) {
    SerialMon.print("MQTT CONNECT: ");
    SerialMon.print(MQTT_BROKER);

    boolean status = mqtt.connect("ARDUINO", "mosquitto", "mqttuser");

    if (status == false) {
        SerialMon.println(" FAIL");
        return false;
    }
    SerialMon.println(" CONNECTED");
    mqtt.publish(MQTT_TOPIC_INIT, "START");
    mqtt.subscribe(MQTT_TOPIC_LED);
    return mqtt.connected();
}

void sendGPSLocation(void) {
    bool gps = modem.getGPS(&latitude, &longitude, &speed);
    if (gps == false) {
        Serial.println(F("GPS NOT READY"));
        return;
    }

    SerialMon.print(F("LAT:"));
    SerialMon.print(latitude, 6);
    SerialMon.print(F(", LON:"));
    SerialMon.print(longitude, 6);
    SerialMon.print(F(", SPD:"));
    SerialMon.print(speed);

    if (speed <= 3) {
        SerialMon.println(F(" NO MOVEMENT"));
        return;
    }
    SerialMon.println();

    char location[60];
    dtostrf(latitude, 3, 6, &location[0]);
    strcat(&location[strlen(location)], ",");
    dtostrf(longitude, 3, 6, &location[strlen(location)]);
    strcat(&location[strlen(location)], ",");
    itoa(speed, &location[strlen(location)], 10);

    mqtt.publish(MQTT_TOPIC_GPS, location);
}