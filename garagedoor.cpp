#include "garagedoor.h"

#include <stdio.h>
#include <string.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PageBuilder.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiClient.h>

#include "config.h"

#define CLOSED LOW
#define OPEN HIGH

unsigned long lastUpdate;

const char *firmwareUpdatePath = "/firmware";
const char *firmwareUpdateUsername = "admin";
const char *firmwareUpdatePassword = "admin";

const int pinLED = 2; // internal blue LED

const int pinO1DoorOpen = D1;
const int pinO2DoorClosed = D3;

const int pinS4CmdClose = D5;
const int pinS5CmdLampToggle = D6;
const int pinS2CmdOpen = D7;
const int pinS3CmdPartial = D8;

const int pinTrig = D2;
const int pinEcho = D0;

const char *door_statuses[] = {
        "ERROR",
        "CLOSED",
        "OPEN",
        "OPEN" // This should actually be "Partially open", but Home Assistant does not know the difference
};
const char *car_statuses[] = {
        "NOT_PRESENT",
        "PRESENT"
};

// Set the default statuses for door and car
int doorStatus = DOOR_STATUS_ERROR;
int carStatus = CAR_NOT_PRESENT;

// Set up a value that is updated each loop whenever the door status or car presence changes
bool sendMQTTUpdate = false;

// Set up a HTTP server
ESP8266WebServer httpServer(80);

// Set up a firmware updater page
ESP8266HTTPUpdateServer httpUpdater;

// Set up a wifi client
WiFiClient wifiClient;

// Set up an MQTT client
PubSubClient pubSubClient(wifiClient);

// Set up "tickers" that regularly call the check functions
Ticker doorTicker;
Ticker carPresenceTicker;

// HTML body of the root page
const char rootHtml[] =
        "<ul>"
        "<li>Door status: <strong>{{DOOR_STATUS}}</strong></li>"
        "<li>Car status: <strong>{{CAR_STATUS}}</strong></li>"
        "<li>Wi-Fi network: <strong>{{WIFI_NETWORK}}</strong></li>"
        "<li>Wi-Fi signal strength: <strong>{{WIFI_SIGNAL}}</strong></li>"
        "<li>Software version: <strong>{{VERSION}}</strong></li>"
        "</ul>"
        "<div><a href=\"open\">Open</a> | <a href=\"close\">Close</a></div>";

/**
 * Log to MQTT and serial
 */
void log(char *message) {
    Serial.println(message);
    pubSubClient.publish(MQTT_TOPIC_ACTION, message);
}

/**
 * Check the door status
 */
void checkDoorStatus() {
    int oldDoorStatus = doorStatus;

    int doorOpenStatus = digitalRead(pinO1DoorOpen);
    int doorClosedStatus = digitalRead(pinO2DoorClosed);

    if (doorOpenStatus == OPEN) {
        if (doorClosedStatus == OPEN) {
            doorStatus = DOOR_STATUS_PARTIALLY_OPEN;
        } else { // doorClosedStatus == CLOSED
            doorStatus = DOOR_STATUS_CLOSED;
        }
    } else { // doorOpenStatus == CLOSED
        if (doorClosedStatus == OPEN) {
            doorStatus = DOOR_STATUS_OPEN;
        } else { // doorClosedStatus == CLOSED
            doorStatus = DOOR_STATUS_ERROR;
        }
    }

    if (oldDoorStatus != doorStatus) {
        Serial.printf("Door status: %s\n", door_statuses[doorStatus]);
    }
}

/**
 * Check if a car is present by checking if something (supposedly the car)
 * is less than 1 meter away from the HC-SR04 ultrasonic distance sensor.
 */
void checkCarPresence() {
    digitalWrite(pinTrig, LOW);
    delayMicroseconds(2);
    digitalWrite(pinTrig, HIGH);
    delayMicroseconds(10);
    digitalWrite(pinTrig, LOW);

    int duration = pulseIn(pinEcho, HIGH);
    int distance = (duration * .0343) / 2;

    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print("cm");
    Serial.println();

    int oldCarStatus = carStatus;

    carStatus = (distance < 100) ? CAR_PRESENT : CAR_NOT_PRESENT;

    if (carStatus != oldCarStatus) {
        pubSubClient.publish(MQTT_TOPIC_CAR_STATUS, car_statuses[carStatus]);
        Serial.printf("Car status: %s\n", car_statuses[carStatus]);
    }

    delay(1000);
}

