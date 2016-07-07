/*
  PIXI by Werccollective.com
 programming:
 Olav Huizer: olav@werccollective.com , http://werccollective.com
 David Koster: http://dingenbouwen.blogspot.nl
 Herman Koppingga: herman@kopinga.nl 

 Updates on: https://github.com/Valoa/lily

 Hardware:
 - AVR Atmega328p @1MHz (internal oscillator)
 - nrf24l01 wireless modules
 - tp4056 lipo charger
 - apa102 leds
 - custom PCB

 Based on work by:
 - https://github.com/aaronds/arduino-nrf24l01
 - http://adafruit.com/ (DotStar library)
 - http://donalmorrissey.blogspot.nl/2010/04/sleeping-arduino-part-5-wake-up-via.html  (sleep code)
 - 
 */


#include "Lily.h"
#include <SPI.h>
#include <Mirf.h>
#include <MirfHardwareSpiDriver.h>

#include <Adafruit_DotStar.h>
#include <avr/sleep.h> // For sleeping.
#include <avr/power.h> // For sleeping.
#include <avr/wdt.h> // For sleeping.


//Alarm stuff
int alarmPin = A3;    // select the input pin for the lipo alarm
int alarmValue = 0;  // variable to store the value coming from the alarmPin
int alarmTrigger = 0;
#define TRIGGERPOINT 50


// NRF buffer
#define PAYLOAD_SIZE 12
byte buffer[PAYLOAD_SIZE];

//buffer variables, 0 - 11:
byte packageType = 0;
byte hue = 0;
byte saturation = 0;
byte intensity = 0;
byte colorShift = 0;
byte fadeOut = 0;
int fadeInTime = 0;
int fadeOutTime = 0;
int ledTime = 0;
int delayBeforeTransmit = 0;

bool interruptingBit = 0;
bool powerLedBit = 0;
bool ledBit = 0;

int ignoreChance = 0;

//bit byte places
#define INTERRUPTING 0
#define POWERLED 1

// variables for color shift
byte red = 0;
byte green = 0;
byte blue = 0;


// Hardware configuration
const int ldrPin = A5; //
const int powerLedPin = 5; // pin5 is located underneath the atmega / used to be a piezzo buzzer.


// Sleep counter.
volatile int justSlept = 1;


// Timing variables.
unsigned long fadeStartTime = 0;
unsigned long receivedTime = 0;
unsigned long checkedLDRTime = 0;
unsigned long somethingActive = 0;
unsigned long sentTime = 0;
int transmitDelay = 0;

// Daylight detection averaging variables
const int numLdrReadings = 5;


// the readings from the analog input
int ldrReadings[] = {
  300, 300, 300, 300, 300 // sleep faster
};

int ldrIndex = 0;                  // the index of the current reading
int ldrTotal = 5 * 300; // the running total initialized to start awake.
int ldrAverage = 0;                // the average


// Local fade variables
float fadeIntensity;
float fadeStep;
unsigned long lastFade;
#define FADINGIN 0
#define FADINGOUT 1
#define FRAMERATEDELAY 25 //1000ms/40fps

byte ledIntensity;
byte sentReply = 0;
byte sentMessage = 0;
int forwardTime = 0;


#define TRANSMITPACKETTYPES 4 // amount of different sender packs
byte transmitPacketChances[8] = {127, 127, 127, 127, 127, 127, 127, 127};
byte packetTypeToSend = 0;


int minIdleTime = 5000;
int maxIdleTime = 10000;

bool coffeeBit = 0;

//Debug print stuff
#define PRINTCOLORSTUFF 0
#define PRINTFADESTUFF 0
#define PRINTMESSAGESTUFF 0
#define PRINTPOWERLED 0
#define PRINTLDRSTUFF 0
#define PRINTPACKETPICKSTUFF 0

//fadeState stuff
#define waitingForPackage 0
#define readyToFade 1
#define fadingIn 2
#define inPlateu 3
#define fadingOut 4

byte fadeState = waitingForPackage;


//packetType stuff
#define SHOWBATTERY 1
#define STARTFADE 2
#define SHOWALARM 3
#define STARTBATTERYFADE 4
#define CHANGECHANCES 5
#define CHANGEIDLETIMES 6
#define COFFEEPACKET 7
#define ALWAYSPOWERLED 8

