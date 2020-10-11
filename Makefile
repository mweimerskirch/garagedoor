# Arduino CLI reference: https://arduino.github.io/arduino-cli/latest/commands/arduino-cli/

PLATFORM := "esp8266:esp8266"
BOARD := "esp8266:esp8266:d1_mini"
PORT := "/dev/ttyUSB0"

upload:
	arduino-cli compile --fqbn $(BOARD) --verbose --upload --port $(PORT) garagedoor.ino

compile:
	arduino-cli compile --fqbn $(BOARD) --verbose garagedoor.ino

setup:
	arduino-cli lib install PubSubClient
	arduino-cli lib install PageBuilder
	arduino-cli core install $(PLATFORM) --additional-urls http://arduino.esp8266.com/stable/package_esp8266com_index.json

clean:
	rm -rf build
