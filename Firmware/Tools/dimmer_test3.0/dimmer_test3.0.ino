#include <Wire.h>
#include <TimerOne.h>

#define RELAY_PIN 26
#define ZC_PIN 2
#define PSM_PIN 9
#define PLATE_ADDRESS 0x27
#define SWITCH_NUM 4

volatile bool zcFlag = false;
volatile int stepCounter = 0;
bool channelState = false;
bool lastSwitchState = true;
bool isHolding = false;
unsigned long lastDebounceTime = 0;
unsigned long lastPressTime = 0;
unsigned long lastDimTime = 0;
int dimLevel = 128; // 0-128, 0 = full on, 128 = off
float dimDuration = 5000.0; // Default: 5s to dim 100% to 0%
const unsigned long DEBOUNCE_TIME = 50;
const unsigned long HOLD_THRESHOLD = 230;
const int freqStep = 75; // 50Hz, 75µs per step

void zcInterrupt() {
  zcFlag = true;
  stepCounter = 0;
  digitalWrite(PSM_PIN, LOW);
}

void dim_check() {
  if (zcFlag && channelState && dimLevel < 128) {
    if (stepCounter >= dimLevel) {
      digitalWrite(PSM_PIN, HIGH); // Fixed pulse until next zero-cross
      zcFlag = false;
    } else {
      stepCounter++;
    }
  }
}

void setLED(bool state) {
  Wire.beginTransmission(PLATE_ADDRESS);
  Wire.write(0x13);
  Wire.write(state ? (1 << 4) : 0);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Wire.begin(); // SDA=20, SCL=21 on Mega
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PSM_PIN, OUTPUT);
  pinMode(ZC_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PSM_PIN, LOW);
  
  Wire.beginTransmission(PLATE_ADDRESS);
  Wire.write(0xFF); // Initialize PCF8574
  Wire.endTransmission();
  
  attachInterrupt(digitalPinToInterrupt(ZC_PIN), zcInterrupt, RISING); // Pin 2 on Mega
  Timer1.initialize(freqStep);
  Timer1.attachInterrupt(dim_check);
  setLED(false);
  Serial.println("Dimmer: Toggle <230ms, hold >230ms to dim. D0-D100, T1-T10 (seconds).");
}

void loop() {
  unsigned long now = millis();

  // Read I2C switch
  byte state = 0xFF;
  Wire.beginTransmission(PLATE_ADDRESS);
  Wire.write(0x12);
  if (Wire.endTransmission() == 0) {
    Wire.requestFrom(PLATE_ADDRESS, 1);
    if (Wire.available()) {
      state = Wire.read();
    }
  }
  bool currentState = (state >> SWITCH_NUM) & 0x01;

  // Debounce and handle switch
  if (currentState != lastSwitchState && now - lastDebounceTime >= DEBOUNCE_TIME) {
    lastDebounceTime = now;
    if (currentState == false) { // Press
      lastPressTime = now;
      isHolding = true;
      lastDimTime = now;
    } else { // Release
      if (now - lastPressTime < HOLD_THRESHOLD) {
        channelState = !channelState;
        dimLevel = channelState ? 0 : 128; // 0 = full on, 128 = off
        digitalWrite(RELAY_PIN, channelState ? HIGH : LOW);
        setLED(channelState);
        Serial.println(channelState ? "ON" : "OFF");
      } else if (dimLevel < 128) {
        Serial.print("Dimmed to: ");
        Serial.println(map(128 - dimLevel, 0, 128, 0, 100)); // Show 0-100%
      }
      isHolding = false;
    }
    lastSwitchState = currentState;
  }

  // Time-based dimming on hold
  if (channelState && isHolding && now - lastPressTime >= HOLD_THRESHOLD) {
    unsigned long stepInterval = dimDuration / 128; // ms per step (e.g., 4000 / 128 = 31.25ms)
    if (now - lastDimTime >= stepInterval) {
      dimLevel++; // Dim down
      dimLevel = constrain(dimLevel, 0, 128);
      lastDimTime = now;
      Serial.print("Dimmed to: ");
      Serial.println(map(128 - dimLevel, 0, 128, 0, 100));
    }
  }

  // Serial control
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("D") || input.startsWith("d")) {
      if (input.substring(1).toInt() > 0 || input.substring(1) == "0") {
        int newDim = input.substring(1).toInt();
        if (newDim >= 0 && newDim <= 100) {
          dimLevel = map(100 - newDim, 0, 100, 0, 128); // 0% = 128, 100% = 0
          channelState = (newDim > 0);
          digitalWrite(RELAY_PIN, channelState ? HIGH : LOW);
          setLED(channelState);
          Serial.print("Set dimLevel: ");
          Serial.print(newDim);
          Serial.println("%");
        } else {
          Serial.println("Enter D0-D100");
        }
      } else {
        Serial.println("Invalid number");
      }
    } else if (input.startsWith("T") || input.startsWith("t")) {
      if (input.substring(1).toInt() > 0) {
        int newDuration = input.substring(1).toInt();
        if (newDuration >= 1 && newDuration <= 10) {
          dimDuration = newDuration * 1000.0; // Convert to ms
          Serial.print("Set dimDuration: ");
          Serial.print(newDuration);
          Serial.println("s");
        } else {
          Serial.println("Enter T1-T10");
        }
      } else {
        Serial.println("Invalid number");
      }
    } else {
      Serial.println("Enter D0-D100 or T1-T10");
    }
  }
}
