#include <SoftwareSerial.h>
#include <IRremote.h>

#include "utils.h"

SoftwareSerial XBee(2, 4); // RX, TX

// define all the states here
#define IDLE 0
#define INIT 1
#define WAIT 2
#define CONFIRM 3
#define VERIFY 4
#define CONNECTED 5
#define SOFTPOT_THREASHOLD 950
#define SOFTPOT_DELTA_THREASHOLD 111
#define DELAY_IN_WAIT 1000000
unsigned long session_id = 0xA90;

// An IR LED must be connected to Arduino PWM pin 3.
IRsend irsend;
int softpotPin = A0; //analog pin 0
int sendState = 0;
int ledPin = 8;
int state = 0;
int softpotReading = 0;
int softpotInitV = 0;
int delta_threshold = 0;
int changes = 0;
struct XBeePacket XBeePacketArr[20];
volatile int XBeePacketCounter;
volatile int selectedXBee;
volatile int initSelectedXBee;
volatile int previousSelected;

unsigned long start_time;
unsigned long last_release_time;
bool released;
bool pressed;
unsigned long end_time;


int readXBeeString (char *strArray) {
  int i = 0;
  if(!XBee.available()) {
    return -1;
  }
  while (XBee.available()) {
    strArray[i] = XBee.read();
    i++;
  }
  strArray[i] = '\0';
  return i;
}

void setup()
{
  Serial.begin(9600);
  state = IDLE;
  digitalWrite(softpotPin, HIGH); //enable pullup resistor
  randomSeed(analogRead(5));
  digitalWrite(ledPin, LOW);
  XBee.begin(9600);
  XBeePacketCounter = 0;
  pressed = false;
  released = false;
  last_release_time = 0;
}

void loop() {
  switch(state) {
  case IDLE:
    softpotReading = analogRead(softpotPin);
    Serial.print("Softpot Reading: ");
    Serial.println(softpotReading);
    delay(20);
    if (softpotReading < SOFTPOT_THREASHOLD) {
      Serial.println("[INIT] Entering");      
      state = INIT;
      softpotInitV = softpotReading;
      pressed = true;
    }
    selectedXBee = -1;
    break;
  case INIT:
    Serial.println("[INIT] Sending IR");
    session_id = random(0xFFFF);
    // irsend.sendSony(session_id, 16);
    XBee.print(session_id, HEX);  

    // at the same time listening to any response
    state = WAIT;
    Serial.println("[WAIT] entering state");
    XBeePacketCounter = 0;
    start_time = millis();
    break;
  case WAIT:
    // When entering this state, wait for 1 sec to determine how many signals have been received
    end_time = millis();
    // soft timer expires
    if (end_time - start_time > DELAY_IN_WAIT/1000) {
      // check for the number of replies and act accordingly
      Serial.println(XBeePacketCounter);

      if (XBeePacketCounter == 0) {
	Serial.println("No Msg received");
	state = IDLE;
      }
      else if (XBeePacketCounter == 1) {
	selectedXBee = XBeePacketCounter - 1;
      	Serial.print("[CONFIRM] entering with selected: ");
	Serial.print(selectedXBee);
	Serial.print(" deviceId:");
	Serial.println(XBeePacketArr[selectedXBee].id);
	// send out the selected message
	sendXBeePacketFromRaw(&XBee, XBeePacketArr[selectedXBee].id, "c", XBeePacketArr[selectedXBee].data);
	state = CONFIRM;
      }
      else {
	// there are multiple repliers, need to adjust
	// sort the received Id or actually no need if we have TDMA

	// complex part to implement, take a coffee and then start
	selectedXBee = XBeePacketCounter / 2;
	initSelectedXBee = selectedXBee;
	previousSelected = 0;
	released = false;
	state = VERIFY;
      }
    }
    if(XBee.available()) {
      int i = 0;
      // delay for the complete of transmission
      delay(10);
      Serial.println("[WAIT] Reading Packet");
      struct XBeePacket p = readXBeePacket(&XBee);
      printXBeePacket(p);

      // now add this packet to packet array if id is new
      for (i = 0; i < XBeePacketCounter; i++) {
	if ( atoi(p.id) == atoi(XBeePacketArr[i].id) ) {
	  break;
	}
      }
      if (i == XBeePacketCounter) {
	// not found, add to the array
	XBeePacketArr[i] =  p;
	XBeePacketCounter++;
      }
    }
    break;
  case CONFIRM:
    // send out the signal to THE selected XBee
    // WAIT for the confirmation message to arrive
    // now confirm is almost nothing...
    state = CONNECTED;    
    break;
  case VERIFY:
    // when there are multiple targets who have responded
    // one of the LED should be on at this case
    Serial.print("[VERIFY] now trying ");
    Serial.print(selectedXBee);
    Serial.print(", id=");
    Serial.println(XBeePacketArr[selectedXBee].id);
    if (selectedXBee != previousSelected) {
      sendXBeePacketFromRaw(&XBee, XBeePacketArr[selectedXBee].id, "v", XBeePacketArr[selectedXBee].data);
      previousSelected = selectedXBee;
    }

    // configure the slider dynamically    
    delta_threshold = SOFTPOT_DELTA_THREASHOLD;
    changes = 0;
    if (XBeePacketCounter > 5) {
      // this happens really rare
      // you will only have 800 of the whole slider to use
      // then each step is 800/XBeePacketCounter
      delta_threshold = 800/XBeePacketCounter;
    }
    
    softpotReading = analogRead(softpotPin);
    if (pressed == true && softpotReading < SOFTPOT_THREASHOLD){
      // still in hold
      pressed = true;    
    }
     
    // should detect double click here:
    if (pressed == true && softpotReading > SOFTPOT_THREASHOLD) {
      // hand left, expected to have a tap
      last_release_time = millis();
      pressed = false;
      released = true;
      Serial.println("released");
    }
    if (released == true && softpotReading < SOFTPOT_THREASHOLD) {
      // detect if tapped again in a timely fashion
      unsigned long new_release_time = millis();
      if (new_release_time - last_release_time < 400) {
	Serial.println("tap event detected");
	sendXBeePacketFromRaw(&XBee, XBeePacketArr[selectedXBee].id, "c", XBeePacketArr[selectedXBee].data);
	state = CONNECTED;
      }      
    }
    if (released == true && softpotReading > SOFTPOT_THREASHOLD) {
      // timeout happens?      
      unsigned long new_release_time = millis();
      if (new_release_time - last_release_time > 1000) {
	// definitely timeout
	Serial.println("release timeout");
	state = IDLE;
      }      
    }     

	
    if (pressed == true) {      
      changes = (softpotReading - softpotInitV) / delta_threshold;      
      selectedXBee = initSelectedXBee + changes;
      if (selectedXBee < 0) {
	selectedXBee = 0;
      }
      if (selectedXBee >= XBeePacketCounter) {
	selectedXBee = XBeePacketCounter-1;
      }      
    }
    
    break;
  case CONNECTED:

    // temporarily for debugging 
    delay(10000);
    state = IDLE;
    
    // start Gesture recognition and communication

    break;
  default:
    break;
  }
}


