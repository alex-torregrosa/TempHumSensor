/**
   The MySensors Arduino library handles the wireless radio link and protocol
   between your home built sensors/actuators and HA controller of choice.
   The sensors forms a self healing radio network with optional repeaters. Each
   repeater and gateway builds a routing tables in EEPROM which keeps track of
 the network topology allowing messages to be routed to nodes.

   Created by Henrik Ekblad <henrik.ekblad@mysensors.org>
   Copyright (C) 2013-2015 Sensnology AB
   Full contributor list:
 https://github.com/mysensors/Arduino/graphs/contributors

   Documentation: http://www.mysensors.org
   Support Forum: http://forum.mysensors.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   version 2 as published by the Free Software Foundation.

 *******************************
   DESCRIPTION
   Temperature, Humidity, Light level sensor using SI7021
*/

/*
 ***********************************************************************
                       SENSOR CONFIGURATION
 ***********************************************************************
*/

// Enable debug prints to serial monitor
#define MY_DEBUG

// Comment if you are not running on battery/coin cell/... but on main power
#define BATTERY_SENSOR
#define BATTERY_ALERT_LEVEL \
  30  // (%) Will blink red instead of green when sending data if battery is
      // equal or below this level
#define BATTERY_SEND_FREQUENCY \
  5  // Number of data sending before unchanged battery level is resent (If
     // battery percentage changed, it will always be sent)

// Parameters for VCC measurement
// For coin cell, set max to 2900 (after 5% of capacity it will be down to that
// level) and minimum to 2400 (less than 10% left in capactiry, consider it
// dead) For AA/AAA set max to 3000 and min to 2000.
#define BATTERY_VCC_MIN 2400  // Minimum expected Vcc level, in milliVolts.
#define BATTERY_VCC_MAX 2900  // Maximum expected Vcc level, in milliVolts.

// This a a coefficient to fix the imprecision of measurement of the battery
// voltage in the atmega. To have accurate voltage reporting & reliable battery
// level you need to set it this way for EACH node as each atmega as different
// readings : 1) set value to 1000f, connect your node to serial adapter 2) run
// in debug mode and read the voltage reported in serial console 3) measure the
// voltage between Vcc and GND of your serial adapter 3) divide value reported
// by the NModule with voltage measured on your serial adapter, then
// multiplicate by 1000 and put the value below, with at least one decimal (even
// if 0) and a minus f
#define BATTERY_COEF 1005.9f

// This will report battery voltage to the controller, if you want more precise
// measurement
//  In "normal" use of sensor you should keep it commented and use battery
//  percent
//#define SEND_BATTERY_VOLTAGE

// Green and red light. On NModule sensor board don't change the values below
//#define LED_PIN_GREEN 4
//#define LED_PIN_RED 5
// Duration during which the leds are ON for a "blink"
// To find the best value, test your board with blink sketch with A1 and A2 pin
// and find the shortest time you can use without losing too much brightness
#define LED_BLINK_DURATION 8

// Enables/disables sleeps between sendings to optimize for CR2032 or similar
// coin cell, if you are not using AA/AAA battery this is not necessary
//  if you are using alcaline coin cells and not lithium it is also not
//  necessary from my experience!
#define USE_COIN_CELL

// Time between sending of data to the gateway/controller
// If you use both temp/hum and light sensor, this interval will be split in 2
// parts
#define SLEEP_TIME_BETWEEN_DATASEND \
  60000  // (value is in ms so this is one minute)

// Enable and select radio type attached
#define MY_RADIO_RF24

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! \\
// Optimization of MySensors library parameters for battery/coin cell
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! \\

// Disable uplink check during initialization
// This will reduce number of TX/RX during initialization of the sensor which is
// the most painful part for the button cell
//   you can check on your controller log if the sensor messages found their way
//   to the gateway
#define MY_TRANSPORT_UPLINK_CHECK_DISABLED

