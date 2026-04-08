
/*   This will sleep until the next multiple of T_MINS.
  If T_MINS is 30, it will sleep until the minute hand says 0 or 30.
  If T_MINS is 2, it will sleep until the minute hand says 0, 2, ... 58.
  Important: If the measurement cycle time is more than T_MINS, it will not 
  take a measurement cycle every T_MINS. For examplle, if T_MINS = 2 but 
  the measurement cycle takes 3 minutes, it will actually measure every 4 minutes!
  // This will set the alarm for the next measurement
  */
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

  // We're ignoring the year through hour, only alarming on the minute and second fields.s
  DateTime wakeTime = DateTime(0, 0, 0, 0, nextMinute, 0);

  if (!rtc_ds3231.setAlarm2(wakeTime, DS3231_A2_Minute)) {
    Serial.println("ERROR: Failed to set Alarm2!");
  } else {
    //Serial.println("Alarm2 set successfully.");
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

// Latch power on if woken by button press.
  // RED LED on = hold button
  // GREEN LED on = latched, safe to release
  // -------------------------------------------------------
void fireAlarm2() {
  if (rtc_ds3231.alarmFired(2)) {
    //Serial.println("Alarm2 already fired. Normal RTC wakeup. Power is latched.");
    return;
  }

  DateTime fireTime = rtc_ds3231.now() + TimeSpan(2);
  if (!rtc_ds3231.setAlarm1(fireTime, DS3231_A1_Date)) {
    Serial.println("ERROR: Failed to set Alarm1!");
    digitalWrite(ERROR_LED, HIGH);
    return;
  }
  //Serial.printf("Alarm1 set for: %02d:%02d:%02d\n", fireTime.hour(), fireTime.minute(), fireTime.second());

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
      Serial.println("TIMEOUT: Button released too early - power latch failed.");
      digitalWrite(RED_LED, LOW);
      digitalWrite(ERROR_LED, HIGH);
      return;
    }
    //digitalWrite(RED_LED, !digitalRead(RED_LED));
    delay(50);
  }

  // Latched! Signal user to release button
  Serial.println("Alarm1 fired. Power latched. Release the button ok.");
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);  // "safe to release"
}

/*  Infinite blink error LED with ms interval
  */
void errorBlinkLoop(int ms) {
  while (true) {
    digitalWrite(ERROR_LED, !digitalRead(ERROR_LED));
    delay(ms);
  }
}

/* For getting correct date and time for SD card
  */
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = rtc_ds3231.now();
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

/*
  */
