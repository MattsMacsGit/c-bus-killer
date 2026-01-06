// Arduino Mega sketch for 20-channel relay system with I2C switch plates, timers, and dimming.
// Optimized for low SRAM: PROGMEM strings, smaller data types, minimal debug log.
// Integrates non-blocking Timer1 dimmer for channel 5 (pin 26 relay, pin 9 triac, pin 2 zero-cross).

#include <Wire.h>
#include <EEPROM.h>
#include <TimerOne.h>

// Constants
#define EEPROM_MODE F("TESTING")
#define EEPROM_START_ADDRESS 2000
#define MAX_SWITCHES 20
#define TOTAL_CHANNELS 20
#define MAX_PLATES 8
#define REG_SWITCH_STATE 0x12 // MCP23017 GPIOA
#define REG_LED_STATE 0x13 // MCP23017 OLATB
const uint8_t relayControlPins[TOTAL_CHANNELS] = {22, 23, 24, 25, 26, 27, 28, 29, 10, 11, 12, 13, 30, 31, 32, 33, 34, 35, 36, 37};
const uint8_t switchPlateI2CAddresses[MAX_PLATES] = {0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0};
const uint8_t numSwitchesPerPlate[MAX_PLATES] = {6, 1, 2, 2, 4, 4, 1, 0};
const uint8_t psmPins[TOTAL_CHANNELS] = {0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // Channel 5 dimmable
#define zcPin 2 // Zero-cross for channel 5
#define COMMON_ANODE false
#define CONFIRM_TIMEOUT 10000UL
#define DEBOUNCE_DELAY 50UL
#define DROPOUT_REPORT_INTERVAL 10000UL
#define MAX_CHANNELS_PER_SWITCH 5
#define MAX_SWITCHES_PER_CHANNEL 5
#define TOGGLE_HOLD_THRESHOLD 950UL // Dimmer’s 950ms
#define GLOBAL_TIMER_THRESHOLD 3000UL // 3s for global timer
#define DIM_MIN 0 // 0% (full on)
#define DIM_MAX 128 // 100% (off)
#define DIM_DURATION 5000.0 // 5s to dim 0-100%
#define freqStep 75 // 50Hz, 75µs per step

// Switch State FSM
enum SwitchStateEnum : uint8_t { IDLE, PRESSED, HELD_230MS, HELD_3S };
struct SwitchState {
  SwitchStateEnum state;
  uint32_t pressTime;
  uint32_t releaseTime;
};

// Global Variables
uint8_t totalSwitches = 0;
bool channelStates[TOTAL_CHANNELS] = {false};
uint8_t dimLevel[TOTAL_CHANNELS] = {0}; // 0-128
uint32_t lastDimSerial[TOTAL_CHANNELS] = {0};
uint8_t switchToChannelMap[MAX_SWITCHES][MAX_CHANNELS_PER_SWITCH] = {{0}};
uint8_t numChannelsPerSwitch[MAX_SWITCHES] = {0};
uint8_t channelToSwitchMap[TOTAL_CHANNELS][MAX_SWITCHES_PER_CHANNEL] = {{0}};
uint8_t numSwitchesPerChannel[TOTAL_CHANNELS] = {0};
uint16_t timerDuration = 0;
uint16_t channelTimerDurations[TOTAL_CHANNELS] = {0};
uint32_t timerEndTime[TOTAL_CHANNELS] = {0};
bool flashState = false;
uint8_t timerFlashState = 0;
uint32_t uptimeSeconds = 0;
uint32_t uptimeDays = 0;
char serialBuffer[96] = {0}; // Reduced from 128
uint8_t serialBufferIndex = 0;
char channelNames[TOTAL_CHANNELS][16] = {{0}};
const char defaultChannelNames[TOTAL_CHANNELS][16] PROGMEM = {
  "CH1_1", "CH1_2", "CH1_3", "CH1_4", "CH1_5", "CH1_6", "CH1_7", "CH1_8",
  "CH2_1", "CH2_2", "CH2_3", "CH2_4",
  "CH3_1", "CH3_2", "CH3_3", "CH3_4", "CH3_5", "CH3_6", "CH3_7", "CH3_8"
};
bool dirtyMappings = false;
bool dirtyLEDs = false;
SwitchState switchStates[MAX_SWITCHES];
bool programMode = false;
bool awaitingConfirmation = false;
uint32_t confirmStartTime = 0;
char pendingCommand[16] = {0};
uint8_t i2cFailureCount[MAX_PLATES] = {0};
char printBuffer[48] = {0}; // Reduced from 64
char debugLog[128] = {0}; // Minimal debug log
uint8_t debugLogIndex = 0;
volatile bool zcFlag = false;
volatile uint8_t stepCounter = 0;
uint32_t lastDimTime = 0;

// SRAM Monitoring
int freeSRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// Debug Logging
void logDebug(const char* message) {
  int len = strlen(message);
  if (len >= sizeof(debugLog)) return;
  if (debugLogIndex + len + 1 >= sizeof(debugLog)) debugLogIndex = 0;
  strncpy(debugLog + debugLogIndex, message, sizeof(debugLog) - debugLogIndex - 1);
  debugLogIndex += len;
  debugLog[debugLogIndex++] = '\n';
  debugLog[debugLogIndex] = '\0';
}

// Dimmer Functions
void zcInterrupt() {
  zcFlag = true;
  stepCounter = 0;
  digitalWrite(psmPins[4], LOW);
}

void dim_check() {
  if (zcFlag && channelStates[4] && dimLevel[4] < DIM_MAX) {
    if (stepCounter >= dimLevel[4]) {
      digitalWrite(psmPins[4], HIGH);
      zcFlag = false;
    } else {
      stepCounter++;
    }
  }
}

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(5000, true);
  for (uint8_t plateIdx = 0; plateIdx < MAX_PLATES; plateIdx++) {
    if (!switchPlateI2CAddresses[plateIdx] || !numSwitchesPerPlate[plateIdx]) continue;
    uint8_t address = switchPlateI2CAddresses[plateIdx];
    if (!retryI2CWrite(address, 0x00, 0xFF)) Serial.println(F("Error: Failed IODIRA"));
    if (!retryI2CWrite(address, 0x01, 0x00)) Serial.println(F("Error: Failed IODIRB"));
    if (!retryI2CWrite(address, 0x0C, 0xFF)) Serial.println(F("Error: Failed GPPUA"));
  }

  for (uint8_t i = 0; i < TOTAL_CHANNELS; i++) {
    pinMode(relayControlPins[i], OUTPUT);
    digitalWrite(relayControlPins[i], LOW);
    if (psmPins[i]) {
      pinMode(psmPins[i], OUTPUT);
      digitalWrite(psmPins[i], LOW);
    }
  }
  pinMode(zcPin, INPUT_PULLUP);

  Timer1.initialize(freqStep);
  Timer1.attachInterrupt(dim_check);
  attachInterrupt(digitalPinToInterrupt(zcPin), zcInterrupt, RISING);

  loadMappingsFromEEPROM();

  for (uint8_t plateIdx = 0; plateIdx < MAX_PLATES; plateIdx++) {
    totalSwitches += numSwitchesPerPlate[plateIdx];
  }
  if (totalSwitches > MAX_SWITCHES) totalSwitches = MAX_SWITCHES;

  Serial.println(F("System Ready!"));
  snprintf(printBuffer, sizeof(printBuffer), "SRAM: %d bytes", freeSRAM());
  Serial.println(printBuffer);
  Serial.flush();
}

