#ifndef Lily_H
#define Lily_H

#include <arduino.h>

void initStrip();
long readVcc();
void batteryStatus();
void fastBatteryStatus();
void makeColor(byte redSend, byte greenSend, byte blueSend );
void ledMarquee();

void enterSleep();
void setupSleep();
void stopSleep();
bool ledsAreOff();
void getBatteryColor(byte &batteryRed, byte &batteryGreen, byte &batteryBlue);

void setHSBBytes(int hue, byte sat, byte bri, byte &red, byte &gre, byte &blu);

void startAnimation();
void sleepAnimation();
void wakeAnimation();

bool transmitter();
bool iHavePowerled();
#endif