void setup(void) {
  TCCR0B = (TCCR0B & 0b11111000) | 1;
  Serial.begin(9600);
  Serial.println("Lily started");
  config_NRF();

  pinMode(powerLedPin, OUTPUT);
  digitalWrite(powerLedPin, LOW);
  pinMode(ldrPin, INPUT);
  digitalWrite(alarmPin, 1);

  // Let the user know we are started by doing a led marquee.
  initStrip();
  startAnimation();
  randomSeed(analogRead(ldrPin)*analogRead(ldrPin)*analogRead(ldrPin));
}





void loop(void) {
  transmitIdleTimeChange();
  transmitChanceChange();
  if (actualMillis() - checkedLDRTime >= 1000) {
    checkedLDRTime = actualMillis();
    doSleepStuff();
  }

  if (transmitter()) {
    if (actualMillis() - somethingActive >= transmitDelay) {
      transmitDelay = random(minIdleTime, maxIdleTime);
      pickPackageTypeToSend();
      if (packetTypeToSend == 0)sendRandomFadePackage();
      if (packetTypeToSend == 1) sendBurstPackage();
      if (packetTypeToSend == 2) sendFastBlinkyWhite();
      if (packetTypeToSend == 3) sendSlowFullFade();
      //    if (packetTypeToSend == 4) sendRandomFadePackage();
      //    if (packetTypeToSend == 5) sendRandomFadePackage();
      //    if (packetTypeToSend == 6) sendRandomFadePackage();
      //    if (packetTypeToSend == 7) sendRandomFadePackage();
    }
  }

  if (Mirf.dataReady()) {
    getMessage();

    if (buffer[0] == SHOWBATTERY && buffer[1]) {
      buffer[1]--;
      Mirf.send((byte *)&buffer);
      while (Mirf.isSending());  //Forward right away
      batteryStatus();
      while (Mirf.dataReady()) getMessage();
    }
    if (buffer[0] == SHOWALARM && buffer[1]) {
      buffer[1]--;
      Mirf.send((byte *)&buffer);
      while (Mirf.isSending());  //Forward right away
      alarmStatus();
      while (Mirf.dataReady()) getMessage();
    }

    if (buffer[0] == STARTFADE) {
      if (random(0, 255) < buffer[11]); //Do nothing, discard packet
      else if (fadeState == waitingForPackage || bitRead(buffer[10], 0)) {
        decodeMessage();
        interruptingBit = 0;
        fadeState = readyToFade;
        receivedTime = actualMillis();
        fadeStartTime = actualMillis();
        sentMessage = 0;
      }
    }
    if (buffer[0] == CHANGECHANCES) {
      Serial.println("Changes changed!");
      for (int i = 0; i < 8; i++) {
        transmitPacketChances[i] = buffer[i + 1];
        Serial.print("Chance "); Serial.print(i); Serial.print(" = "); Serial.println(transmitPacketChances[i]);
      }
      startAnimation();
    }
    if (buffer[0] == CHANGEIDLETIMES) {
      Serial.println("Idle times changed!");
      minIdleTime = buffer[1] * 1000;
      maxIdleTime = buffer[2] * 1000;
      Serial.print("minIdleTime = "); Serial.println(minIdleTime);
      Serial.print("maxIdleTime = "); Serial.println(maxIdleTime);
      startAnimation();
    }
    if (buffer[0] == COFFEEPACKET) {
      Serial.println("Illy beans!");
      coffeeBit = buffer[1];
      Serial.print("Coffeestatus is now: "); Serial.println(coffeeBit);
      startAnimation();
    }
  }

  if (sentMessage == 0 && actualMillis() - receivedTime >= delayBeforeTransmit && intensity > fadeOut) {
    sendMessage();
    sentMessage = 1;
  }

  updateFade();
}




void transmitChanceChange() { // send changes to masters
  buffer[0] = CHANGECHANCES;
  buffer[1] = 0; //Chances in x/255 //random fade pack
  buffer[2] = 0;        //Burst pack
  buffer[3] = 255;        // fast blinky
  buffer[4] = 0;        // slow full fade
  buffer[5] = 0;        // chase1
  buffer[6] = 0;        // chase3
  buffer[7] = 0;        //feedback fade
  buffer[8] = 0;        // lake glider  
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());
}



void transmitIdleTimeChange() {
  buffer[0] = CHANGEIDLETIMES;
  buffer[1] = 1;   //Idle time MIN in seconds
  buffer[2] = 1;  //Idle time MAX in seconds
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());
}



void giveOrTakeCoffee() {
  buffer[0] = COFFEEPACKET;
  buffer[1] = 0; //0 = noCoffee;
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());
}



void transmitBatteryPacket() {
  buffer[0] = SHOWBATTERY;
  buffer[1] = 10; //Forward max 20 times
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());
}