// I2C Functions
bool retryI2CRead(uint8_t address, uint8_t registerAddr, uint8_t& data) {
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(address);
    Wire.write(registerAddr);
    if (Wire.endTransmission() == 0 && Wire.requestFrom(address, (uint8_t)1) && Wire.available()) {
      data = Wire.read();
      return true;
    }
    delay(10);
  }
  return false;
}

bool retryI2CWrite(uint8_t address, uint8_t registerAddr, uint8_t data) {
  uint8_t plateIdx = 255;
  for (uint8_t i = 0; i < MAX_PLATES; i++) {
    if (switchPlateI2CAddresses[i] == address) {
      plateIdx = i;
      break;
    }
  }
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(address);
    Wire.write(registerAddr);
    Wire.write(data);
    if (Wire.endTransmission() == 0) {
      if (plateIdx != 255) i2cFailureCount[plateIdx] = 0; // Reset on success
      return true;
    }
    delay(10);
  }
  if (plateIdx != 255) {
    i2cFailureCount[plateIdx]++;
    if (i2cFailureCount[plateIdx] % 10 == 0) { // Log every 10th failure
      snprintf(printBuffer, sizeof(printBuffer), "I2C fail plate %d: %d", plateIdx + 1, i2cFailureCount[plateIdx]);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
  }
  return false;
}

void resetI2CPlate(uint8_t plateIdx) {
  if (!switchPlateI2CAddresses[plateIdx] || !numSwitchesPerPlate[plateIdx]) return;
  uint8_t address = switchPlateI2CAddresses[plateIdx];
  retryI2CWrite(address, 0x00, 0xFF);
  retryI2CWrite(address, 0x01, 0x00);
  retryI2CWrite(address, 0x0C, 0xFF);
  i2cFailureCount[plateIdx] = 0;
  snprintf(printBuffer, sizeof(printBuffer), "Reset plate %d", plateIdx + 1);
  Serial.println(printBuffer);
  logDebug(printBuffer);
}

// Switch and LED Handling
int8_t getGlobalSwitchIndex(uint8_t plateIdx, uint8_t switchIdx) {
  if (plateIdx >= MAX_PLATES || switchIdx >= numSwitchesPerPlate[plateIdx]) return -1;
  uint8_t globalIdx = 0;
  for (uint8_t i = 0; i < plateIdx; i++) globalIdx += numSwitchesPerPlate[i];
  globalIdx += switchIdx;
  return (globalIdx < MAX_SWITCHES) ? globalIdx : -1;
}

void updateLEDsForPlate(uint8_t plateIdx) {
  if (!switchPlateI2CAddresses[plateIdx] || !numSwitchesPerPlate[plateIdx]) return;
  uint8_t address = switchPlateI2CAddresses[plateIdx];
  uint8_t ledData = 0;

  for (uint8_t s = 0; s < numSwitchesPerPlate[plateIdx]; s++) {
    int8_t globalSwitchIdx = getGlobalSwitchIndex(plateIdx, s);
    if (globalSwitchIdx == -1) continue;

    bool timerActive = false;
    for (uint8_t i = 0; i < numChannelsPerSwitch[globalSwitchIdx]; i++) {
      uint8_t channelIdx = switchToChannelMap[globalSwitchIdx][i];
      if (timerEndTime[channelIdx] > millis()) {
        timerActive = true;
        break;
      }
    }

    if (timerActive && !programMode) {
      if (timerFlashState) ledData |= (1 << s);
    } else if (programMode) {
      if (flashState) ledData |= (1 << s);
    } else {
      for (uint8_t i = 0; i < numChannelsPerSwitch[globalSwitchIdx]; i++) {
        if (channelStates[switchToChannelMap[globalSwitchIdx][i]]) {
          ledData |= (1 << s);
          break;
        }
      }
    }
  }

  retryI2CWrite(address, REG_LED_STATE, ledData);
}

void updateAllLEDs() {
  for (uint8_t plateIdx = 0; plateIdx < MAX_PLATES; plateIdx++) {
    if (switchPlateI2CAddresses[plateIdx] && numSwitchesPerPlate[plateIdx]) {
      updateLEDsForPlate(plateIdx);
    }
  }
  dirtyLEDs = false;
}

void processSwitches() {
  for (uint8_t plateIdx = 0; plateIdx < MAX_PLATES; plateIdx++) {
    if (!switchPlateI2CAddresses[plateIdx] || !numSwitchesPerPlate[plateIdx]) continue;
    uint8_t address = switchPlateI2CAddresses[plateIdx];
    uint8_t switchData = 0;
    if (!retryI2CRead(address, REG_SWITCH_STATE, switchData)) {
      snprintf(printBuffer, sizeof(printBuffer), "Read fail plate %d", plateIdx + 1);
      Serial.println(printBuffer);
      logDebug(printBuffer);
      continue;
    }

    for (uint8_t s = 0; s < numSwitchesPerPlate[plateIdx]; s++) {
      int8_t globalSwitchIdx = getGlobalSwitchIndex(plateIdx, s);
      if (globalSwitchIdx == -1) continue;
      bool currentState = !(switchData & (1 << s));
      SwitchState& switchState = switchStates[globalSwitchIdx];
      uint32_t currentTime = millis();

      switch (switchState.state) {
        case IDLE:
          if (currentState && currentTime - switchState.releaseTime >= DEBOUNCE_DELAY) {
            switchState.state = PRESSED;
            switchState.pressTime = currentTime;
          }
          break;

        case PRESSED:
          if (!currentState && currentTime - switchState.pressTime >= DEBOUNCE_DELAY &&
              currentTime - switchState.pressTime < TOGGLE_HOLD_THRESHOLD) {
            toggleSwitch(globalSwitchIdx);
            switchState.state = IDLE;
            switchState.releaseTime = currentTime;
          } else if (currentState && currentTime - switchState.pressTime >= TOGGLE_HOLD_THRESHOLD) {
            switchState.state = HELD_230MS;
          }
          break;

        case HELD_230MS:
          if (!currentState && currentTime - switchState.pressTime >= DEBOUNCE_DELAY) {
            switchState.state = IDLE;
            switchState.releaseTime = currentTime;
            if (dimLevel[4] < DIM_MAX) {
              snprintf(printBuffer, sizeof(printBuffer), "CH1_5 ON %d", map(DIM_MAX - dimLevel[4], 0, DIM_MAX, 0, 100));
              Serial.println(printBuffer);
            }
          } else {
            bool dimmable = false;
            for (uint8_t i = 0; i < numChannelsPerSwitch[globalSwitchIdx]; i++) {
              uint8_t channelIdx = switchToChannelMap[globalSwitchIdx][i];
              if (psmPins[channelIdx] && channelStates[channelIdx]) {
                dimmable = true;
                break;
              }
            }
            if (dimmable) {
              dimSwitch(globalSwitchIdx);
            }
            if (currentTime - switchState.pressTime >= GLOBAL_TIMER_THRESHOLD) {
              bool allOff = true;
              for (uint8_t i = 0; i < numChannelsPerSwitch[globalSwitchIdx]; i++) {
                if (channelStates[switchToChannelMap[globalSwitchIdx][i]]) {
                  allOff = false;
                  break;
                }
              }
              if (allOff && timerDuration > 0) {
                switchState.state = HELD_3S;
                setGlobalTimer(globalSwitchIdx);
              }
            }
          }
          break;

        case HELD_3S:
          if (!currentState && currentTime - switchState.pressTime >= DEBOUNCE_DELAY) {
            switchState.state = IDLE;
            switchState.releaseTime = currentTime;
          }
          break;
      }
    }
  }
}