/**
 * Get the door status as a string (for displaying on the HTML page)
 * @param args
 * @return Door status
 */
String getDoorStatus(PageArgument &args) {
    checkDoorStatus();

    switch (doorStatus) {
        case DOOR_STATUS_CLOSED:
            return String("Closed");
        case DOOR_STATUS_OPEN:
            return String("Open");
        case DOOR_STATUS_PARTIALLY_OPEN:
            return String("Partially open");
        case DOOR_STATUS_ERROR:
        default:
            return String("Error");

    }
}

/**
 * Get the car status as a string (for displaying on the HTML page)
 * @param args
 * @return Car presence status
 */
String getCarStatus(PageArgument &args) {
    checkCarPresence();

    switch (carStatus) {
        case CAR_PRESENT:
            return String("Present");
        case CAR_NOT_PRESENT:
        default:
            return String("Not present");
    }
}

/**
 * Get the wifi network name (for displaying on the HTML page)
 * @param args
 * @return Wifi network name
 */
String getWifiNetwork(PageArgument &args) {
    return WiFi.SSID();
}

/**
 * Get the wifi signal strength (for displaying on the HTML page)
 * @param args
 * @return Wifi signal strength
 */
String getWifiSignal(PageArgument &args) {
    return String(printf("%ld dBm", WiFi.RSSI()));
}

/**
 * Get the version (for displaying on the HTML page)
 * @param args
 * @return Version
 */
String getVersion(PageArgument &args) {
    return String(VERSION);
}

// Set up the root HTML page
PageElement html_header("<!DOCTYPE html><html><body>");
PageElement html_footer("</body></html>");
PageElement html_body(rootHtml, {
        {"DOOR_STATUS",  getDoorStatus},
        {"CAR_STATUS",   getCarStatus},
        {"WIFI_NETWORK", getWifiNetwork},
        {"WIFI_SIGNAL",  getWifiSignal},
        {"VERSION",      getVersion}
});
PageBuilder ROOT_PAGE("/", {html_header, html_body, html_footer});

/**
 * React to the "open" command
 */
void openDoor() {
    log("openDoor called");
    digitalWrite(pinS2CmdOpen, HIGH);
    delay(1000);
    digitalWrite(pinS2CmdOpen, LOW);

    log("openDoor done");
}

/**
 * React to the "close" command
 */
void closeDoor() {
    log("closeDoor called");

    digitalWrite(pinS4CmdClose, HIGH);
    delay(1000);
    digitalWrite(pinS4CmdClose, LOW);

    log("closeDoor done");
}

/**
 * MQTT callback
 * @param topic
 * @param payload
 * @param length
 */
void mqttCallback(char *topic, byte *payload, unsigned int length) {
    payload[length] = 0;

    Serial.printf("MQTT call back '%s'", topic, (const char *) payload);

    if (strstr((const char *) payload, "open") != NULL) {
        openDoor();
    }

    if (strstr((const char *) payload, "close") != NULL) {
        closeDoor();
    }

    if (strstr((const char *) payload, "doorStatus") != NULL) {
        checkDoorStatus();
        pubSubClient.publish(MQTT_TOPIC_DOOR_STATUS, door_statuses[doorStatus]);
    }

    if (strstr((const char *) payload, "carPresence") != NULL) {
        checkCarPresence();
        pubSubClient.publish(MQTT_TOPIC_CAR_STATUS, car_statuses[carStatus]);
    }
}

/**
 * (re)connect to wifi
 */
void reconnectToWifi() {
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.printf("WiFi: Not connected to Wifi network %s. Retrying.\n", wifi_ssid);

        // Blink 5 times (fast) to signal Wifi signal lost
        for (int i = 0; i < 5; ++i) {
            digitalWrite(pinLED, HIGH);
            delay(200);
            digitalWrite(pinLED, LOW);
            delay(200);
        }

        WiFi.begin(wifi_ssid, wifi_password);
        delay(5000);
    }

    Serial.printf("WiFi connected. IP address: %s\n", WiFi.localIP().toString().c_str());
    digitalWrite(pinLED, HIGH);
}

/**
 * (re)connect to MQTT
 */
