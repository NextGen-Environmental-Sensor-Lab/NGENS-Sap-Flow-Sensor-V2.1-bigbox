/* SAP FLOW SENSOR 
    Developped and tested on this system: 
    https://github.com/NextGen-Environmental-Sensor-Lab/NGENS_SapFlowSensor
    Based on J.Belity paper 
    https://www.sciencedirect.com/science/article/pii/S2468067222000967#b0080

    NEXTGEN SENSOR LAB, ASRC, CUNY. RTOLEDO-CROW OCT 2023

    V2.1 'BIGBOX'. CODE MODDED FOR RP2040 ADDALOGGER AND SOME IMPROVEMENTS 04.07.2026
*/

// config.h contains all #includes, #defines and configuration.
// Library includes are also listed here to ensure Arduino IDE
// detects and links them correctly.
#include "sfs_config.h"
#include <RTClib.h>
#include <SPI.h>
#include "SdFat.h"
#include <Adafruit_ADS1X15.h>
#include <EEPROM.h>
#include <Wire.h>

// states (periods) in cycle
enum class HeatingState {
  PREHEAT,
  HEAT,
  POSTHEAT,
  END,
};

RTC_DS3231 rtc_ds3231;
Adafruit_ADS1115 ads1, ads2;
SdFat SD;
String deviceID = "UNKNOWN";  // Global device ID
String fileName;
float tempC1, tempC2, tempC3, tempC4, tempC5, tempC6, batteryLevel;
float heaterVoltage, heaterCurrent;
volatile bool provisioningRequested = false;  // Flag set by ISR - must be volatile

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

  Wire.end();
  delay(10);
  Wire.begin();
  delay(10);
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

  fireAlarm2();
  analogReadResolution(12);  // set to 12-bit (0–4095) for RP2040

  fileName = deviceID + ".csv";  // make filename after reading EEPROM
  initializeSD_ADC();            // do this first for writing debugging info to sd
  checkForDumpCommand();         // If the user requested an SD dump, do so

  Serial.print("System date-time: ");
  printDateTime();

  //RTC adjust
  if (rtc_ds3231.lostPower()) {
    Serial.println("RTC lost power - entering provisioning to set time.");
    digitalWrite(TIMER_LED, HIGH);
    provisioningMode();
    digitalWrite(TIMER_LED, LOW);
  }

  #if ENABLE_VOLTAGE_CUTOFF  // this #if needs fixing
  if (batteryLevel < VOLTAGE_CUTOFF) {
    String message;
    message += "Battery pack voltage is ";
    message += String(batteryLevel, 3);
    message += "V which is below the cutoff of ";
    message += String(VOLTAGE_CUTOFF, 3);
    message += "V. The measurement cycle will be skipped.\n";

    Serial.print(message);
    writeTextSD(message);

    // Go to sleep until the next wakeup time
    putToSleep(rtc_ds3231.now() + TimeSpan(0, T_HRS, T_MINS, T_SECS));
  } else
  #endif
  {
    checkProvisioning();
    // Set the alarm before the measurement cycle. Otherwise you need let it
    // run a full measurment cycle while programming or it won't wake up!
    setNextAlarm();

    writeHeaderSD();
    batteryLevel = measureVoltage();
    Serial.printf("Battery before event: %.3f\n", batteryLevel);
    writeTextSD("Battery before event: " + String(batteryLevel, 3) + "\n");

    measurementCycle();

    batteryLevel = measureVoltage();
    writeTextSD("Battery after event: " + String(batteryLevel, 3) + "\n");
    Serial.printf("Battery voltage after event: %.3fV\n", batteryLevel);
  }

  turnOff();
  digitalWrite(GREEN_LED, LOW);
  delay(100);
  Serial.println("If we make it here we are on USB power and will enter loop()");
}

// -----------------------------------------------
//  The loop() is only reached when under USB power!
// -----------------------------------------------
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

  checkForDumpCommand();
  checkProvisioning();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 3000) {
    Serial.print("USB connected -- ");
    printDateTime();
    // printRegisterState();
    lastPrint = millis();
  }
}

void measurementCycle() {
  allLEDs(LOW);

  HeatingState heatingState = HeatingState::PREHEAT;
  // Delay execution until the clock rolls over to the next second to align execution.
  waitForNextSecond();

  const int fullCycleSeconds = PREH_SECS + H_SECS + POSTH_SECS;

  for (int i = 0; i < fullCycleSeconds; ++i) {
    checkProvisioning();
    if (i < PREH_SECS) {
      // Spend this second in preheat
      heatingState = HeatingState::PREHEAT;
      digitalWrite(RED_LED, HIGH);
    } else if (i < PREH_SECS + H_SECS) {
      // Spend this second in heat
      heatingState = HeatingState::HEAT;
      digitalWrite(RED_LED, LOW);
      digitalWrite(YELLOW_LED, HIGH);
    } else {
      // Spend this second in postheat
      heatingState = HeatingState::POSTHEAT;
      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(GREEN_LED, HIGH);
    }

    if (heatingState == HeatingState::HEAT) {
      heaterOn();
    } else {
      heaterOFF();
    }

    readThermistor();
    writeSD(heatingState);

    // Toggle the LED, but don't delay 1 second. Instead poll the RTC time until we reach the next second.
    digitalWrite(TIMER_LED, !digitalRead(TIMER_LED));
    waitForNextSecond();
  }
  digitalWrite(GREEN_LED, LOW);
}