// Dimming and SSR Control
void dimSwitch(uint8_t switchIdx) {
  if (switchIdx >= MAX_SWITCHES) return;
  uint32_t currentTime = millis();
  if (currentTime - lastDimTime < (uint32_t)(DIM_DURATION / DIM_MAX)) return;

  bool dimmable = false;
  for (uint8_t i = 0; i < numChannelsPerSwitch[switchIdx]; i++) {
    uint8_t channelIdx = switchToChannelMap[switchIdx][i];
    if (psmPins[channelIdx] && channelStates[channelIdx]) {
      dimmable = true;
      break;
    }
  }
  if (!dimmable) return;

  bool updated = false;
  for (uint8_t i = 0; i < numChannelsPerSwitch[switchIdx]; i++) {
    uint8_t channelIdx = switchToChannelMap[switchIdx][i];
    if (psmPins[channelIdx] && channelStates[channelIdx]) {
      dimLevel[channelIdx]++;
      dimLevel[channelIdx] = constrain(dimLevel[channelIdx], DIM_MIN, DIM_MAX);
      updated = true;
      if (!programMode && currentTime - lastDimSerial[channelIdx] >= 500) {
        char name[16];
        strncpy(name, channelNames[channelIdx], 15);
        name[15] = '\0';
        if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
        snprintf(printBuffer, sizeof(printBuffer), "%s ON %d", name, map(DIM_MAX - dimLevel[channelIdx], 0, DIM_MAX, 0, 100));
        Serial.println(printBuffer);
        lastDimSerial[channelIdx] = currentTime;
      }
    }
  }

  if (updated) {
    lastDimTime = currentTime;
    dirtyLEDs = true;
  }
}

void updateSSRs() {
  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    digitalWrite(relayControlPins[c], channelStates[c] ? HIGH : LOW);
  }
}

void toggleSwitch(uint8_t switchIdx) {
  if (switchIdx >= MAX_SWITCHES) return;
  bool newState = numChannelsPerSwitch[switchIdx] > 0 ? !channelStates[switchToChannelMap[switchIdx][0]] : true;

  for (uint8_t i = 0; i < numChannelsPerSwitch[switchIdx]; i++) {
    uint8_t channelIdx = switchToChannelMap[switchIdx][i];
    channelStates[channelIdx] = newState;
    if (newState) {
      if (psmPins[channelIdx]) dimLevel[channelIdx] = DIM_MIN;
      if (channelTimerDurations[channelIdx]) {
        timerEndTime[channelIdx] = millis() + (uint32_t)channelTimerDurations[channelIdx] * 60000UL;
      }
    } else {
      dimLevel[channelIdx] = DIM_MAX;
      timerEndTime[channelIdx] = 0;
    }
    if (!programMode) {
      char name[16];
      strncpy(name, channelNames[channelIdx], 15);
      name[15] = '\0';
      if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
      snprintf(printBuffer, sizeof(printBuffer), "%s %s", name, newState ? "ON" : "OFF");
      if (newState && psmPins[channelIdx]) {
        char dimStr[8];
        snprintf(dimStr, sizeof(dimStr), " %d", map(DIM_MAX - dimLevel[channelIdx], 0, DIM_MAX, 0, 100));
        strncat(printBuffer, dimStr, sizeof(printBuffer) - strlen(printBuffer) - 1);
      }
      Serial.println(printBuffer);
    }
  }

  updateSSRs();
  dirtyLEDs = true;

  if (programMode) {
    uint8_t plateIdx = 0, switchNum = switchIdx;
    for (uint8_t p = 0; p < MAX_PLATES; p++) {
      if (switchNum < numSwitchesPerPlate[p]) {
        plateIdx = p;
        break;
      }
      switchNum -= numSwitchesPerPlate[p];
    }
    snprintf(printBuffer, sizeof(printBuffer), "SW%d_%d Toggled %s", plateIdx + 1, switchNum + 1, newState ? "ON" : "OFF");
    Serial.println(printBuffer);
    logDebug(printBuffer);
  }
}

// Timer Management
void updateTimers() {
  uint32_t currentTime = millis();
  bool updated = false;

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    if (timerEndTime[c] && timerEndTime[c] <= currentTime) {
      char name[16];
      strncpy(name, channelNames[c], 15);
      name[15] = '\0';
      if (!name[0]) strncpy_P(name, defaultChannelNames[c], 15);
      toggleChannelByName(name, false);
      updated = true;
      snprintf(printBuffer, sizeof(printBuffer), "%s timer expired", name);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
  }

  if (updated) {
    dirtyLEDs = true;
    updateSSRs();
  }
}

void validateTimers() {
  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    if (timerEndTime[c] && !channelStates[c]) {
      timerEndTime[c] = 0;
      snprintf(printBuffer, sizeof(printBuffer), "Cleared timer CH%d", c + 1);
      Serial.println(printBuffer);
      logDebug(printBuffer);
      dirtyLEDs = true;
    }
  }
}

