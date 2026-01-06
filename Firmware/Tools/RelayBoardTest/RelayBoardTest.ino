#include <Wire.h>
#include <RBDdimmer.h>

#define RELAY_PIN 26
#define ZC_PIN 2
#define PSM_PIN 9
#define PLATE_ADDRESS 0x27
#define SWITCH_NUM 5

dimmerLamp dimmer(PSM_PIN); // Only PSM pin
bool relayState = false;
bool lastSwitchState = true;
int dimLevel = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Wire.begin();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(ZC_PIN, INPUT); // ZC pin as input for interrupt
  digitalWrite(RELAY_PIN, LOW);

  // Configure I2C switch plate
  Wire.beginTransmission(PLATE_ADDRESS);
  Wire.write(0x0C); // Config port
  Wire.write(0x3F); // Pull-ups
  Wire.endTransmission();

  // Initialize dimmer
  dimmer.begin(NORMAL_MODE, OFF);
  dimmer.setPower(0); // Start off
  Serial.println("Dimmer Test Ready. Enter 0-100.");

  // Ensure ZC pin is ready (library typically uses pin 2 for INT0)
  // No need to manually attach interrupt; library handles it
}

void loop() {
  // Read switch
  Wire.beginTransmission(PLATE_ADDRESS);
  Wire.write(0x12); // Input port
  Wire.endTransmission();
  Wire.requestFrom(PLATE_ADDRESS, 1);
  bool switchState = true;
  if (Wire.available()) {
    byte state = Wire.read();
    switchState = (state >> SWITCH_NUM) & 0x01; // 0 = pressed
  }

  // Toggle on press
  if (switchState == false && lastSwitchState == true) {
    relayState = !relayState;
    dimLevel = relayState ? 50 : 0; // Default to 50% when on
    dimmer.setPower(dimLevel);
    digitalWrite(RELAY_PIN, relayState);
    Serial.print("Switch1_5: ");
    Serial.println(relayState ? "ON, 50%" : "OFF");
  }
  lastSwitchState = switchState;

  // Serial input for dim level
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    int newDim = input.toInt();
    if (newDim >= 0 && newDim <= 100) {
      dimLevel = newDim;
      relayState = (dimLevel > 0);
      dimmer.setPower(dimLevel);
      digitalWrite(RELAY_PIN, relayState);
      Serial.print("New dimLevel: ");
      Serial.println(dimLevel);
    } else {
      Serial.println("Enter a number between 0 and 100");
    }
  }
}
