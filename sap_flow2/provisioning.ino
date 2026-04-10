// -------------------------------------------------------
// Validate ID string - alphanumeric, 1-12 chars
// -------------------------------------------------------
bool isValidID(const char* s) {
  int len = strlen(s);
  if (len == 0 || len > ID_MAX_LEN) return false;
  for (int i = 0; i < len; i++) {
    if (!isalnum((unsigned char)s[i])) return false;
  }
  return true;
}

// -------------------------------------------------------
// Validate DateTime fields are in range
// -------------------------------------------------------
bool isValidDateTime(int year, int month, int day,
                     int hour, int minute, int second) {
  if (year < 2020 || year > 2099) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > 31) return false;
  if (hour < 0 || hour > 23) return false;
  if (minute < 0 || minute > 59) return false;
  if (second < 0 || second > 59) return false;
  return true;
}

// -------------------------------------------------------
// Read a line from Serial with timeout (ms)
// Returns true if a line was received, false on timeout
// or if provisioning button was pressed to escape
// -------------------------------------------------------
bool readSerialLine(char* buf, int bufSize, unsigned long timeoutMs) {
  unsigned long start = millis();
  int pos = 0;
  memset(buf, 0, bufSize);

  while (millis() - start < timeoutMs) {
    // Check for escape via provisioning button
    if (provisioningRequested) {
      provisioningRequested = false;
      Serial.println("\nButton press - exiting provisioning.");
      return false;
    }

    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (pos > 0) {
          buf[pos] = '\0';
          return true;
        }
        // ignore empty lines
      } else if (pos < bufSize - 1) {
        buf[pos++] = c;
      }
    }
    delay(10);
  }
  Serial.println("\nTimeout - exiting provisioning.");
  return false;
}

// -------------------------------------------------------
// Read a single character response with timeout
// Returns the char, or 0 on timeout/escape
// -------------------------------------------------------
char readSerialChar(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (provisioningRequested) {
      provisioningRequested = false;
      Serial.println("\nButton press - exiting provisioning.");
      return 0;
    }
    if (Serial.available() > 0) {
      char c = Serial.read();
      // flush rest of line
      while (Serial.available()) Serial.read();
      return c;
    }
    delay(10);
  }
  Serial.println("\nTimeout.");
  return 0;
}

// -------------------------------------------------------
// Save ID to EEPROM
// -------------------------------------------------------
void saveIDtoEEPROM(const char* id) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  for (int i = 0; i <= (int)strlen(id); i++) {  // includes null terminator
    EEPROM.write(EEPROM_ID_ADDR + i, id[i]);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("ID saved to EEPROM.");
}

// -------------------------------------------------------
// Load ID from EEPROM
// Returns true if a valid ID was found
// -------------------------------------------------------
bool loadIDfromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_VAL) {
    Serial.println("EEPROM not initialized - no stored ID.");
    EEPROM.end();
    return false;
  }

  char idBuf[ID_MAX_LEN + 1];
  for (int i = 0; i <= ID_MAX_LEN; i++) {
    idBuf[i] = EEPROM.read(EEPROM_ID_ADDR + i);
  }
  idBuf[ID_MAX_LEN] = '\0';  // ensure null termination

  EEPROM.end();

  if (!isValidID(idBuf)) {
    Serial.println("EEPROM contains invalid ID - ignoring.");
    return false;
  }

  deviceID = String(idBuf);
  Serial.println("Loaded ID from EEPROM: " + deviceID);
  return true;
}

