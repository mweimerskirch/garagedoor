Garage door for ESP8266
=

ESP8266 based project to control my garage door and check if a car is present.

Works only with the adapter board UAP-1 on garage doors by "Hörmann" (https://www.tor7.de/hoermann-universaladapterplatine-uap-1).

WORK IN PROGRESS.

Setup
-
Install Arduino-CLI:
```
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Install dependencies
```
make setup
```

Building the software
-
```
cp config.dist.h config.h
# Edit config.h
make upload
```

Building the hardware
-
Work in progess