void transmitAlarmPacket() {
  buffer[0] = SHOWALARM;
  buffer[1] = 10; //Forward max 20 times
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());
}


void doSleepStuff() {
  while (dayLight() && !coffeeBit && actualMillis() >= 10000) {
    if (!justSlept) {
      Mirf.powerDown();
      sleepAnimation();
    }
    setupSleep();
    enterSleep();
  }
  if (justSlept) {
    stopSleep();
    Mirf.config();
    justSlept = 0;
    wakeAnimation();
  }
}



ISR(WDT_vect) {
  justSlept = 1; // Semi sleep time counter in seconds.
}





void pickPackageTypeToSend() {
  int chancesTotal = 0;
  packetTypeToSend = 255;
  for (int i = 0; i < TRANSMITPACKETTYPES; i++) {
    chancesTotal += transmitPacketChances[i];
    if (PRINTPACKETPICKSTUFF) {
      Serial.print("Chance ");
      Serial.print(i);
      Serial.print(" = ");
      Serial.println(transmitPacketChances[i]);
    }
  }

  if (PRINTPACKETPICKSTUFF) {
    Serial.print("Chancestotal = ");
    Serial.println(chancesTotal);
  }

  int packedTypeToSendPicker = random(0, chancesTotal);

  if (PRINTPACKETPICKSTUFF) {
    Serial.print("packedTypeToSendPicker = ");
    Serial.println(packedTypeToSendPicker);
  }
  for (int i = 0; i < TRANSMITPACKETTYPES; i++) {
    if (packedTypeToSendPicker < transmitPacketChances[i]) {
      packetTypeToSend = i;
      break;
    }
    else {
      chancesTotal -= transmitPacketChances[i];
      packedTypeToSendPicker -= transmitPacketChances[i];
    }
  }

  if (PRINTPACKETPICKSTUFF) {
    Serial.print("packetTypeToSend = ");
    Serial.println(packetTypeToSend);
  }
}





///////////////////////////////////////////packets///////////////////////////////////////////


void sendSlowFullFade() {
  //Packet settings
  packageType = STARTFADE;
  powerLedBit = 1;
  ledBit = 1;
  interruptingBit = 0;
  ignoreChance = 63; //package drop

  //Colour settings
  hue = 0;//random(0, 256);
  colorShift = 3;
  saturation = random(127, 255);

  //Repeat time settings
  intensity = 255;
  int timesToRepeat = 45;
  fadeOut = intensity / timesToRepeat;

  //Fade time settings
  int meanDif = 100;
  int mean = 1500;// fade time
  ledTime = mean + random(0, meanDif);
  fadeInTime = mean + random(0, meanDif);
  fadeOutTime = mean + random(0, meanDif);

  //Transmit time settings
  delayBeforeTransmit = (fadeInTime + ledTime + fadeOutTime) * 0 / 1023;//send pos

  fadeState = readyToFade;
  fadeStartTime = actualMillis();
  sentMessage = 0;
}



void sendRandomFadePackage() {
  //Packet settings
  packageType = STARTFADE;
  powerLedBit = 1;
  ledBit = 1;
  interruptingBit = 0;
  ignoreChance = 0;

  //Colour settings
  hue = 85;//random(0, 256);
  colorShift = 2;
  saturation = random(127, 255);

  //Repeat time settings
  intensity = 255;
  fadeOut = 25;

  //Fade time settings
  int meanMin = 100;
  int meanMax = 250;
  int meanDif = 100;
  int mean = random(meanMin, meanMax);
  ledTime = mean + random(0, meanDif);
  fadeInTime = mean + random(0, meanDif);
  fadeOutTime = mean + random(0, meanDif);

  //Transmit time settings
  delayBeforeTransmit = random(0, fadeInTime + ledTime);// + ledTime + fadeOutTime

  fadeState = readyToFade;
  fadeStartTime = actualMillis();
  sentMessage = 0;
}



void sendBurstPackage() {
  //Packet settings
  packageType = STARTFADE;
  powerLedBit = 1;
  ledBit = 1;
  interruptingBit = 0;
  ignoreChance = 0;

  //Colour settings
  hue = 170;//random(0, 256);
  colorShift = 2;
  saturation = random(127, 255);

  //Repeat time settings
  intensity = 250;
  fadeOut = 25;

  //Fade time settings
  int meanMin = 500;
  int meanMax = 1000;
  int meanDif = 500;
  int mean = random(meanMin, meanMax);
  ledTime = mean + random(0, meanDif);
  fadeInTime = mean + random(0, meanDif);
  fadeOutTime = mean + random(0, meanDif);

  //Transmit time settings
  delayBeforeTransmit = 0;// + ledTime + fadeOutTime

  fadeState = readyToFade;
  fadeStartTime = actualMillis();
  sentMessage = 0;
}