void setGlobalTimer(uint8_t switchIdx) {
  if (switchIdx >= MAX_SWITCHES || !timerDuration) return;
  bool allOff = true;
  for (uint8_t i = 0; i < numChannelsPerSwitch[switchIdx]; i++) {
    if (channelStates[switchToChannelMap[switchIdx][i]]) {
      allOff = false;
      break;
    }
  }
  if (allOff) {
    toggleSwitch(switchIdx);
    for (uint8_t i = 0; i < numChannelsPerSwitch[switchIdx]; i++) {
      uint8_t channelIdx = switchToChannelMap[switchIdx][i];
      timerEndTime[channelIdx] = millis() + (uint32_t)timerDuration * 60000UL;
    }
    if (programMode) {
      snprintf(printBuffer, sizeof(printBuffer), "Global Timer %dm SW%d", timerDuration, switchIdx + 1);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    dirtyLEDs = true;
  }
}

void setChannelTimer(uint8_t channelIdx, uint16_t duration) {
  if (channelIdx >= TOTAL_CHANNELS || duration > 65535) return;
  channelTimerDurations[channelIdx] = duration;
  if (duration && channelStates[channelIdx]) {
    timerEndTime[channelIdx] = millis() + (uint32_t)duration * 60000UL;
  } else if (!duration) {
    timerEndTime[channelIdx] = 0;
  }
  char name[16];
  strncpy(name, channelNames[channelIdx], 15);
  name[15] = '\0';
  if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
  snprintf(printBuffer, sizeof(printBuffer), "Timer %dm %s", duration, name);
  Serial.println(printBuffer);
  logDebug(printBuffer);
  dirtyMappings = true;
}

void cancelChannelTimer(uint8_t channelIdx) {
  if (channelIdx >= TOTAL_CHANNELS || !timerEndTime[channelIdx]) return;
  timerEndTime[channelIdx] = 0;
  if (programMode) {
    char name[16];
    strncpy(name, channelNames[channelIdx], 15);
    name[15] = '\0';
    if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
    snprintf(printBuffer, sizeof(printBuffer), "%s timer canceled", name);
    Serial.println(printBuffer);
    logDebug(printBuffer);
  }
}

// Serial Command Processing
void processSerialInput() {
  while (Serial.available() && serialBufferIndex < 95) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuffer[serialBufferIndex] = '\0';
      char command[96];
      strncpy(command, serialBuffer, 95);
      command[95] = '\0';
      for (char* p = command; *p; p++) *p = tolower(*p);
      serialBufferIndex = 0;
      memset(serialBuffer, 0, sizeof(serialBuffer));

      if (awaitingConfirmation) {
        if (millis() - confirmStartTime > CONFIRM_TIMEOUT) {
          Serial.println(F("Error: No response. Canceled."));
          awaitingConfirmation = false;
          pendingCommand[0] = '\0';
        } else if (strcmp(command, "y") == 0) {
          if (strcmp(pendingCommand, "ClearMappings") == 0) clearMappings();
          else if (strcmp(pendingCommand, "SystemReset") == 0) systemReset();
          else if (strcmp(pendingCommand, "ClearAllTimers") == 0) clearAllTimers();
          awaitingConfirmation = false;
          pendingCommand[0] = '\0';
        } else if (strcmp(command, "n") == 0) {
          Serial.println(F("Command canceled."));
          awaitingConfirmation = false;
          pendingCommand[0] = '\0';
        } else {
          Serial.println(F("Error: Use 'Y' or 'N'."));
        }
        return;
      }

      if (programMode) {
        if (strcmp(command, "manual") == 0) displayManual();
        else if (strcmp(command, "exitprogrammode") == 0) {
          if (dirtyMappings) {
            saveMappingsToEEPROM();
            Serial.println(F("Mappings saved."));
          }
          programMode = false;
          Serial.println(F("Program Mode Exited!"));
        } else if (strcmp(command, "clearalltimers") == 0) {
          Serial.println(F("Confirm clear timers (Y/N)?"));
          awaitingConfirmation = true;
          confirmStartTime = millis();
          strcpy(pendingCommand, "ClearAllTimers");
        } else if (strncmp(command, "rename ", 7) == 0) {
          processRenameCommand(command);
        } else if (strncmp(command, "globaltimer ", 11) == 0) {
          processGlobalTimerCommand(command);
        } else if (strcmp(command, "status") == 0) {
          displayStatus();
        } else if (strstr(command, " status")) {
          char channelName[16];
          sscanf(command, "%s status", channelName);
          int8_t channelIdx = findChannelIndex(channelName);
          if (channelIdx != -1) displayChannelStatus(channelIdx);
          else Serial.println(F("Error: Channel not found."));
        } else if (strcmp(command, "clearmappings") == 0) {
          Serial.println(F("Confirm clear mappings (Y/N)?"));
          awaitingConfirmation = true;
          confirmStartTime = millis();
          strcpy(pendingCommand, "ClearMappings");
        } else if (strcmp(command, "systemreset") == 0) {
          Serial.println(F("Confirm reset (Y/N)?"));
          awaitingConfirmation = true;
          confirmStartTime = millis();
          strcpy(pendingCommand, "SystemReset");
        } else if (strstr(command, " timer ")) {
          processChannelTimerCommand(command);
        } else if (strstr(command, " on ") || strstr(command, " on\0")) {
          processToggleCommand(command, true);
        } else if (strstr(command, " off")) {
          processToggleCommand(command, false);
        } else if (strcmp(command, "debuglog") == 0) {
          Serial.println(F("Debug Log:"));
          Serial.println(debugLog);
        } else {
          processMappingCommand(command);
        }
      } else {
        if (strcmp(command, "enterprogrammode") == 0) {
          programMode = true;
          Serial.println(F("Program Mode Entered!"));
        } else if (strstr(command, " on ") || strstr(command, " on\0")) {
          processToggleCommand(command, true);
        } else if (strstr(command, " off")) {
          processToggleCommand(command, false);
        } else if (strstr(command, " status")) {
          char channelName[16];
          sscanf(command, "%s status", channelName);
          int8_t channelIdx = findChannelIndex(channelName);
          if (channelIdx != -1) displayChannelStatus(channelIdx);
          else Serial.println(F("Error: Channel not found."));
        } else if (strcmp(command, "debuglog") == 0) {
          Serial.println(F("Debug Log:"));
          Serial.println(debugLog);
        } else {
          Serial.println(F("Error: Invalid command."));
        }
      }
    } else if (serialBufferIndex < 95) {
      serialBuffer[serialBufferIndex++] = c;
    } else {
      Serial.println(F("Error: Serial buffer overflow."));
      serialBufferIndex = 0;
      memset(serialBuffer, 0, sizeof(serialBuffer));
    }
  }
}

