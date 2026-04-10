#pragma once

#include "sfs_config.h"
#include <RTClib.h>
#include <SPI.h>
#include "SdFat.h"
#include <Adafruit_ADS1X15.h>
#include <EEPROM.h>
#include <Wire.h>

// ---- Timing parameters ----
/* T is the period between measurement events. Ideally, this should evenly
 divide an hour because we set the alarm to the next multiple. If not, e.g. 7, 
 then there will be a short period after the hour with the remainder of 60 mod 7.
 Currently not set up to use HRS or SECS.
*/
#define T_HRS 0
#define T_MINS 3
#define T_SECS 0
#define PREH_SECS 10
#define H_SECS 1
#define POSTH_SECS 10

// #define T_HRS 0
// #define T_MINS 30
// #define T_SECS 0
// /// PREH is the measurement period before heat on
// #define PREH_SECS 20
// /// H is the period to apply heat
// #define H_SECS 2
// /// POTSTH is the measurement period after heat
// #define POSTH_SECS 120

// ......|______|----|_____________|...................... |____|----|_____________|
//         PREH   H       POSTH
//.......|............................T....................|....................
// note: T > PREH + H + POSTH > TS


// ---- Pin assignments ----
#define HEATER_PIN    6
#define RED_LED       13 // PRE HEAT - shares pin with LEDBUILTIN
#define YELLOW_LED    12 // HEAT
#define GREEN_LED     11 // POST HEAT
#define POWER_LED     10
#define TIMER_LED     9
#define ERROR_LED     5
#define SD_CS_PIN     23 // NEW FOR RP2040
#define PROVISION_PIN A1 // Provisioning button

// A battery voltage of 0.9V per cell is a commonly cited cutoff voltage for NiMH cells
#define ENABLE_VOLTAGE_CUTOFF 0
#define VOLTAGE_CUTOFF        (0.9 * 8)

// ---- EEPROM / Provisioning ----
#define EEPROM_SIZE       16
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_ID_ADDR    1
#define EEPROM_MAGIC_VAL  0xA5
#define ID_MAX_LEN        12