// Timeout for transport to be ready during initialization, after this duration,
// even if it didn't manage to enable transport the node will enter the loop()
// method 5s is long enough to connect to gateway so you should connect your
// node back to your computer and check the log if your node cannot connect
#define MY_TRANSPORT_WAIT_READY_MS \
  5000  // no more than 5 seconds of waiting for transport to be ready

// Reduce maximum delay before going to sleep, as we do not want to kill the
// battery if gateway is not available Before going to sleep MySensors will make
// sure we don't have a connection problem to MySensors network If this value is
// too high and you have connection problem, MySensors will never go to sleep
// and bye bye battery :(
#define MY_SLEEP_TRANSPORT_RECONNECT_TIMEOUT_MS 2000

// To go extreme, you can hardcode the node id (so your node doesn't have to get
// an idea from gateway)
//  in that case you need to set the parent node id as static and set it. It's
//  set to gateway here (0) but you can put the ID of a repeater If you're
//  unclear about what this means, leave those lines commented If you are using
//  AA/AAA, leave those lines commented
//#define MY_NODE_ID 10  // this needs to be set explicitly
//#define MY_PARENT_NODE_ID 0  // 0 is gateway
//#define MY_PARENT_NODE_IS_STATIC

/*
 ***********************************************************************

  --------- NO CHANGE SHOULD BE NECESSARY BELOW THIS ...  --------------

 ***********************************************************************
*/

#include <MySensors.h>
#include <SPI.h>
#include <Wire.h>

// Necessary to read Vcc (only for battery)
#ifdef BATTERY_SENSOR
#include <SystemStatus.h>  // Get it here:  https://github.com/cano64/ArduinoSystemStatus

SystemStatus vcc();
int LastBatteryPercent =
    200;  // so we are sure to send the battery level at first check
double LastBatteryVoltage = 10;
byte SendsFromLastBattery = BATTERY_SEND_FREQUENCY;

#ifdef SEND_BATTERY_VOLTAGE
#define CHILD_ID_VOLT 254
MyMessage voltMsg(CHILD_ID_VOLT, V_VOLTAGE);
#endif

#endif

// Sleep time between reads (in seconds)
unsigned long SLEEP_TIME = SLEEP_TIME_BETWEEN_DATASEND;
unsigned long SleepTimeSum = SLEEP_TIME_BETWEEN_DATASEND;

#define CHILD_ID_TEMP 1
#define CHILD_ID_HUM 2
#include <SI7021.h>
SI7021 sensor;
float lastTemp = -1;
byte lastHum = -1;

MyMessage tempMsg(CHILD_ID_TEMP, V_TEMP);
MyMessage humMsg(CHILD_ID_HUM, V_HUM);

// Sleep between sendings to preserve coin cell
//  if not using button cell just make sure the #define USE_COIN_CELL is
//  commented at the beginning of the sketch and it will do nothing
void sleepForCoinCell() {
#ifdef USE_COIN_CELL
  sleep(400);
#endif
}

// Called before initialization of the library
void before() {}

void setup() {
  // pinMode(LED_PIN_RED, OUTPUT);
  // digitalWrite(LED_PIN_RED, LOW);
  // pinMode(LED_PIN_GREEN, OUTPUT);
  // digitalWrite(LED_PIN_GREEN, LSLEEP_TIME_BETWEEN_DATASENDOW);

  // Blink red and green to indicate setup
  // BlinkLed(LED_PIN_RED);
  // wait(100);
  // BlinkLed(LED_PIN_GREEN);

  SLEEP_TIME = SLEEP_TIME_BETWEEN_DATASEND;
  if (!sensor.begin()) {
#ifdef MY_DEBUG
    Serial.println("FAILED TO INIT SENSOR !!!!");
#endif
    // 2 red blinks = sensor failed to initialize temp/hum sensor
    // BlinkLedMultiple(LED_PIN_RED, 2);
  }
}