void processMappingCommand(const char* command) {
  int8_t channelIndices[5] = {-1, -1, -1, -1, -1};
  int8_t switchIndices[5] = {-1, -1, -1, -1, -1};
  uint8_t numChannels = 0, numSwitches = 0;
  char channels[48], switches[48];

  if (sscanf(command, "%s %s", channels, switches) != 2) {
    Serial.println(F("Error: Invalid mapping."));
    return;
  }

  char* token = strtok(channels, ",");
  while (token && numChannels < 5) {
    int8_t channelIdx = findChannelIndex(token);
    if (channelIdx != -1) channelIndices[numChannels++] = channelIdx;
    else {
      snprintf(printBuffer, sizeof(printBuffer), "Error: Invalid channel: %s", token);
      Serial.println(printBuffer);
      return;
    }
    token = strtok(NULL, ",");
  }

  token = strtok(switches, ",");
  while (token && numSwitches < 5) {
    if (strncmp(token, "sw", 2) == 0) {
      uint8_t plateNum, switchNum;
      if (sscanf(token, "sw%d_%d", &plateNum, &switchNum) == 2) {
        int8_t globalSwitchIdx = getGlobalSwitchIndex(plateNum - 1, switchNum - 1);
        if (globalSwitchIdx != -1) switchIndices[numSwitches++] = globalSwitchIdx;
        else {
          snprintf(printBuffer, sizeof(printBuffer), "Error: Invalid switch: %s", token);
          Serial.println(printBuffer);
          return;
        }
      } else {
        Serial.println(F("Error: Invalid switch format."));
        return;
      }
    }
    token = strtok(NULL, ",");
  }

  if (!numChannels || !numSwitches) {
    Serial.println(F("Error: No valid channels/switches."));
    return;
  }

  uint8_t dimmableCount = 0;
  for (uint8_t i = 0; i < numChannels; i++) {
    if (psmPins[channelIndices[i]]) dimmableCount++;
  }
  if (dimmableCount > 1) {
    Serial.println(F("Error: One dimmable channel per group."));
    return;
  }

  for (uint8_t i = 0; i < numChannels; i++) {
    uint8_t c = channelIndices[i];
    for (uint8_t j = 0; j < numSwitchesPerChannel[c]; j++) {
      uint8_t s = channelToSwitchMap[c][j];
      for (uint8_t k = 0; k < numChannelsPerSwitch[s]; k++) {
        if (switchToChannelMap[s][k] == c) {
          switchToChannelMap[s][k] = switchToChannelMap[s][numChannelsPerSwitch[s] - 1];
          numChannelsPerSwitch[s]--;
          break;
        }
      }
    }
    numSwitchesPerChannel[c] = 0;
  }

  for (uint8_t i = 0; i < numSwitches; i++) {
    uint8_t s = switchIndices[i];
    for (uint8_t j = 0; j < numChannelsPerSwitch[s]; j++) {
      uint8_t c = switchToChannelMap[s][j];
      for (uint8_t k = 0; k < numSwitchesPerChannel[c]; k++) {
        if (channelToSwitchMap[c][k] == s) {
          channelToSwitchMap[c][k] = channelToSwitchMap[c][numSwitchesPerChannel[c] - 1];
          numSwitchesPerChannel[c]--;
          break;
        }
      }
    }
    numChannelsPerSwitch[s] = 0;
  }

  for (uint8_t i = 0; i < numChannels; i++) {
    uint8_t c = channelIndices[i];
    for (uint8_t j = 0; j < numSwitches; j++) {
      uint8_t s = switchIndices[j];
      channelToSwitchMap[c][numSwitchesPerChannel[c]++] = s;
      switchToChannelMap[s][numChannelsPerSwitch[s]++] = c;
    }
  }

  dirtyMappings = true;
  snprintf(printBuffer, sizeof(printBuffer), "Mapped %s to %s", channels, switches);
  Serial.println(printBuffer);
  logDebug(printBuffer);
}

void displayStatus() {
  Serial.println(F("Default Name  Renamed Name      State    Mappings                Timer"));
  Serial.println(F("------------  ----------------  -------  ----------------------  -------"));
  for (int c = 0; c < TOTAL_CHANNELS; c++) {
    char line[80] = {0};
    char renamedName[16] = {0};
    strncpy(renamedName, channelNames[c], 15);
    renamedName[15] = '\0';
    if (!renamedName[0]) strcpy(renamedName, "(Unnamed)");
    char state[8] = {0};
    if (channelStates[c]) {
      if (psmPins[c] != 0) snprintf(state, sizeof(state), "ON %d%%", map(DIM_MAX - dimLevel[c], 0, DIM_MAX, 0, 100));
      else strcpy(state, "ON");
    } else {
      strcpy(state, "OFF");
    }
    char mappings[24] = {0};
    int pos = 0;
    for (int i = 0; i < numSwitchesPerChannel[c]; i++) {
      int s = channelToSwitchMap[c][i];
      int plateIdx = 0, switchIdx = s;
      for (int p = 0; p < MAX_PLATES; p++) {
        if (switchIdx < numSwitchesPerPlate[p]) {
          plateIdx = p;
          break;
        }
        switchIdx -= numSwitchesPerPlate[p];
      }
      if (i > 0 && pos < sizeof(mappings) - 1) mappings[pos++] = ',';
      pos += snprintf(mappings + pos, sizeof(mappings) - pos, "SW%d_%d", plateIdx + 1, switchIdx + 1);
    }
    if (!mappings[0]) strcpy(mappings, "(None)");
    char timer[8] = {0};
    if (channelTimerDurations[c] > 0) {
      snprintf(timer, sizeof(timer), "%dm", channelTimerDurations[c]);
    } else {
      strcpy(timer, "None");
    }
    char defaultName[16] = {0};
    strncpy_P(defaultName, defaultChannelNames[c], 15);
    defaultName[15] = '\0';
    snprintf(line, sizeof(line), "%-12s  %-16s  %-7s  %-22s  %7s", defaultName, renamedName, state, mappings, timer);
    Serial.println(line);
    delay(10); // Prevent Serial buffer overflow
  }
  Serial.flush();
  Serial.println();
  char globalTimerStr[16] = {0};
  if (timerDuration > 0) {
    snprintf(globalTimerStr, sizeof(globalTimerStr), "%dm", timerDuration);
  } else {
    strcpy(globalTimerStr, "None");
  }
  Serial.print(F("Global Timer: "));
  Serial.println(globalTimerStr);
  delay(10);
  unsigned long totalSeconds = uptimeDays * 86400UL + uptimeSeconds;
  int days = totalSeconds / 86400;
  int hours = (totalSeconds % 86400) / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  char uptimeStr[20] = {0};
  snprintf(uptimeStr, sizeof(uptimeStr), "%dd %dh %dm %ds", days, hours, minutes, seconds);
  Serial.print(F("System Uptime: "));
  Serial.println(uptimeStr);
  delay(10);
  Serial.println();
  Serial.println(F("Plate ID      Address  Status                 Switch Count  Failures"));
  Serial.println(F("------------  -------  ---------------------  ------------  --------"));
  for (int p = 0; p < MAX_PLATES; p++) {
    char line[60] = {0};
    char address[8] = {0};
    snprintf(address, sizeof(address), "0x%02X", switchPlateI2CAddresses[p]);
    if (switchPlateI2CAddresses[p] == 0) strcpy(address, "0x00");
    char status[24] = "Online";
    if (switchPlateI2CAddresses[p] == 0) strcpy(status, "Not Configured");
    char plateName[12] = {0};
    snprintf(plateName, sizeof(plateName), "Plate %d", p + 1);
    snprintf(line, sizeof(line), "%-12s  %-7s  %-21s  %12d  %8d", plateName, address, status, numSwitchesPerPlate[p], 0);
    Serial.println(line);
    delay(10);
  }
  Serial.flush();
}