void initializeSD_ADC() {
  // NEW FOR RP2040
  SdSpiConfig config(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &SPI1);
  //
  // Setup the callback for using the correct date and time for file modification
  SdFile::dateTimeCallback(dateTime);

  //  if (!SD.begin(SD_CHIP_SELECT)) {
  // NEW FOR RP2040
  if (!SD.begin(config)) {
    Serial.println("Card failed or not present!");
    errorBlinkLoop(250);
  }
  Serial.println("Card Initialized");

  ads1.setGain(GAIN_TWO);
  ads2.setGain(GAIN_TWO);
  if (!ads1.begin(0x48)) {
    Serial.println("Failed to initialized ADS1.");
    digitalWrite(ERROR_LED, 1);
    //while (1) {};
  }
  if (!ads2.begin(0x49)) {
    Serial.println("Failed to initialized ADS2.");
    digitalWrite(ERROR_LED, 1);
    //while (1) {};
  }
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

/*
  */
DateTime inputDateTime() {
  // char datetime[32] = "YYYY/MM/DD hh:mm:ss";
  // rtc_ds3231.now().toString(datetime);

  while (Serial.available() > 0)
    Serial.read();

  Serial.println("Enter date-time as YYYY/MM/dd hh:mm:ss");
  char message[32];
  unsigned int message_pos = 0;

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

/*
  */
void printDateTime(DateTime DT) {
  Serial.print(getTimestamp(DT) + "->");
}

/*
  */
void printDateTime() {
  Serial.println(getTimestamp());
}

/*

  */
void printTime(DateTime DT) {
  char dt[32] = "hh:mm:ss ";
  DT.toString(dt);
  Serial.print(dt);
}

/*

  */
void printTimeSpan(TimeSpan TS) {
  Serial.printf("days:%d - %d:%d:%d ", TS.days(), TS.hours(), TS.minutes(), TS.seconds());
}

/* Wait until the clock rolls over to the next second. */
void waitForNextSecond() {
  DateTime startTime = rtc_ds3231.now();
  while (rtc_ds3231.now() == startTime) {
    delay(1);
  }
}

/* Measure the battery using A0, with 1k/10k voltage divider. Returns in unit Volts.
 */
float measureVoltage() {
  float batteryV = analogRead(A0);
  // 10k/100k voltage divider
  batteryV *= 11;
  // 3.3V analog reference
  batteryV *= 3.3;
  // 10-bit ADC
  batteryV /= 1024;
  return batteryV;
}

/* Reads the thermistors, stores the temps in tempC1 thru tempC8. 
  */
void readThermistor() {
  const float SERIESRESISTOR = 10000.0;
  const float MAX_ADC = 40000;  // Stefan changed this from 19999 to 40000, also changed gain to 2x

  int16_t ADCout1 = ads1.readADC_SingleEnded(0);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout2 = ads1.readADC_SingleEnded(1);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout3 = ads1.readADC_SingleEnded(2);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout4 = ads1.readADC_SingleEnded(3);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout5 = ads2.readADC_SingleEnded(0);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout6 = ads2.readADC_SingleEnded(1);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout7 = ads2.readADC_SingleEnded(2);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3
  int16_t ADCout8 = ads2.readADC_SingleEnded(3);  //A0 input on ADS1115; change to 1=A1, 2=A2, 3=A3

  float ohms1 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout1) - 1);
  float ohms2 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout2) - 1);
  float ohms3 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout3) - 1);
  float ohms4 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout4) - 1);
  float ohms5 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout5) - 1);
  float ohms6 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout6) - 1);
  float ohms7 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout7) - 1);
  float ohms8 = SERIESRESISTOR * ((MAX_ADC / (float)ADCout8) - 1);

  // this function temp(ohms) is valid 0-50 C
  tempC1 = 62.57 - ohms1 * (0.005314) + 0.0000001827 * ohms1 * ohms1 - 0.000000000002448 * ohms1 * ohms1 * ohms1;
  tempC2 = 62.57 - ohms2 * (0.005314) + 0.0000001827 * ohms2 * ohms2 - 0.000000000002448 * ohms2 * ohms2 * ohms2;
  tempC3 = 62.57 - ohms3 * (0.005314) + 0.0000001827 * ohms3 * ohms3 - 0.000000000002448 * ohms3 * ohms3 * ohms3;
  tempC4 = 62.57 - ohms4 * (0.005314) + 0.0000001827 * ohms4 * ohms4 - 0.000000000002448 * ohms4 * ohms4 * ohms4;
  tempC5 = 62.57 - ohms5 * (0.005314) + 0.0000001827 * ohms5 * ohms5 - 0.000000000002448 * ohms5 * ohms5 * ohms5;
  tempC6 = 62.57 - ohms6 * (0.005314) + 0.0000001827 * ohms6 * ohms6 - 0.000000000002448 * ohms6 * ohms6 * ohms6;
  tempC7 = 62.57 - ohms7 * (0.005314) + 0.0000001827 * ohms7 * ohms7 - 0.000000000002448 * ohms7 * ohms7 * ohms7;
  tempC8 = 62.57 - ohms8 * (0.005314) + 0.0000001827 * ohms8 * ohms8 - 0.000000000002448 * ohms8 * ohms8 * ohms8;

  // If the value is way outside what should be possible, report NaN instead of a crazy value
  if (tempC1 > 150 || tempC1 < -60) tempC1 = NAN;
  if (tempC2 > 150 || tempC2 < -60) tempC2 = NAN;
  if (tempC3 > 150 || tempC3 < -60) tempC3 = NAN;
  if (tempC4 > 150 || tempC4 < -60) tempC4 = NAN;
  if (tempC5 > 150 || tempC5 < -60) tempC5 = NAN;
  if (tempC6 > 150 || tempC6 < -60) tempC6 = NAN;
  if (tempC7 > 150 || tempC7 < -60) tempC7 = NAN;
  if (tempC8 > 150 || tempC8 < -60) tempC8 = NAN;
}

/*
  */
void writeSD(HeatingState heatingState) {

  FsFile myFile = SD.open(FILE_NAME, FILE_WRITE);
  String outString(getTimestamp());
  outString += String(", ");
  outString += String(tempC1, 3) + ", ";
  outString += String(tempC2, 3) + ", ";
  outString += String(tempC3, 3) + ", ";
  outString += String(tempC4, 3) + ", ";
  outString += String(tempC5, 3) + ", ";
  outString += String(tempC6, 3) + ", ";
  outString += String(tempC7, 3) + ", ";
  outString += String(tempC8, 3) + ", ";

  switch (heatingState) {
    case HeatingState::PREHEAT:
      {
        outString += String("pre-heat");
        break;
      }
    case HeatingState::HEAT:
      {
        outString += String("heat");
        break;
      }
    case HeatingState::POSTHEAT:
      {
        outString += String("post-heat");
        break;
      }
  }

  outString += ", " + String(batteryLevel, 3);

  myFile.println(outString);
  myFile.close();
  Serial.println(outString);
}

