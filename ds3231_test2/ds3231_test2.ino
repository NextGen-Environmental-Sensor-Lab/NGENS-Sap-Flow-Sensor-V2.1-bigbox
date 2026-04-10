// CODE TO TEST THE SAP FLOW SENSOR TIMING SCHEMA WITH
// THE DS3231. Also checks the Aux interrupt button.

#include <RTClib.h>
#include <Wire.h>
#include <EEPROM.h>

// -------------------------------------------------------
// Non-volatile storage layout in EEPROM
// Address 0:     magic byte (0xA5 = initialized)
// Address 1-13:  ID string (12 chars + null terminator)
// -------------------------------------------------------
#define EEPROM_SIZE 16
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_ID_ADDR 1
#define EEPROM_MAGIC_VAL 0xA5
#define ID_MAX_LEN 12

#define T_MINS 4

// LED pins
#define RED_LED 13
#define YELLOW_LED 12
#define GREEN_LED 11
#define POWER_LED 10
#define TIMER_LED 9
#define ERROR_LED 5
// Provisioning button
#define PROVISION_PIN A1

RTC_DS3231 rtc_ds3231;
// Global device ID
String deviceID = "UNKNOWN";

// Flag set by ISR - must be volatile
volatile bool provisioningRequested = false;

// Forward declarations
void fireAlarm2();
void setNextAlarm();
void turnOff();
void printDateTime();
void printRegisterState();
void initLEDs();
void allLEDs(int);
void checkProvisioning();
void provisioningMode();
void onProvisionButton();  // ISR

void setup() {
  // LEDs first - red on immediately so user knows system is alive
  initLEDs();
  digitalWrite(POWER_LED, HIGH);
  digitalWrite(RED_LED, HIGH);

  Serial.begin(115200);
  delay(800);
  Serial.println(__FILE__);

  loadIDfromEEPROM();

  // Configure provisioning button pin
  // Button connects A1 to GND, so we need internal pull-up
  pinMode(PROVISION_PIN, INPUT_PULLUP);
  // Attach interrupt - triggers on falling edge (HIGH -> LOW when button pressed)
  attachInterrupt(digitalPinToInterrupt(PROVISION_PIN), onProvisionButton, FALLING);
  Serial.println("Provisioning button interrupt armed on A1.");

  if (!rtc_ds3231.begin()) {
    Serial.println("Couldn't find RTC!");
    while (1) {
      digitalWrite(ERROR_LED, !digitalRead(ERROR_LED));
      delay(200);
    }
  }
  Serial.println("RTC found.");

  rtc_ds3231.disable32K();
  rtc_ds3231.writeSqwPinMode(DS3231_OFF);

  // Serial.println("Initial register state:");
  // printRegisterState();

  // Latch power - RED stays on during this, GREEN when latched
  fireAlarm2();

  // Serial.println("Register state after fireAlarm2():");
  // printRegisterState();

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);  // "safe to release"

  printDateTime();

  //RTC initialize date/time if needed
  if (rtc_ds3231.lostPower()) {
    digitalWrite(TIMER_LED, HIGH);
    DateTime dt = inputDateTime();
    if (dt.isValid())
      rtc_ds3231.adjust(dt);
    digitalWrite(TIMER_LED, 0);
  }

  // Check provisioning before arming next alarm
  checkProvisioning();

  setNextAlarm();

  Serial.println("Measurement cycle would run here.");
  // Simulate measurement cycle with provisioning checks
  for (int i = 0; i < 10; i++) {
    Serial.print("Cycle step ");
    Serial.println(i);
    checkProvisioning();  // check at each step
    delay(1000);
  }

  // Check provisioning before powering off
  checkProvisioning();

  turnOff();
  // Green off - normal operation continues
  digitalWrite(GREEN_LED, LOW);
  delay(100);
  Serial.println("If we reach here, we are on USB power - entering loop()");
}

void loop() {
  if (rtc_ds3231.alarmFired(2)) {
    Serial.println("\nAlarm2 fired in loop() - rebooting...");
    delay(300);
    rp2040.reboot();
  }

  if (rtc_ds3231.alarmFired(1)) {
    Serial.println("\nAlarm1 fired in loop() - rebooting...");
    delay(300);
    rp2040.reboot();
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 3000) {
    Serial.print(".");
    printDateTime();
    // printRegisterState();
    lastPrint = millis();
  }
}

// -------------------------------------------------------
// ISR - keep it minimal, just set the flag
// -------------------------------------------------------
void onProvisionButton() {
  provisioningRequested = true;
}

