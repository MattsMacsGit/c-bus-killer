// EEPROM Backup/Restore for MattSmartHome (Arduino Mega)
// Backup/restore 1322 bytes for mappings (e.g., bikeroom)
// TESTING (2000-3321) to LTS (0-1321)
// Author: Grok (xAI), May 2025

#include <EEPROM.h>

// --- Configuration ---
const char* POSITION = "LTS"; // "LTS" for restore, "TESTING" for backup
const char* OPERATION = "RESTORE"; // "RESTORE" to restore

// --- Constants ---
const int LTS_START = 0;
const int LTS_END = 1322;
const int TESTING_START = 2000;
const int TESTING_END = 3322;
const int EXPECTED_BYTES = 1322;

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println(F("EEPROM Backup/Restore - MattSmartHome"));

  int startAddr, endAddr;
  if (strcmp(POSITION, "LTS") == 0) {
    startAddr = LTS_START;
    endAddr = LTS_END;
    Serial.println(F("Position: LTS (0-1321)"));
  } else if (strcmp(POSITION, "TESTING") == 0) {
    startAddr = TESTING_START;
    endAddr = TESTING_END;
    Serial.println(F("Position: TESTING (2000-3321)"));
  } else {
    Serial.println(F("ERROR: Invalid POSITION. Halting."));
    while (true);
  }

  Serial.println(F("Set Serial Monitor to 115200 baud."));
  Serial.println(F("Press Enter to start..."));
  unsigned long timeout = millis() + 30000; // 30s timeout
  while (Serial.available() == 0 && millis() < timeout);
  if (Serial.available() == 0) {
    Serial.println(F("Timeout. Starting anyway."));
  }
  while (Serial.available()) Serial.read(); // Clear buffer

  if (strcmp(OPERATION, "BACKUP") == 0) {
    backupEEPROM(startAddr);
  } else if (strcmp(OPERATION, "RESTORE") == 0) {
    restoreEEPROM(startAddr);
  } else {
    Serial.println(F("ERROR: Invalid OPERATION. Halting."));
    while (true);
  }
}

// --- Backup ---
void backupEEPROM(int startAddr) {
  Serial.println(F("BACKUP: Copy hex to backup_2025-05-04.txt"));
  Serial.println(F("BACKUP START"));

  int bytesWritten = 0;
  for (int addr = startAddr; addr < startAddr + EXPECTED_BYTES; addr++) {
    byte value = EEPROM.read(addr);
    if (value < 16) Serial.print("0");
    Serial.print(value, HEX);
    Serial.print(" ");
    bytesWritten++;
    if (bytesWritten % 16 == 0) Serial.println();
    if (bytesWritten % 256 == 0) {
      Serial.flush();
      delay(100);
    }
  }
  if (bytesWritten % 16 != 0) Serial.println();

  Serial.println(F("BACKUP COMPLETE"));
  Serial.print(F("Wrote "));
  Serial.print(bytesWritten);
  Serial.println(F(" bytes."));
}

// --- Restore ---
void restoreEEPROM(int startAddr) {
  Serial.println(F("RESTORE: Paste entire hex file, press Enter, wait ~10s"));

  // Clear LTS
  if (strcmp(POSITION, "LTS") == 0) {
    for (int i = LTS_START; i < LTS_END; i++) EEPROM.write(i, 0xFF);
    Serial.println(F("Cleared LTS (0-1999)"));
  }

  Serial.println(F("Paste hex data now..."));

  int addr = startAddr;
  int bytesRestored = 0;
  char buffer[128] = {0};
  int bufferIndex = 0;

  while (bytesRestored < EXPECTED_BYTES) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || bufferIndex >= 127) {
        buffer[bufferIndex] = '\0';
        char* token = strtok(buffer, " ");
        while (token && bytesRestored < EXPECTED_BYTES) {
          char* endPtr;
          long value = strtol(token, &endPtr, 16);
          if (*endPtr != '\0' || value < 0 || value > 255) {
            Serial.print(F("ERROR: Invalid hex at byte "));
            Serial.println(bytesRestored);
            while (true);
          }
          EEPROM.write(addr++, (byte)value);
          bytesRestored++;
          token = strtok(NULL, " ");
        }
        bufferIndex = 0;
        memset(buffer, 0, sizeof(buffer));
        if (bytesRestored < EXPECTED_BYTES) {
          Serial.print(F("Progress: "));
          Serial.print(bytesRestored);
          Serial.print(F("/"));
          Serial.print(EXPECTED_BYTES);
          Serial.println(F(" bytes restored"));
        }
      } else if (c != '\r') {
        buffer[bufferIndex++] = c;
      }
      if (Serial.available() > 50) {
        delay(100);
      }
    }
  }

  Serial.println(F("RESTORE COMPLETE"));
  Serial.println(F("Re-upload V2.3 with EEPROM_MODE = 'LTS'."));
}

// --- Loop ---
void loop() {
  // Do nothing
}