int8_t findChannelIndex(const char* channelName) {
  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    if (strcasecmp(channelName, channelNames[c]) == 0) return c;
    char defaultName[16];
    strncpy_P(defaultName, defaultChannelNames[c], 15);
    defaultName[15] = '\0';
    if (strcasecmp(channelName, defaultName) == 0) return c;
  }
  return -1;
}

void toggleChannelByName(const char* channelName, bool state) {
  int8_t channelIdx = findChannelIndex(channelName);
  if (channelIdx == -1) {
    Serial.println(F("Error: Channel not found."));
    return;
  }

  channelStates[channelIdx] = state;
  if (state) {
    if (psmPins[channelIdx]) dimLevel[channelIdx] = DIM_MIN;
    if (channelTimerDurations[channelIdx]) {
      timerEndTime[channelIdx] = millis() + (uint32_t)channelTimerDurations[channelIdx] * 60000UL;
    }
  } else {
    dimLevel[channelIdx] = DIM_MAX;
    timerEndTime[channelIdx] = 0;
  }

  for (uint8_t i = 0; i < numSwitchesPerChannel[channelIdx]; i++) {
    uint8_t s = channelToSwitchMap[channelIdx][i];
    for (uint8_t j = 0; j < numChannelsPerSwitch[s]; j++) {
      uint8_t c = switchToChannelMap[s][j];
      if (c != channelIdx) {
        channelStates[c] = state;
        if (state) {
          if (psmPins[c]) dimLevel[c] = DIM_MIN;
          if (channelTimerDurations[c]) {
            timerEndTime[c] = millis() + (uint32_t)channelTimerDurations[c] * 60000UL;
          }
        } else {
          dimLevel[c] = DIM_MAX;
          timerEndTime[c] = 0;
        }
      }
    }
  }

  updateSSRs();
  dirtyLEDs = true;
  char name[16];
  strncpy(name, channelNames[channelIdx], 15);
  name[15] = '\0';
  if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
  snprintf(printBuffer, sizeof(printBuffer), "%s %s", name, state ? "ON" : "OFF");
  if (state && psmPins[channelIdx]) {
    char dimStr[8];
    snprintf(dimStr, sizeof(dimStr), " %d", map(DIM_MAX - dimLevel[channelIdx], 0, DIM_MAX, 0, 100));
    strncat(printBuffer, dimStr, sizeof(printBuffer) - strlen(printBuffer) - 1);
  }
  Serial.println(printBuffer);
  logDebug(printBuffer);
}

void processToggleCommand(const char* command, bool state) {
  int dimLevelVal = -1;
  char channelName[16];
  if (state && strstr(command, " on ")) {
    char dimPart[8];
    if (sscanf(command, "%s on %s", channelName, dimPart) == 2) {
      dimLevelVal = atoi(dimPart);
      if (dimLevelVal < 0 || dimLevelVal > 100) {
        Serial.println(F("Error: Dim level 0-100."));
        return;
      }
    } else {
      sscanf(command, "%s", channelName);
    }
  } else {
    sscanf(command, "%s", channelName);
  }
  int8_t channelIdx = findChannelIndex(channelName);
  if (channelIdx == -1) {
    Serial.println(F("Error: Channel not found."));
    return;
  }
  toggleChannelByName(channelName, state);
  if (dimLevelVal != -1 && psmPins[channelIdx]) {
    dimLevel[channelIdx] = map(100 - dimLevelVal, 0, 100, 0, DIM_MAX);
    char name[16];
    strncpy(name, channelNames[channelIdx], 15);
    name[15] = '\0';
    if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
    snprintf(printBuffer, sizeof(printBuffer), "%s ON %d", name, dimLevelVal);
    Serial.println(printBuffer);
    logDebug(printBuffer);
  }
}

void processRenameCommand(const char* command) {
  char oldName[16], newName[16];
  if (sscanf(command, "rename %s %s", oldName, newName) != 2) {
    Serial.println(F("Error: Invalid rename."));
    return;
  }
  if (strlen(newName) > 15 || !strlen(newName)) {
    Serial.println(F("Error: Name 1-15 chars."));
    return;
  }
  int8_t channelIdx = findChannelIndex(oldName);
  if (channelIdx == -1) {
    Serial.println(F("Error: Channel not found."));
    return;
  }
  strncpy(channelNames[channelIdx], newName, 16);
  dirtyMappings = true;
  snprintf(printBuffer, sizeof(printBuffer), "Renamed %s to %s", oldName, newName);
  Serial.println(printBuffer);
  logDebug(printBuffer);
}

void processGlobalTimerCommand(const char* command) {
  int duration;
  if (sscanf(command, "globaltimer %d", &duration) != 1 || duration < 0 || duration > 65535) {
    Serial.println(F("Error: Invalid timer."));
    return;
  }
  timerDuration = duration;
  dirtyMappings = true;
  snprintf(printBuffer, sizeof(printBuffer), "Global Timer %dm", duration);
  Serial.println(printBuffer);
  logDebug(printBuffer);
}

void processChannelTimerCommand(const char* command) {
  char channelName[16];
  int duration;
  if (sscanf(command, "%s timer %d", channelName, &duration) != 2 || duration < 0 || duration > 65535) {
    Serial.println(F("Error: Invalid timer."));
    return;
  }
  int8_t channelIdx = findChannelIndex(channelName);
  if (channelIdx == -1) {
    Serial.println(F("Error: Channel not found."));
    return;
  }
  setChannelTimer(channelIdx, duration);
}