// -------------------------------------------------------
// Provisioning mode
// Entered via button press interrupt
// User can also press button again to escape at any time
// -------------------------------------------------------
void provisioningMode() {
  Serial.println("\n*** PROVISIONING MODE ***");
  Serial.println("Press provisioning button at any time to exit.\n");

  // Re-arm interrupt so button can escape provisioning
  provisioningRequested = false;
  attachInterrupt(digitalPinToInterrupt(PROVISION_PIN), onProvisionButton, FALLING);

  // Flash all LEDs to indicate provisioning mode
  allLEDs(LOW);
  for (int i = 0; i < 6; i++) {
    allLEDs(HIGH);
    delay(150);
    allLEDs(LOW);
    delay(150);
  }
  digitalWrite(POWER_LED, HIGH);
  digitalWrite(YELLOW_LED, HIGH);  // yellow on = provisioning active

  // ---- Display current state before asking for new values ----
  Serial.println("Current device state:");
  Serial.printf("  Date/Time : %s\n", getTimestamp().c_str());
  Serial.printf("  Device ID : %s\n", deviceID.c_str());
  Serial.println();

  const unsigned long INPUT_TIMEOUT = 120000;  // 2 min to enter input

  // ---- Step 1: Get date/time and ID from user ----
  char inputBuf[64];
  int Year, Month, Day, Hour, Minute, Second;
  char idBuf[ID_MAX_LEN + 1];
  bool parsed = false;

  while (!parsed) {
    Serial.println("Enter date-time and ID as: YYYY/MM/DD HH:MM:SS DEVICEID");
    Serial.print("timeout in 2 minutes");


    if (!readSerialLine(inputBuf, sizeof(inputBuf), INPUT_TIMEOUT)) {
      // Timeout or button escape
      goto exitProvisioning;
    }

    // Parse the input
    memset(idBuf, 0, sizeof(idBuf));
    int parsed_count = sscanf(inputBuf, "%d/%d/%d %d:%d:%d %12s",
                              &Year, &Month, &Day,
                              &Hour, &Minute, &Second,
                              idBuf);

    if (parsed_count < 6) {
      Serial.println("ERROR: Could not parse input. Please try again.\n");
      continue;
    }

    // Validate date/time
    if (!isValidDateTime(Year, Month, Day, Hour, Minute, Second)) {
      Serial.println("ERROR: Date/time values out of range. Please try again.\n");
      continue;
    }

    // Validate ID if provided
    if (parsed_count == 7) {
      // Convert to uppercase for consistency
      for (int i = 0; idBuf[i]; i++) idBuf[i] = toupper(idBuf[i]);
      if (!isValidID(idBuf)) {
        Serial.println("ERROR: ID must be 1-12 alphanumeric characters. Please try again.\n");
        continue;
      }
    } else {
      // No ID provided - keep existing
      strncpy(idBuf, deviceID.c_str(), ID_MAX_LEN);
      idBuf[ID_MAX_LEN] = '\0';
      Serial.println("No ID provided - keeping existing: " + deviceID);
    }

    parsed = true;
  }

  // ---- Step 2: Show parsed values and ask for confirmation ----
  {
    Serial.println("\nParsed values:");
    Serial.printf("  Date/Time : %04d/%02d/%02d %02d:%02d:%02d\n",
                  Year, Month, Day, Hour, Minute, Second);
    Serial.printf("  Device ID : %s\n", idBuf);
    Serial.println("\nConfirm? Enter y to accept, n to re-enter, or press button to exit:");

    while (Serial.available() > 0) Serial.read();  // clean out the buffer before response
    char confirm = readSerialChar(INPUT_TIMEOUT);

    if (confirm == 0) {
      // Timeout or button escape
      goto exitProvisioning;
    }

    // if (confirm == 'n' || confirm == 'N') {
    if (confirm != 'y' && confirm != 'Y') {
      Serial.printf("Received '%c'\n", confirm);
      Serial.println("Re-entering...\n");
      // Restart provisioning from the top
      provisioningMode();
      return;
    }

    // if (confirm != 'y' && confirm != 'Y') {
    //   Serial.println("Unrecognised input - exiting provisioning without changes.");
    //   goto exitProvisioning;
    // }
  }

  // ---- Step 3: Commit changes ----
  {
    Serial.println("\nApplying changes...");

    // Set RTC
    DateTime newTime(Year, Month, Day, Hour, Minute, Second);
    rtc_ds3231.adjust(newTime);
    Serial.print("RTC set to: ");
    Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                  Year, Month, Day, Hour, Minute, Second);

    // Update global ID
    deviceID = String(idBuf);
    Serial.println("Device ID set to: " + deviceID);

    // Save ID to EEPROM
    saveIDtoEEPROM(idBuf);
    // At end of provisioningMode() Step 3, after updating deviceID:
    fileName = deviceID + ".csv";
    Serial.println("File name updated to: " + fileName);

    Serial.println("\n*** PROVISIONING COMPLETE ***");

    // Confirmation blink - green
    allLEDs(LOW);
    for (int i = 0; i < 6; i++) {
      digitalWrite(GREEN_LED, HIGH);
      delay(150);
      digitalWrite(GREEN_LED, LOW);
      delay(150);
    }
  }

  exitProvisioning:
  // Restore LEDs
  allLEDs(LOW);
  digitalWrite(POWER_LED, HIGH);
  digitalWrite(YELLOW_LED, LOW);

  // Detach and re-arm interrupt cleanly
  detachInterrupt(digitalPinToInterrupt(PROVISION_PIN));
  provisioningRequested = false;
  attachInterrupt(digitalPinToInterrupt(PROVISION_PIN), onProvisionButton, FALLING);

  Serial.println("Returning to normal operation.\n");
}

// -------------------------------------------------------
// Check flag and enter provisioning if requested
// Call this at safe points throughout setup()
// -------------------------------------------------------
void checkProvisioning() {
  if (!provisioningRequested) return;
  provisioningRequested = false;  // clear flag before entering
  heaterOFF();  // safety - ensure heater is off before provisioning
  provisioningMode();
}

// -------------------------------------------------------
// ISR - keep it minimal, just set the flag
// -------------------------------------------------------
void onProvisionButton() {
  provisioningRequested = true;
}