// Check flag and enter provisioning if requested
// Call this at safe points throughout setup()
// -------------------------------------------------------
void checkProvisioning() {
  if (!provisioningRequested) return;
  provisioningRequested = false;  // clear flag before entering
  provisioningMode();
}

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
    allLEDs(HIGH); delay(150);
    allLEDs(LOW);  delay(150);
  }
  digitalWrite(POWER_LED, HIGH);
  digitalWrite(YELLOW_LED, HIGH);  // yellow on = provisioning active

    // ---- Display current state before asking for new values ----
  Serial.println("Current device state:");
  Serial.printf("  Date/Time : %s\n", getTimestamp().c_str());
  Serial.printf("  Device ID : %s\n", deviceID.c_str());
  Serial.println();

  const unsigned long INPUT_TIMEOUT = 120000;  // 2 minutes to enter data

  // ---- Step 1: Get date/time and ID from user ----
  char inputBuf[64];
  int Year, Month, Day, Hour, Minute, Second;
  char idBuf[ID_MAX_LEN + 1];
  bool parsed = false;

  while (!parsed) {
    Serial.println("Enter date-time and ID as: YYYY/MM/DD HH:MM:SS DEVICEID");
    Serial.println("You have 2 minutes");

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
      Serial.println("ERROR: Could not parse input. Try again.\n");
      continue;
    }

    // Validate date/time
    if (!isValidDateTime(Year, Month, Day, Hour, Minute, Second)) {
      Serial.println("ERROR: Date/time values out of range. Try again.\n");
      continue;
    }

    // Validate ID if provided
    if (parsed_count == 7) {
      // Convert to uppercase for consistency
      for (int i = 0; idBuf[i]; i++) idBuf[i] = toupper(idBuf[i]);
      if (!isValidID(idBuf)) {
        Serial.println("ERROR: ID must be 1-12 alphanumeric characters. Try again.\n");
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
    Serial.println("\nEnter: y to accept, n to re-enter, button to exit:");

    while(Serial.available()>0) Serial.read(); // clean out the buffer before response
    char confirm = readSerialChar(INPUT_TIMEOUT);

    if (confirm == 0) {
      // Timeout or button escape
      goto exitProvisioning;
    }

    // if (confirm == 'n' || confirm == 'N') {
    if (confirm != 'y' && confirm != 'Y') {
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

    Serial.println("\n*** PROVISIONING COMPLETE ***");

    // Confirmation blink - green
    allLEDs(LOW);
    for (int i = 0; i < 6; i++) {
      digitalWrite(GREEN_LED, HIGH); delay(150);
      digitalWrite(GREEN_LED, LOW);  delay(150);
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

// Initialize all LED pins
// -------------------------------------------------------
void initLEDs() {
  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  pinMode(YELLOW_LED, OUTPUT);
  digitalWrite(YELLOW_LED, LOW);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  pinMode(POWER_LED, OUTPUT);
  digitalWrite(POWER_LED, LOW);
  pinMode(TIMER_LED, OUTPUT);
  digitalWrite(TIMER_LED, LOW);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ERROR_LED, LOW);
}

void allLEDs(int ONOFF) {
  digitalWrite(RED_LED, ONOFF);
  digitalWrite(YELLOW_LED, ONOFF);
  digitalWrite(GREEN_LED, ONOFF);
  digitalWrite(POWER_LED, ONOFF);
  digitalWrite(TIMER_LED, ONOFF);
  digitalWrite(ERROR_LED, ONOFF);
}

void printDateTime() {
  DateTime now = rtc_ds3231.now();
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d> \n",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  Serial.print(buf);
}

// Print raw DS3231 register state for diagnostics
// -------------------------------------------------------
void printRegisterState() {
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  uint8_t err = Wire.endTransmission();
  Wire.requestFrom(0x68, 1);
  uint8_t control = Wire.available() ? Wire.read() : 0xFF;

  Wire.beginTransmission(0x68);
  Wire.write(0x0F);
  Wire.endTransmission();
  Wire.requestFrom(0x68, 1);
  uint8_t status = Wire.available() ? Wire.read() : 0xFF;

  char buf[128];
  snprintf(buf, sizeof(buf),
           "i2c err: %d, ctrl (0x0E): 0x%02X, stat (0x0F): 0x%02X"
           ", INTCN: %s, A2IE: %s, A1IE: %s, A2F: %s, A1F: %s",
           err, control, status,
           (control & 0x04) ? "SET" : "NOT SET",
           (control & 0x02) ? "SET" : "NOT SET",
           (control & 0x01) ? "SET" : "NOT SET",
           (status & 0x02) ? "SET" : "NOT SET",
           (status & 0x01) ? "SET" : "NOT SET");
  Serial.println(buf);
}

// Latch power on if woken by button press.
// RED LED on = hold button
// GREEN LED on = latched, safe to release
// -------------------------------------------------------
void fireAlarm2() {
  if (rtc_ds3231.alarmFired(2)) {
    Serial.println("Alarm2 already fired. Normal RTC wakeup. Power is latched.");
    return;
  }

  //digitalWrite(RED_LED, HIGH);
  //Serial.println("Alarm2 NOT fired- button press");
  DateTime fireTime = rtc_ds3231.now() + TimeSpan(2);
  if (!rtc_ds3231.setAlarm1(fireTime, DS3231_A1_Date)) {
    Serial.println("ERROR: Failed to set Alarm1!");
    digitalWrite(ERROR_LED, HIGH);
    return;
  }
  Serial.printf("Alarm1 set for: %02d:%02d:%02d\n", fireTime.hour(), fireTime.minute(), fireTime.second());

  // Enable Alarm1 interrupt (A1IE = bit0, INTCN = bit2)
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.endTransmission();
  Wire.requestFrom(0x68, 1);
  if (!Wire.available()) {
    Serial.println("ERROR: No response reading control register!");
    digitalWrite(ERROR_LED, HIGH);
    return;
  }
  uint8_t control = Wire.read();
  control |= 0x05;  // INTCN (bit2) + A1IE (bit0)
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.write(control);
  Wire.endTransmission();
  //Serial.println("Alarm1 interrupt enabled. Waiting for Alarm1 to fire...");

  // Wait for Alarm1 to fire
  unsigned long start = millis();
  while (!rtc_ds3231.alarmFired(1)) {
    if (millis() - start > 5000) {
      Serial.println("TIMEOUT: Button released too early.");
      digitalWrite(RED_LED, LOW);
      digitalWrite(ERROR_LED, HIGH);
      return;
    }
    //digitalWrite(RED_LED, !digitalRead(RED_LED));
    delay(50);
  }
  // Latched! Signal user to release button
  //Serial.println("Alarm1 fired. Power latched. Release the button ok.");
}

// Arm next Alarm2 wakeup at next T_MINS boundary
// -------------------------------------------------------
void setNextAlarm() {
  DateTime currentTime = rtc_ds3231.now();
  int nextMinute = currentTime.minute() + 1;
  while (nextMinute % T_MINS) {
    nextMinute++;
  }
  if (nextMinute >= 60) {
    nextMinute -= 60;
  }

  Serial.print("Setting next Alarm2 for minute: ");
  Serial.println(nextMinute);

  DateTime wakeTime = DateTime(0, 0, 0, 0, nextMinute, 0);

  if (!rtc_ds3231.setAlarm2(wakeTime, DS3231_A2_Minute)) {
    Serial.println("ERROR: Failed to set Alarm2!");
  } else {
    Serial.println("Alarm2 set successfully.");
  }
}

// Power down - clear both alarms to release SQW
// -------------------------------------------------------
void turnOff() {
  // Serial.println("turnOff() called - cutting power.");
  // printRegisterState();
  Serial.println("Both alarms cleared. Power should cut now.\n");
  allLEDs(LOW);  // all LEDs off before power cuts
  rtc_ds3231.clearAlarm(1);
  rtc_ds3231.clearAlarm(2);
}

// Enter the current date/time from the serial monitor
// -------------------------------------------------------
DateTime inputDateTime() {
  // char datetime[32] = "YYYY/MM/DD hh:mm:ss";
  // rtc_ds3231.now().toString(datetime);

  while (Serial.available() > 0)
    Serial.read();

  Serial.println("Enter date-time as YYYY/MM/dd hh:mm:ss");
  static char message[32];
  static unsigned int message_pos = 0;

  while (Serial.available() == 0) {};
  while (Serial.available() > 0) {
    char inByte = Serial.read();
    //Message coming in (check not terminating character) and guard for over message size
    if (inByte != '\n' && (message_pos < 32 - 1)) {
      //Add the incoming byte to our message
      message[message_pos] = inByte;
      message_pos++;
    } else {
      //Add null character to string
      message[message_pos] = '\0';
      //Print the message (or do other things)
      Serial.println(message);
      message_pos = 0;
    }
  }
  if (message) {
    int Year, Month, Day, Hour, Minute, Second;
    sscanf(message, "%d/%d/%d %d:%d:%d", &Year, &Month, &Day, &Hour, &Minute, &Second);
    return DateTime(Year, Month, Day, Hour, Minute, Second);
  } else
    return DateTime(0, 0, 0, 0, 0, 0);
}

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

/*
  */
String getTimestamp(DateTime DT) {
  char datetime[32] = "YYYY/MM/DD hh:mm:ss";
  DT.toString(datetime);
  return String(datetime);
}

/*
  */
String getTimestamp() {
  return getTimestamp(rtc_ds3231.now());
}