void displayChannelStatus(uint8_t channelIdx) {
  if (programMode) {
    char renamedName[16];
    strncpy(renamedName, channelNames[channelIdx], 15);
    renamedName[15] = '\0';
    if (!renamedName[0]) strcpy(renamedName, "(Unnamed)");
    char state[8];
    if (channelStates[channelIdx]) {
      if (psmPins[channelIdx]) snprintf(state, sizeof(state), "ON %d%%", map(DIM_MAX - dimLevel[channelIdx], 0, DIM_MAX, 0, 100));
      else strcpy(state, "ON");
    } else {
      strcpy(state, "OFF");
    }
    char mappings[24] = {0};
    uint8_t pos = 0;
    for (uint8_t i = 0; i < numSwitchesPerChannel[channelIdx]; i++) {
      uint8_t s = channelToSwitchMap[channelIdx][i];
      uint8_t plateIdx = 0, switchIdx = s;
      for (uint8_t p = 0; p < MAX_PLATES; p++) {
        if (switchIdx < numSwitchesPerPlate[p]) {
          plateIdx = p;
          break;
        }
        switchIdx -= numSwitchesPerPlate[p];
      }
      pos += snprintf(mappings + pos, sizeof(mappings) - pos, "SW%d_%d", plateIdx + 1, switchIdx + 1);
      if (i < numSwitchesPerChannel[channelIdx] - 1 && pos < sizeof(mappings) - 2) mappings[pos++] = ',';
    }
    if (!mappings[0]) strcpy(mappings, "(None)");
    char timer[8];
    if (channelTimerDurations[channelIdx]) snprintf(timer, sizeof(timer), "%dm", channelTimerDurations[channelIdx]);
    else strcpy(timer, "None");
    char defaultName[16];
    strncpy_P(defaultName, defaultChannelNames[channelIdx], 15);
    defaultName[15] = '\0';
    Serial.println(F("Default Name  Renamed Name      State    Mappings                Timer"));
    Serial.println(F("------------  ----------------  -------  ----------------------  -------"));
    snprintf(printBuffer, sizeof(printBuffer), "%-12s  %-16s  %-7s  %-22s  %s", defaultName, renamedName, state, mappings, timer);
    Serial.println(printBuffer);
  } else {
    char name[16];
    strncpy(name, channelNames[channelIdx], 15);
    name[15] = '\0';
    if (!name[0]) strncpy_P(name, defaultChannelNames[channelIdx], 15);
    snprintf(printBuffer, sizeof(printBuffer), "%s %s", name, channelStates[channelIdx] ? "on" : "off");
    if (channelStates[channelIdx] && psmPins[channelIdx]) {
      char dimStr[8];
      snprintf(dimStr, sizeof(dimStr), " %d", map(DIM_MAX - dimLevel[channelIdx], 0, DIM_MAX, 0, 100));
      strncat(printBuffer, dimStr, sizeof(printBuffer) - strlen(printBuffer) - 1);
    }
    Serial.println(printBuffer);
  }
}

void clearMappings() {
  memset(numSwitchesPerChannel, 0, sizeof(numSwitchesPerChannel));
  memset(channelToSwitchMap, 0, sizeof(channelToSwitchMap));
  memset(numChannelsPerSwitch, 0, sizeof(numChannelsPerSwitch));
  memset(switchToChannelMap, 0, sizeof(switchToChannelMap));
  dirtyMappings = true;
  Serial.println(F("Mappings cleared."));
  logDebug("Mappings cleared");
}

void systemReset() {
  clearMappings();
  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    channelStates[c] = false;
    dimLevel[c] = DIM_MAX;
    channelTimerDurations[c] = 0;
    timerEndTime[c] = 0;
    channelNames[c][0] = '\0';
  }
  timerDuration = 0;
  dirtyMappings = true;
  Serial.println(F("System reset."));
  logDebug("System reset");
}

void clearAllTimers() {
  memset(channelTimerDurations, 0, sizeof(channelTimerDurations));
  memset(timerEndTime, 0, sizeof(timerEndTime));
  timerDuration = 0;
  dirtyMappings = true;
  Serial.println(F("Timers cleared."));
  logDebug("Timers cleared");
}

// EEPROM Management
void saveMappingsToEEPROM() {
  if (!dirtyMappings) return;
  int offset = EEPROM_START_ADDRESS;

  EEPROM.put(offset, timerDuration);
  offset += sizeof(uint16_t);

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    EEPROM.put(offset, channelTimerDurations[c]);
    offset += sizeof(uint16_t);
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    EEPROM.put(offset, channelNames[c]);
    offset += 16;
  }

  for (uint8_t s = 0; s < MAX_SWITCHES; s++) {
    for (uint8_t i = 0; i < MAX_CHANNELS_PER_SWITCH; i++) {
      EEPROM.put(offset, switchToChannelMap[s][i]);
      offset += sizeof(uint8_t);
    }
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    for (uint8_t i = 0; i < MAX_SWITCHES_PER_CHANNEL; i++) {
      EEPROM.put(offset, channelToSwitchMap[c][i]);
      offset += sizeof(uint8_t);
    }
  }

  for (uint8_t s = 0; s < MAX_SWITCHES; s++) {
    EEPROM.put(offset, numChannelsPerSwitch[s]);
    offset += sizeof(uint8_t);
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    EEPROM.put(offset, numSwitchesPerChannel[c]);
    offset += sizeof(uint8_t);
  }

  dirtyMappings = false;
  Serial.println(F("EEPROM saved."));
  logDebug("EEPROM saved");
}

