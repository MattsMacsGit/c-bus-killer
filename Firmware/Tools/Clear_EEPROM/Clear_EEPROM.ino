#include <EEPROM.h>

void setup() {
  Serial.begin(9600);
  while (!Serial);
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0); // Zero out all bytes
  }
  Serial.println("EEPROM cleared!");
}

void loop() {}