/*

  */
void writeTextSD(String message) {
  //File myFile = SD.open(FILE_NAME, FILE_WRITE);
  // NEW FOR RP2040
  FsFile myFile = SD.open(FILE_NAME, FILE_WRITE);

  String outString = String("M-") + getTimestamp() + "-" + message;
  myFile.println(outString);
  myFile.close();
}

/*
  */
void writeHeaderSD() {
  //File myFile = SD.open(FILE_NAME, FILE_WRITE);
  // NEW FOR RP2040
  FsFile myFile = SD.open(FILE_NAME, FILE_WRITE);
  myFile.printf("\nM- *****************************\nM- Starting Event on Device %s\n", DEVICE_NAME);

  String datetime = getTimestamp();

  myFile.print("M-");
  myFile.print(datetime);
  myFile.printf(" T_MINS=%d PREH_SECS=%d H_SECS=%d POSTH_SECS=%d\n", T_MINS, PREH_SECS, H_SECS, POSTH_SECS);
  // myFile.printf(" T=%d:%d:%d PREH=%d:%d:%d H=%d:%d:%d POSTH=%d:%d:%d \n",
  //               T_HRS, T_MINS, T_SECS, PREH_HRS, PREH_MINS, PREH_SECS,
  //               H_HRS, H_MINS, H_SECS, POSTH_HRS, POSTH_MINS, POSTH_SECS);
  // myFile.print("M-");
  // myFile.print(datetime);
  // myFile.printf(" Sleep Time: %d:%d, Wake Time: %d:%d \n", SLEEP_HRS, SLEEP_MINS, WAKE_HRS, WAKE_MINS);

  // Report the battery level here
  myFile.print("M-");
  myFile.print(datetime);
  myFile.printf(" Battery voltage: %f V\n", batteryLevel);
  Serial.println("Battery voltage: " + String(batteryLevel) + " V");

  myFile.close();
}

/*
  */
void checkForDumpCommand() {
  if (Serial.available() >= 5) {
    char command[5] = {};
    for (int i = 0; i < 5; ++i) {
      int b = Serial.read();
      if (b < 0) command[i] = 0;
      else command[i] = b;
    }
    // Flush any remainder in the buffer
    while (Serial.available()) Serial.read();

    if (command[0] == 'd' && command[1] == 'u' && command[2] == 'm' && command[3] == 'p') {
      // Serial.println("Dump placeholder");
      dumpSdToSerial();
    } else {
      Serial.print("Unknown command: ");
      Serial.println(command);
    }
  }
}

/*
  */
void dumpSdToSerial() {
  //File myFile = SD.open(FILE_NAME, FILE_READ);
  // NEW FOR RP2040
  FsFile myFile = SD.open(FILE_NAME, FILE_READ);

  Serial.print("\n*********************************\nDump to Serial\nmyFile position() = ");
  Serial.println(myFile.position());

  Serial.print("Dumping ");
  Serial.print(myFile.size());
  Serial.println(" bytes\n");

  myFile.seek(0);

  auto startTime = millis();

  while (true) {
    int b = myFile.read();
    if (b < 0) break;
    Serial.write(b);
  }

  auto dumpTime = millis() - startTime;

  Serial.print("\nDump complete. ");
  Serial.print(myFile.size());
  Serial.print(" bytes in ");
  Serial.print(dumpTime / 1000.0);
  Serial.println(" seconds\n***********************************\n");
}

/*
  */
void heaterOn() {
  digitalWrite(HEATER_PIN, HIGH);
}
void heaterOFF() {
  digitalWrite(HEATER_PIN, LOW);
}

// Initialize all LED pins
  // -------------------------------------------------------
void initLEDs() {AA
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
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(HEATER_PIN,OUTPUT);
  digitalWrite(HEATER_PIN, LOW);
}

// Turn all LEDs on/off
void allLEDs(int ONOFF) {
  digitalWrite(RED_LED, ONOFF);
  digitalWrite(YELLOW_LED, ONOFF);
  digitalWrite(GREEN_LED, ONOFF);
  //digitalWrite(POWER_LED, ONOFF); // Always on
  digitalWrite(TIMER_LED, ONOFF);
  digitalWrite(ERROR_LED, ONOFF);
  digitalWrite(LED_BUILTIN, ONOFF);
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