void loadMappingsFromEEPROM() {
  int offset = EEPROM_START_ADDRESS;

  uint16_t loadedTimerDuration;
  EEPROM.get(offset, loadedTimerDuration);
  timerDuration = (loadedTimerDuration <= 65535) ? loadedTimerDuration : 0;
  if (loadedTimerDuration > 65535) {
    Serial.println(F("Error: Invalid timerDuration."));
    logDebug("Invalid timerDuration");
  }
  offset += sizeof(uint16_t);

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    uint16_t loadedDuration;
    EEPROM.get(offset, loadedDuration);
    channelTimerDurations[c] = (loadedDuration <= 65535) ? loadedDuration : 0;
    if (loadedDuration > 65535) {
      snprintf(printBuffer, sizeof(printBuffer), "Invalid timer CH%d", c);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    offset += sizeof(uint16_t);
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    char loadedName[16];
    EEPROM.get(offset, loadedName);
    bool valid = (strlen(loadedName) <= 15);
    if (valid) {
      for (size_t i = 0; i < strlen(loadedName); i++) {
        if (loadedName[i] < 32 || loadedName[i] > 126) {
          valid = false;
          break;
        }
      }
    }
    if (valid) {
      strncpy(channelNames[c], loadedName, 16);
    } else {
      channelNames[c][0] = '\0';
      snprintf(printBuffer, sizeof(printBuffer), "Invalid name CH%d", c);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    offset += 16;
  }

  for (uint8_t s = 0; s < MAX_SWITCHES; s++) {
    for (uint8_t i = 0; i < MAX_CHANNELS_PER_SWITCH; i++) {
      uint8_t loadedChannel;
      EEPROM.get(offset, loadedChannel);
      switchToChannelMap[s][i] = (loadedChannel < TOTAL_CHANNELS) ? loadedChannel : 0;
      if (loadedChannel >= TOTAL_CHANNELS) {
        snprintf(printBuffer, sizeof(printBuffer), "Invalid map SW%d_%d", s, i);
        Serial.println(printBuffer);
        logDebug(printBuffer);
      }
      offset += sizeof(uint8_t);
    }
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    for (uint8_t i = 0; i < MAX_SWITCHES_PER_CHANNEL; i++) {
      uint8_t loadedSwitch;
      EEPROM.get(offset, loadedSwitch);
      channelToSwitchMap[c][i] = (loadedSwitch < MAX_SWITCHES) ? loadedSwitch : 0;
      if (loadedSwitch >= MAX_SWITCHES) {
        snprintf(printBuffer, sizeof(printBuffer), "Invalid map CH%d_%d", c, i);
        Serial.println(printBuffer);
        logDebug(printBuffer);
      }
      offset += sizeof(uint8_t);
    }
  }

  for (uint8_t s = 0; s < MAX_SWITCHES; s++) {
    uint8_t loadedCount;
    EEPROM.get(offset, loadedCount);
    if (loadedCount <= MAX_CHANNELS_PER_SWITCH) {
      numChannelsPerSwitch[s] = loadedCount;
    } else {
      numChannelsPerSwitch[s] = 0;
      memset(switchToChannelMap[s], 0, MAX_CHANNELS_PER_SWITCH);
      snprintf(printBuffer, sizeof(printBuffer), "Invalid switch count SW%d", s);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    offset += sizeof(uint8_t);
  }

  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    uint8_t loadedCount;
    EEPROM.get(offset, loadedCount);
    if (loadedCount <= MAX_SWITCHES_PER_CHANNEL) {
      numSwitchesPerChannel[c] = loadedCount;
    } else {
      numSwitchesPerChannel[c] = 0;
      memset(channelToSwitchMap[c], 0, MAX_SWITCHES_PER_CHANNEL);
      snprintf(printBuffer, sizeof(printBuffer), "Invalid channel count CH%d", c);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    offset += sizeof(uint8_t);
  }
}

void displayManual() {
  Serial.println(F("Smart Home Commands Manual"));
  Serial.println(F("Normal Mode:"));
  Serial.println(F("  EnterProgramMode               Enter Program Mode"));
  Serial.println(F("  <channel> ON [dim]             Turn ON, e.g., bikeroom ON 50"));
  Serial.println(F("  <channel> OFF                  Turn OFF, e.g., mainbedroom OFF"));
  Serial.println(F("Program Mode:"));
  Serial.println(F("  manual                         Show this manual"));
  Serial.println(F("  ExitProgramMode                Exit and save"));
  Serial.println(F("  rename <old> <new>             Rename, e.g., rename CH1_4 bikeroom"));
  Serial.println(F("  globaltimer <min>              Set timer, e.g., globaltimer 20"));
  Serial.println(F("  status                         Show status"));
  Serial.println(F("  <channel> status               Channel status, e.g., bikeroom status"));
  Serial.println(F("  clearmappings                  Clear mappings (Y/N)"));
  Serial.println(F("  clearalltimers                 Clear timers (Y/N)"));
  Serial.println(F("  systemreset                    Reset system (Y/N)"));
  Serial.println(F("  <channel> timer <min>          Set timer, e.g., bikeroom timer 10"));
  Serial.println(F("  <channel> ON [dim]             Turn ON, e.g., bikeroom ON 50"));
  Serial.println(F("  <channel> OFF                  Turn OFF, e.g., mainbedroom OFF"));
  Serial.println(F("  <channels> <switches>          Map, e.g., CH1_4 SW4_1"));
  Serial.println(F("  debuglog                       Show debug log"));
  Serial.println(F("Switch Behaviors:"));
  Serial.println(F("  Press (<950ms): Toggle"));
  Serial.println(F("  Hold (>=950ms): Dim"));
  Serial.println(F("  Hold (>=3000ms): Global Timer if OFF"));
  Serial.println(F("  Timer Active: LEDs ON, two 100ms OFF pulses every 1.5s"));
}

// Main Loop
void loop() {
  static uint32_t lastSerialOutput = 0;
  if (Serial.availableForWrite() < 32 && millis() - lastSerialOutput < 100) return;
  lastSerialOutput = millis();

  processSwitches();
  updateSSRs();
  updateTimers();
  processSerialInput();

  static uint32_t lastLEDUpdate = 0;
  if (dirtyLEDs || millis() - lastLEDUpdate >= 250) {
    updateAllLEDs();
    lastLEDUpdate = millis();
  }

  static uint32_t lastFlashToggle = 0;
  if (programMode && millis() - lastFlashToggle >= 1000) {
    flashState = !flashState;
    lastFlashToggle = millis();
    dirtyLEDs = true;
  }

  static uint32_t lastTimerFlash = 0;
  static uint8_t timerFlashPhase = 0;
  bool timerActive = false;
  for (uint8_t c = 0; c < TOTAL_CHANNELS; c++) {
    if (timerEndTime[c] > millis()) {
      timerActive = true;
      break;
    }
  }
  if (timerActive && !programMode) {
    if (timerFlashPhase == 0 && millis() - lastTimerFlash >= 1500) {
      timerFlashState = 0;
      timerFlashPhase = 1;
      lastTimerFlash = millis();
      dirtyLEDs = true;
    } else if (timerFlashPhase == 1 && millis() - lastTimerFlash >= 100) {
      timerFlashState = 1;
      timerFlashPhase = 2;
      lastTimerFlash = millis();
      dirtyLEDs = true;
    } else if (timerFlashPhase == 2 && millis() - lastTimerFlash >= 100) {
      timerFlashState = 0;
      timerFlashPhase = 3;
      lastTimerFlash = millis();
      dirtyLEDs = true;
    } else if (timerFlashPhase == 3 && millis() - lastTimerFlash >= 100) {
      timerFlashState = 1;
      timerFlashPhase = 0;
      lastTimerFlash = millis();
      dirtyLEDs = true;
    }
  } else if (!timerActive && timerFlashState) {
    timerFlashState = 0;
    timerFlashPhase = 0;
    dirtyLEDs = true;
  }

  static uint32_t lastUptimeUpdate = 0;
  if (millis() - lastUptimeUpdate >= 1000) {
    uptimeSeconds++;
    if (uptimeSeconds >= 86400) {
      uptimeSeconds = 0;
      uptimeDays++;
    }
    lastUptimeUpdate = millis();
  }

  static uint32_t lastSRAMCheck = 0;
  if (millis() - lastSRAMCheck >= 3600000) {
    int sram = freeSRAM();
    if (sram < 500) {
      snprintf(printBuffer, sizeof(printBuffer), "Low SRAM: %d bytes", sram);
      Serial.println(printBuffer);
      logDebug(printBuffer);
    }
    lastSRAMCheck = millis();
  }

  static uint32_t lastI2CReset = 0;
  if (millis() - lastI2CReset >= 86400000) {
    for (uint8_t p = 0; p < MAX_PLATES; p++) {
      if (i2cFailureCount[p] > 10) resetI2CPlate(p);
    }
    lastI2CReset = millis();
  }

  static uint32_t lastTimerValidation = 0;
  if (millis() - lastTimerValidation >= 3600000) {
    validateTimers();
    lastTimerValidation = millis();
  }
}
