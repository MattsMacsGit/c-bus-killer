void setup() {
  Serial.begin(9600);
  while (!Serial);
  Wire.begin();

  for (int p = 0; p < MAX_PLATES; p++) {
    if (plateAddresses[p] != 0) {
      totalSwitches += numSwitchesPerPlate[p];
    }
  }

  // Initialize arrays (assuming same as before)
  ledStates = new bool[totalSwitches];
  lastSwitchStates = new bool[totalSwitches];
  lastPressTime = new unsigned long[totalSwitches];
  lastReleaseTime = new unsigned long[totalSwitches];
  pressCount = new int[totalSwitches];
  holdTriggered = new bool[totalSwitches];
  switchNames = new String[totalSwitches];
  numChannelsPerSwitch = new int[totalSwitches];
  numSwitchesPerChannel = new int[TOTAL_CHANNELS];
  switchToChannelMap = new int*[totalSwitches];
  channelToSwitchMap = new int*[TOTAL_CHANNELS];
  for (int i = 0; i < totalSwitches; i++) {
    switchToChannelMap[i] = new int[10]; // MAX_CHANNELS_PER_SWITCH
  }
  for (int i = 0; i < TOTAL_CHANNELS; i++) {
    channelToSwitchMap[i] = new int[10]; // MAX_SWITCHES_PER_CHANNEL
  }

  unsigned long initTime = millis(); // Capture time at startup
  for (int i = 0; i < totalSwitches; i++) {
    ledStates[i] = false;
    lastSwitchStates[i] = true; // Assume unpressed initially
    lastPressTime[i] = initTime; // Set to current time to avoid false holds
    lastReleaseTime[i] = 0;
    pressCount[i] = 0;
    holdTriggered[i] = false;
    numChannelsPerSwitch[i] = 0;
  }
  for (int i = 0; i < TOTAL_CHANNELS; i++) {
    channelStates[i] = false;
    numSwitchesPerChannel[i] = 0;
    channelNames[i] = "Channel1_" + String(i + 1);
  }

  // Rest of setup (I2C config, pin modes, etc.) remains unchanged
  int globalSwitchIdx = 0;
  for (int p = 0; p < MAX_PLATES; p++) {
    if (plateAddresses[p] == 0) continue;
    Wire.beginTransmission(plateAddresses[p]);
    Wire.write(0x12);
    Wire.endTransmission();
    Wire.requestFrom(plateAddresses[p], 1);
    byte state = Wire.read();
    for (int s = 0; s < numSwitchesPerPlate[p]; s++) {
      lastSwitchStates[globalSwitchIdx] = (state >> s) & 0x01;
      globalSwitchIdx++;
    }
  }

  Serial.println("System Ready..");
}