void presentation() {
  sleepForCoinCell();
  // Send the sketch version information to the gateway and Controller
  sendSketchInfo("MiniTempHumSensor", "1.0");
  sleepForCoinCell();

  // Register sensors to gw (they will be created as child devices)
  present(CHILD_ID_TEMP, S_TEMP);
  sleepForCoinCell();
  present(CHILD_ID_HUM, S_HUM);
  sleepForCoinCell();

#ifdef BATTERY_SENSOR
#ifdef SEND_BATTERY_VOLTAGE
  present(CHILD_ID_VOLT, S_MULTIMETER);
#endif
#endif

#ifdef USE_COIN_CELL
  sleep(5000);  // Sleep 5 seconds if using coin cell, let it have a little
                // break :)
#endif
}

float pressure;
float temperature;
float humidity;

bool oneValueSent;  // will tell us if at least one value was sent by the
                    // sensor, if not we will send heartbeat to tell controller
                    // we are still alive

int currentBatteryPercent;
float currentBatteryVoltage;

#define MAX_UNSENT_COUNT 2
byte noSendingCount = 0;

void loop() {
  oneValueSent = false;

#ifdef BATTERY_SENSOR
  currentBatteryPercent =
      map(SystemStatus().getVCC(), BATTERY_VCC_MIN, BATTERY_VCC_MAX, 0, 100);
#ifdef SEND_BATTERY_VOLTAGE
  currentBatteryVoltage = SystemStatus().getVCC();
#endif
#endif

  // if we have reached the configured number of sleeps, we process the other
  // measurements and send values when necessary
  if (SleepTimeSum >= SLEEP_TIME_BETWEEN_DATASEND) {
    SleepTimeSum = 0;

    // Measure temperature and humidity
    temperature = sensor.getCelsiusHundredths();
    temperature = temperature / 100;
    humidity = sensor.getHumidityPercent();

#ifdef MY_DEBUG
    Serial.print("Temperature = ");
    Serial.print(temperature);
    Serial.println(" *C");
    Serial.print("Humidity = ");
    Serial.print(humidity);
    Serial.println("%");
#endif

    if (temperature != lastTemp) {
      oneValueSent = true;
      send(tempMsg.set(temperature, 1));
      sleepForCoinCell();
      lastTemp = temperature;
    }

    if (humidity != lastHum) {
      oneValueSent = true;
      send(humMsg.set(humidity, 1));
      lastHum = humidity;
    }

#ifdef BATTERY_SENSOR
#ifdef SEND_BATTERY_VOLTAGE

    if (currentBatteryVoltage != LastBatteryVoltage) {
      LastBatteryVoltage = currentBatteryVoltage;
      oneValueSent = true;
      currentBatteryVoltage = currentBatteryVoltage / BATTERY_COEF;
#ifdef MY_DEBUG
      Serial.print("Battery voltage (V) = ");
      Serial.println(currentBatteryVoltage);
#endif
      send(voltMsg.set(currentBatteryVoltage, 3));
      sleepForCoinCell();
    }
#endif
    if ((currentBatteryPercent != LastBatteryPercent) ||
        (SendsFromLastBattery >= BATTERY_SEND_FREQUENCY)) {
      LastBatteryPercent = currentBatteryPercent;
      sendBatteryLevel(currentBatteryPercent);
      oneValueSent = true;
      SendsFromLastBattery = 0;
    } else {
      SendsFromLastBattery++;
    }
#endif

    // we blink the led to show user the sensor is still running fine
#ifdef BATTERY_SENSOR
    if (LastBatteryPercent <= BATTERY_ALERT_LEVEL) {
      // BlinkLed(LED_PIN_RED);
    } else {
    }
#endif
    // BlinkLed(LED_PIN_GREEN);
  }

  sleep(SLEEP_TIME);
  SleepTimeSum += SLEEP_TIME;
}

// Make short blink of the led
void BlinkLed(int pin) {
  digitalWrite(pin, HIGH);
  wait(LED_BLINK_DURATION);
  digitalWrite(pin, LOW);
}

// Blinks multiple times
void BlinkLedMultiple(int pin, int count) {
  for (int i = 0; i < count; i++) {
    BlinkLed(pin);
    wait(250);
  }
}
