#include "Lily.h"
#include <avr/sleep.h> // For sleeping.
#include <avr/power.h> // For sleeping.
#include <avr/wdt.h> // For sleeping.
#include <Adafruit_DotStar.h>

//Led stuff
#define NUMPIXELS 3
#define DATAPIN    4
#define CLOCKPIN   3
Adafruit_DotStar strip = Adafruit_DotStar(NUMPIXELS, DATAPIN, CLOCKPIN, DOTSTAR_BGR);






///////////////////////////////////////////Battery Stuff///////////////////////////////////////////

long readVcc() { // read batt level
  long result;    // Todo: does this work together with reading the LDR on A5 or is more setup needed?
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);    // Read 1.1V reference against AVcc
  delay(128); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  result = ADCL;
  result |= ADCH << 8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}



void batteryStatus() {
  byte r, g, b;
  getBatteryColor(r, g, b);
  for (int i = 0; i < 255; i += 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  for (int i = 255; i > 0; i -= 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  makeColor(0, 0, 0);
}




///////////////////////////////////////////SLEEP STUFF///////////////////////////////////////////

void enterSleep(void) {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   /* EDIT: could also use SLEEP_MODE_PWR_DOWN for lowest power consumption. */
  sleep_enable();

  /* Now enter sleep mode. */
  sleep_mode();

  /* The program will continue from here after the WDT timeout*/
  sleep_disable(); /* First thing to do is disable sleep. */

  /* Re-enable the peripherals. */
  power_all_enable();
}



void setupSleep() {
  /* Clear the reset flag. */
  MCUSR &= ~(1 << WDRF);

  /* In order to change WDE or the prescaler, we need to
   * set WDCE (This will allow updates for 4 clock cycles).
   */
  WDTCSR |= (1 << WDCE) | (1 << WDE);

  /* set new watchdog timeout prescaler value */
  WDTCSR = (0 << WDP3)|(1 << WDP2)|(1 << WDP1); /* 1.0 second */

  /* Enable the WD interrupt (note: no reset). */
  WDTCSR |= _BV(WDIE);
}



void stopSleep() {
  /* In order to change WDE or the prescaler, we need to
   * set WDCE (This will allow updates for 4 clock cycles).
   */
  WDTCSR |= (1 << WDCE);
  // Disable the WD interrupt.
  WDTCSR &= ~(_BV(WDIE));
}


////////////////////////////////////////LED stuff////////////////////////////////////////////////////////

void makeColor(byte redSend, byte greenSend, byte blueSend ) {
  for (int i = 0; i < 3; i++ ) {
    strip.setPixelColor(i, strip.Color(redSend, greenSend, blueSend));
  }
  strip.show();
}


void initStrip() {
  strip.begin();
}

bool ledsAreOff() {
  return (!strip.getPixelColor(0));
}


void setHSBBytes(int hue, byte sat, byte bri, byte &red, byte &gre, byte &blu) {
  hue *= 3;
  uint16_t colCalc, redCalc, greCalc, bluCalc, nulCalc, HUE = hue;
  while (hue >= 768) hue -= 768;
  while (HUE >= 256) HUE -= 256;

  if (HUE < 128) {
    colCalc = (127 - HUE) * sat;
    colCalc >>= 8;
  }

  else {
    colCalc = (HUE - 128) * sat;
    colCalc >>= 8;
  }

  nulCalc = 127 * sat;
  nulCalc >>= 8;

  if (hue < 128) {
    redCalc = 128 + colCalc;
    greCalc = 127 - colCalc;
    bluCalc = 127 - nulCalc;
  }
  else if (hue < 256) {
    redCalc = 127 - colCalc;
    greCalc = 128 + colCalc;
    bluCalc = 127 - nulCalc;
  }
  else if (hue < 384) {
    redCalc = 127 - nulCalc;
    greCalc = 128 + colCalc;
    bluCalc = 127 - colCalc;
  }
  else if (hue < 512) {
    redCalc = 127 - nulCalc;
    greCalc = 127 - colCalc;
    bluCalc = 128 + colCalc;
  }
  else if (hue < 640) {
    redCalc = 127 - colCalc;
    greCalc = 127 - nulCalc;
    bluCalc = 128 + colCalc;
  }
  else {
    redCalc = 128 + colCalc;
    greCalc = 127 - nulCalc;
    bluCalc = 127 - colCalc;
  }

  redCalc *= bri;
  greCalc *= bri;
  bluCalc *= bri;

  redCalc >>= 8;
  greCalc >>= 8;
  bluCalc >>= 8;

  red = redCalc;
  gre = greCalc;
  blu = bluCalc;
}







void getBatteryColor(byte &batteryRed, byte &batteryGreen, byte &batteryBlue) {
  int battery = readVcc();
  if (battery < 3200) {
    batteryRed = 255;
    batteryGreen = 0;
    batteryBlue = 0;
  }

  if (battery < 3700) {
    batteryRed = map(battery, 3200, 3700, 255, 0 );
    batteryGreen = map(battery, 3200, 3700, 0, 255 );
    batteryBlue = 0;
    batteryRed = constrain(batteryRed, 0, 255);
    batteryGreen = constrain(batteryGreen, 0, 255);
  }

  if  (battery >= 3700) {
    batteryRed = 0;
    batteryGreen = map(battery, 3700, 4200, 255, 0 );
    batteryBlue = map(battery, 3700, 4200, 0, 255);
    batteryGreen = constrain(batteryGreen, 0, 255);
    batteryBlue = constrain(batteryBlue, 0, 255);
  }

  if  (battery > 4200) {
    batteryRed = 0;
    batteryGreen = 0;
    batteryBlue = 255;
  }
}



void startAnimation() {
  byte r, g, b;
  if (transmitter()) {
    r = 255;
    g = 127;
    b = 0;
  }
  else {
    r = 255;
    g = 0;
    b = 127;
  }
  for (int i = 0; i < 250; i += 10) {
    strip.setPixelColor(1, r * i / 255, g * i / 255, b * i / 255);
    strip.show();
  }
  for (int i = 250; i > 0; i -= 10) {
    strip.setPixelColor(1, r * i / 255, g * i / 255, b * i / 255);
    strip.show();
  }
  makeColor(0, 0, 0);
}



void wakeAnimation() {
  byte r, g, b;
  getBatteryColor(r, g, b);
  for (int i = 0; i < 250; i += 5)  makeColor(i, i, i);
  for (int i = 0; i < 250; i += 5)  makeColor(250 - (250 - r) * i / 250, 250 - (250 - g) * i / 250, 250 - (250 - b) * i / 250);
  for (int i = 250; i > 0; i -= 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  makeColor(0, 0, 0);
}



void sleepAnimation() {
  byte r, g, b;
  getBatteryColor(r, g, b);
  for (int i = 0; i < 250; i += 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  for (int i = 250; i > 0; i -= 5)  makeColor(250 - (250 - r) * i / 250, 250 - (250 - g) * i / 250, 250 - (250 - b) * i / 250);
  for (int i = 250; i > 0; i -= 5)  makeColor(i, i, i);
  makeColor(0, 0, 0);
}





bool transmitter() {
  return 0;
}
