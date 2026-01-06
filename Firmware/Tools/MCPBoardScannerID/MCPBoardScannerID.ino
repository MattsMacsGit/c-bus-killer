#include <Wire.h>

void setup() {
  Wire.begin();
  Wire.setClock(50000); // 50kHz
  Serial.begin(9600);
  while (!Serial); // Wait for Serial Monitor to open
  Serial.println("I2C Scanner Starting...");
}

void loop() {
  byte error, address;
  int deviceCount = 0;

  Serial.println("Scanning...");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      deviceCount++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (deviceCount == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" device(s).");
  }

  Serial.println("Scan complete. Waiting 5 seconds before next scan...");
  delay(5000); // Wait 5 seconds before scanning again
}