void reconnectToMQTT() {
    while (!pubSubClient.connected()) {
        Serial.printf("Connecting to MQTT server %s\n", mqtt_server);
        if (pubSubClient.connect(MQTT_CLIENT_ID, mqtt_user, mqtt_pass)) {
            Serial.printf("Connected: Subscribing to command topic '%s'\n", MQTT_TOPIC_COMMAND);
            pubSubClient.subscribe(MQTT_TOPIC_COMMAND);
        } else {
            Serial.printf("Connection failed: %s. Retrying...\n", pubSubClient.state());
            delay(5000);
        }
    }
}

void CheckDoorStatus() {
    int oldStatus = doorStatus;
    checkDoorStatus();
    if (oldStatus != doorStatus) {
        Serial.printf("Status has changed to: %s\n", door_statuses[doorStatus]);
        sendMQTTUpdate = true;
        delay(1000);
    }
}

/**
 * Arduino setup function
 */
void runSetup(void) {
    Serial.begin(115200);

    Serial.print("Starting setup\n");

    lastUpdate = millis();

    pinMode(pinO1DoorOpen, INPUT_PULLUP);
    pinMode(pinO2DoorClosed, INPUT_PULLUP);

    pinMode(pinLED, OUTPUT);

    pinMode(pinS4CmdClose, OUTPUT);
    pinMode(pinS5CmdLampToggle, OUTPUT);
    pinMode(pinS2CmdOpen, OUTPUT);
    pinMode(pinS3CmdPartial, OUTPUT);

    digitalWrite(pinS4CmdClose, LOW);
    digitalWrite(pinS5CmdLampToggle, LOW);
    digitalWrite(pinS2CmdOpen, LOW);
    digitalWrite(pinS3CmdPartial, LOW);

    pinMode(pinTrig, OUTPUT);

    pinMode(pinEcho, INPUT);

    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);

    // Connect to Wifi
    reconnectToWifi();

    httpUpdater.setup(&httpServer, firmwareUpdatePath, firmwareUpdateUsername, firmwareUpdatePassword);

    // Setup HTTP Server
    ROOT_PAGE.insert(httpServer);
    httpServer.begin();

    // Print a few status messages on the serial port
    Serial.printf("Version: %s\n", VERSION);
    Serial.printf(
            "You can manually control the garage on http://%s%\n",
            WiFi.localIP().toString().c_str()
    );
    Serial.printf(
            "To update the firmware, open http://%s%s in your browser and login with username '%s' and password '%s'\n",
            WiFi.localIP().toString().c_str(),
            firmwareUpdatePath,
            firmwareUpdateUsername,
            firmwareUpdatePassword
    );

    pubSubClient.setServer(mqtt_server, 1883);
    pubSubClient.setCallback(mqttCallback);

    // Connect to MQTT
    reconnectToMQTT();

    // React to HTTP close command
    httpServer.on("/close", []() {
        closeDoor();
        httpServer.sendHeader("Location", "/");
        httpServer.send(302);
    });

    // React to HTTP open command
    httpServer.on("/open", []() {
        openDoor();
        httpServer.sendHeader("Location", "/");
        httpServer.send(302);
    });

    // Check the garage door status every 1 second
    doorTicker.attach(1, CheckDoorStatus);

    // Check for car presence every 5 seconds
    carPresenceTicker.attach(5, checkCarPresence);
}

/**
 * Arduino loop function
 */
void executeLoop(void) {
    // Try to reconnect to Wifi in case the connection got lost
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        reconnectToWifi();
    }

    // Try to reconnect to MQTT in case the connection got lost
    if (!pubSubClient.connected()) {
        reconnectToMQTT();
    }

    // Check if there was a HTTP request
    httpServer.handleClient();

    // Check if there are new MQTT messages
    pubSubClient.loop();

    // Re-send status every minute to confirm nothing has changed
    if (millis() - lastUpdate > 60 * 1000) {
        Serial.println("Re-sending status");
        lastUpdate = millis();
        sendMQTTUpdate = true;
    }

    // Send the MQTT status updates
    if (sendMQTTUpdate) {
        Serial.printf("Door status: %s\n", door_statuses[doorStatus]);
        pubSubClient.publish(MQTT_TOPIC_DOOR_STATUS, door_statuses[doorStatus]);

        Serial.printf("Car status: %s\n", car_statuses[carStatus]);
        pubSubClient.publish(MQTT_TOPIC_CAR_STATUS, car_statuses[carStatus]);

        // Reset the value for next loop
        sendMQTTUpdate = false;
    }
}