void sendFastBlinkyWhite() {
  //Packet settings
  packageType = STARTFADE;
  powerLedBit = 1;
  ledBit = 1;
  interruptingBit = 0;
  ignoreChance = 130; // package drop

  //Colour settings
  hue = 0;//random(0, 256);
  colorShift = 3;
  saturation = 0;

  //Repeat time settings
  intensity = 255;
  int timesToRepeat = 20;//repeat anount
  fadeOut = intensity / timesToRepeat;

  //Fade time settings
  ledTime = 50;//mean + random(0, meanDif);
  fadeInTime = 0;//mean + random(0, meanDif);
  fadeOutTime = 0;// mean + random(0, meanDif);

  //Transmit time settings
  delayBeforeTransmit = 50; //DO NOT MAKE THIS LOWER THAN THE FADE TIME. DISRUPTIVE FLOCKS WILL REVOLT.

  fadeState = readyToFade;
  fadeStartTime = actualMillis();
  sentMessage = 0;
}

///////////////////////////////////////////packet///////////////////////////////////////////





void sendMessage() {
  // Prepare buffer for transmit.
  buffer[0] = packageType;
  buffer[1] = hue;
  buffer[2] = saturation;   // Loses precision, no problem :)
  buffer[3] = intensity;
  buffer[4] = colorShift;  // Loses precision, no problem :)
  buffer[5] = fadeOut;
  buffer[6] = fadeInTime / 10;
  buffer[7] = fadeOutTime / 10;
  buffer[8] = ledTime / 10;
  buffer[9] = delayBeforeTransmit / 10;
  buffer[10] = 0;
  bitWrite(buffer[10], 0, interruptingBit);
  bitWrite(buffer[10], 1, powerLedBit);
  bitWrite(buffer[10], 2, ledBit);

  buffer[11] = ignoreChance;
  //Send message.
  Mirf.send((byte *)&buffer);
  while (Mirf.isSending());

  if (PRINTMESSAGESTUFF) Serial.println("Sent response.");
  if (PRINTMESSAGESTUFF) simplePrintBuffer();
  somethingActive = actualMillis();
}



void getMessage() {
  Mirf.getData(buffer);
  if (PRINTMESSAGESTUFF) Serial.println("Got payload");
  if (PRINTMESSAGESTUFF) simplePrintBuffer();
  somethingActive = actualMillis();
}



void decodeMessage() {  // Convert the buffer to usable variables.
  packageType = buffer[0];
  hue = buffer[1];
  saturation = buffer[2];
  intensity = buffer[3];
  colorShift = buffer[4];
  fadeOut = buffer[5];
  fadeInTime = buffer[6] * 10;
  fadeOutTime = buffer[7] * 10;
  ledTime = buffer[8] * 10;
  delayBeforeTransmit = buffer[9] * 10;
  interruptingBit = bitRead(buffer[10], 0);
  powerLedBit = bitRead(buffer[10], 1);
  ledBit = bitRead(buffer[10], 2);
  ignoreChance = buffer[11];
}





void startFadeIn() {
  // calculate intensitystep
  ledIntensity = intensity;
  setColor();
  fadeStep = (float)(ledIntensity * FRAMERATEDELAY) / (float)fadeInTime; //Not calculated right, but works because of constraints later.
  fadeIntensity = 0;
  fadeState = fadingIn;
  if (PRINTFADESTUFF) {
    Serial.print("fadeInTime: ");  Serial.println(fadeInTime);
    Serial.print("ledTime: ");     Serial.println(ledTime);
    Serial.print("fadeOutTime: "); Serial.println(fadeOutTime);
    Serial.println("Fading in");
  }
}





void startFadeOut() {
  // calculate intensitystep
  fadeStep = (float)(ledIntensity * FRAMERATEDELAY) / (float)fadeOutTime; //Not calculated right, but works because of constraints later.
  fadeIntensity = ledIntensity;
  fadeState = fadingOut;
  if (PRINTFADESTUFF) Serial.println("Fading out");
}





