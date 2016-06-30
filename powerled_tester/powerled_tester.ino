//powerled fader + lipo alarm

int alarmPin = A3;    // select the input pin for the lipo alarm
int alarmValue = 0;  // variable to store the value coming from the alarmPin
int alarmtrigger = 0;

int brightness = 0;    // how bright the LED is
int fadeAmount = 2;    // how many points to fade the LED by
int Powerled = 5;      // select the pin for the PowerLED


void setup() {
  Serial.begin(9600);
  // declare the ledPin as an OUTPUT:
  Serial.println("Oke");
  pinMode(Powerled, OUTPUT);

  digitalWrite(alarmPin, HIGH); // set as internal pullup
}

void loop() {


  while(!BatteryAlarm()){

    analogWrite(Powerled, brightness);    

    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;

    // reverse the direction of the fading at the ends of the fade: 
    if (brightness == 0 || brightness == 255) {
      fadeAmount = -fadeAmount ; 
    }     
  analogWrite(Powerled, 0);   

  }
  
  Serial.println("lowbatt");
}


bool BatteryAlarm () {
  alarmValue = analogRead(alarmPin);  // read the value from the lipo alarmPIN  
  Serial.println(alarmValue);
  //Serial.println(alarmtrigger);
  if (alarmValue < 1000)
  {
    alarmtrigger++;
  }                        
  if (alarmtrigger<100){
    return 0;
  }
  else {
    return 1;
  }

}





