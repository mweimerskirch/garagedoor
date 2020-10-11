#ifndef GARAGEDOOR_GARAGEDOOR_H
#define GARAGEDOOR_GARAGEDOOR_H

#define VERSION "0.9.0"

#define DOOR_STATUS_ERROR 0
#define DOOR_STATUS_CLOSED 1
#define DOOR_STATUS_OPEN 2
#define DOOR_STATUS_PARTIALLY_OPEN 3

#define CAR_NOT_PRESENT 0
#define CAR_PRESENT 1

#define MQTT_CLIENT_ID "garagedoor"

#define MQTT_TOPIC_COMMAND "garage/door/command"
#define MQTT_TOPIC_DOOR_STATUS "garage/door/status"
#define MQTT_TOPIC_CAR_STATUS "garage/car/status"
#define MQTT_TOPIC_ACTION "garage/door/action"
#define MQTT_TOPIC_VERSION "garage/door/version"
#define MQTT_TOPIC_WIFI "garage/door/wifi"

void runSetup(void);

void executeLoop(void);

#endif //GARAGEDOOR_GARAGEDOOR_H
