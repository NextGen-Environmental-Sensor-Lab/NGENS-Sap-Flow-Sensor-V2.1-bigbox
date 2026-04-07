// CODE TO TEST THE SAP FLOW SENSOR TIMING SCHEMA WITH 
// THE DS3231

#include <RTClib.h>
#include <Wire.h>

#define T_MINS 7

// LED pins
#define RED_LED 13
#define YELLOW_LED 12
#define GREEN_LED 11
#define POWER_LED 10
#define TIMER_LED 9
#define ERROR_LED 5

RTC_DS3231 rtc_ds3231;

// Forward declarations
void fireAlarm2();
void setNextAlarm();
void turnOff();
void printDateTime();
void printRegisterState();
void initLEDs();
void allLEDsOff();

void setup() {
  // LEDs first - red on immediately so user knows system is alive
  initLEDs();
  digitalWrite(POWER_LED, HIGH);
  digitalWrite(RED_LED, HIGH);

  Serial.begin(115200);
  delay(800);
  Serial.println(__FILE__);

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

  printDateTime();

  //RTC initialize date/time if needed
  if (rtc_ds3231.lostPower()) {
    digitalWrite(TIMER_LED, HIGH);
    DateTime dt = inputDateTime();
    if (dt.isValid())
      rtc_ds3231.adjust(dt);
    digitalWrite(TIMER_LED, 0);
  }

  setNextAlarm();

  Serial.println("Measurement cycle would run here.");
  delay(5000);  // simulate measurement cycle

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
    printRegisterState();
    lastPrint = millis();
  }
}

// -------------------------------------------------------
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

void allLEDsOff() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(POWER_LED, LOW);
  digitalWrite(TIMER_LED, LOW);
  digitalWrite(ERROR_LED, LOW);
}

void printDateTime() {
  DateTime now = rtc_ds3231.now();
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d> \n",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  Serial.print(buf);
}

// -------------------------------------------------------
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

// -------------------------------------------------------
// Latch power on if woken by button press.
// RED LED on = hold button
// GREEN LED on = latched, safe to release
// -------------------------------------------------------
void fireAlarm2() {
  //Serial.println("fireAlarm2() called.");
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
      Serial.println("TIMEOUT: Button released too early - power latch failed.");
      digitalWrite(RED_LED, LOW);
      digitalWrite(ERROR_LED, HIGH);
      return;
    }
    //digitalWrite(RED_LED, !digitalRead(RED_LED));
    delay(50);
  }

  // Latched! Signal user to release button
  //Serial.println("Alarm1 fired. Power latched. Release the button ok.");
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);  // "safe to release"
}

// -------------------------------------------------------
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

// -------------------------------------------------------
// Power down - clear both alarms to release SQW
// -------------------------------------------------------
void turnOff() {
  // Serial.println("turnOff() called - cutting power.");
  // printRegisterState();
  Serial.println("Both alarms cleared. Power should cut now.\n");
  allLEDsOff();  // all LEDs off before power cuts
  rtc_ds3231.clearAlarm(1);
  rtc_ds3231.clearAlarm(2);
}

/* 
  Enter the current date/time from the serial monitor
*/
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