const int switchPins[] = {2, 3, 4, 5};
const int numSwitches = 4;
int switchStates[numSwitches] = {0};
int lastSwitchStates[numSwitches] = {0};
unsigned long lastDebounceTimes[numSwitches] = {0};
unsigned long pressStartTimes[numSwitches] = {0};
unsigned long lastPressTime = 0;
const unsigned long debounceDelay = 50;
const unsigned long longPressTime = 10000; // 10s
const unsigned long doublePressTime = 500; // 0.5s

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < numSwitches; i++) {
    pinMode(switchPins[i], INPUT);
  }
  Serial.println("Switch debug starting...");
}

void loop() {
  for (int i = 0; i < numSwitches; i++) {
    int reading = digitalRead(switchPins[i]);
    if (reading != lastSwitchStates[i]) {
      lastDebounceTimes[i] = millis();
    }
    if ((millis() - lastDebounceTimes[i]) > debounceDelay) {
      if (reading != switchStates[i]) {
        switchStates[i] = reading;
        if (switchStates[i] == HIGH) {
          Serial.print("Switch ");
          Serial.print(i + 1);
          Serial.println(" pressed");
          if (millis() - lastPressTime < doublePressTime) {
            Serial.println("Double press detected");
          }
          lastPressTime = millis();
          pressStartTimes[i] = millis();
        } else if (pressStartTimes[i] > 0) {
          unsigned long pressDuration = millis() - pressStartTimes[i];
          Serial.print("Switch ");
          Serial.print(i + 1);
          Serial.print(" released after ");
          Serial.print(pressDuration);
          Serial.println("ms");
          if (pressDuration >= longPressTime) {
            Serial.println("Long press detected (10s)");
          }
          pressStartTimes[i] = 0;
        }
      }
    }
    lastSwitchStates[i] = reading;
  }
}