void updateFade() {
  if (actualMillis() - lastFade >= FRAMERATEDELAY) {
    lastFade = actualMillis();
    checkBatteryAlarm();
    if (fadeState != waitingForPackage) {
      somethingActive = actualMillis();
    }

    if (fadeState == readyToFade) {  //Just received a package, fading in now.
      startFadeIn();
    }

    if (fadeState == fadingIn) {
      fadeIntensity += fadeStep;
      if (PRINTFADESTUFF) Serial.println(fadeIntensity);
      if (fadeIntensity > ledIntensity) {
        fadeIntensity = ledIntensity;
        if (PRINTFADESTUFF) Serial.println("In plateu");
        fadeState = inPlateu;
      }
    }

    if (actualMillis() - fadeStartTime >= fadeInTime + ledTime && fadeState == inPlateu) { //Done with 'plateu' time. Starting fadeout
      startFadeOut();
    }

    if (fadeState == fadingOut) {
      fadeIntensity -= fadeStep;
      if (PRINTFADESTUFF) Serial.println(fadeIntensity);
      if (fadeIntensity < 0) {
        fadeIntensity = 0;
        fadeState = waitingForPackage;
        if (PRINTFADESTUFF) Serial.println("Done fading");
      }
    }

    if (powerLedBit) {
      if (!checkBatteryAlarm()) analogWrite(powerLedPin, fadeIntensity);
    }
    else {
      digitalWrite(powerLedPin, 0);
    }

    if (ledBit) { //normal fade
      makeColor(red * fadeIntensity / 255, green * fadeIntensity / 255, blue * fadeIntensity / 255);
    }
    else {
      makeColor(0, 0, 0);
    }
  }
}





void setColor() {
  hue = hue + colorShift;
  if (hue >= 256) hue -= 256;

  intensity = intensity - fadeOut;  // Fadeout between pixies.
  setHSBBytes(hue, saturation, intensity, red, green, blue);

  //  if (strobe)  {
  //    lastStrobe = actualMillis();
  //    if (actualMillis() - lastStrobe >= 30){
  //      intensity = 255-intensity;
  //    }
  //  }

  if (PRINTCOLORSTUFF) {
    Serial.print("red: ");         Serial.println(red);
    Serial.print("green: ");       Serial.println(green);
    Serial.print("bleu: ");        Serial.println(blue);
    Serial.print("hue: ");         Serial.println(hue);
    Serial.print("saturation: ");  Serial.println(saturation);
    Serial.print("intensity: ");   Serial.println(intensity);
  }
}





bool dayLight() {
  if (ledsAreOff()) {
    ldrTotal = ldrTotal - ldrReadings[ldrIndex];  // subtract the last reading:
    ldrReadings[ldrIndex] = analogRead(ldrPin);  // read from the sensor:
    ldrTotal = ldrTotal + ldrReadings[ldrIndex];  // add the reading to the total
    ldrIndex = ldrIndex + 1;  // advance to the next position in the array:
    if (ldrIndex >= numLdrReadings) {  // if we're at the end of the array...
      ldrIndex = 0;    // ...wrap around to the beginning:
    }
    int ldrLevel = ldrTotal / numLdrReadings;  // calculate the average:
    if (PRINTLDRSTUFF) Serial.println(ldrLevel);
    return (ldrLevel < 300);
  }
}





void simplePrintBuffer() {
  for (int i = 0; i < PAYLOAD_SIZE; i++)  {
    Serial.print(i);
    Serial.print(": ");
    Serial.println(buffer[i]);
  }
}




void config_NRF() {
  Mirf.cePin = 9;
  Mirf.csnPin = 10;

  Mirf.spi = &MirfHardwareSpi;
  Mirf.init();

  Mirf.setRADDR((byte *)"LILY0");
  Mirf.setTADDR((byte *)"LILY0");

  Mirf.payload = PAYLOAD_SIZE;
  Mirf.channel = 110;
  Mirf.config();
}





bool checkBatteryAlarm() {
  alarmValue = analogRead(alarmPin);  // read the value from the lipo alarmPIN
  if (alarmValue < 900 && alarmTrigger <= TRIGGERPOINT)  {
    alarmTrigger++;
  }

  if (PRINTPOWERLED) {
    Serial.print("alarmvalue: ");  Serial.println(alarmValue);
    Serial.print("alarmtrigger: "); Serial.print(alarmTrigger);
  }
  if (alarmTrigger >= TRIGGERPOINT) {
    digitalWrite(powerLedPin, 0);
    return 1;
  }
  else return 0;
}




void alarmStatus() {
  byte r, g, b;
  r = checkBatteryAlarm() * 255;
  g = 255 - checkBatteryAlarm() * 255;
  b = 0;
  for (int i = 0; i < 255; i += 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  for (int i = 255; i > 0; i -= 5)  makeColor(r * i / 255, g * i / 255, b * i / 255);
  makeColor(0, 0, 0);
}



unsigned long actualMillis() {
  return millis() / 64;
}
