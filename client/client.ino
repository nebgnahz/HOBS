/*
  client.ino

  This is a general framework to fit in any clients that can be remotely controlled with XBee. Each clients will add additional function of parsing certain commands, while the major application takes charge of external communication.

  Examples of clients include, for now, lamps and laptops.

  Lamps are simple, dump clients that only support on/off.
  Laptops now are viewed as video players, thus supporting play/pause, volume adjustment, and brightness adjustment, etc.


  Created 07/17/2013
  By benzh@eecs.berkeley.edu

*/

#include <SoftwareSerial.h>
#include <IRremote.h>
#include <string.h>

// #define DEBUG
#define DEBUG_TAG

#include "utils.h"


// In this simplified design, there is no need to save states on client for feedback
// so I have deleted all state variables like IDLE, PENDING, CONNECTED, etc.

SoftwareSerial XBee(3,2); // RX, TX
int ledStatePin = 13;
int ledSignalPin = 10;
int controlledPin = 12;
int ledTargetPin = 11;

int RECV_PIN = 8;
IRrecv irrecv(RECV_PIN);
decode_results results;

char deviceId[3] = "00";
char XBeeInString[50];


boolean blinkShort = false;
unsigned int blinkShort_ratio = 6;
unsigned int ledStateInterval = 200;
unsigned long randomDelay = 0;
unsigned long start_time;
unsigned long end_time;
unsigned long toggle_time;
unsigned long signal_time;
unsigned long signal_threshold = 300;
unsigned long pendingThreshold = 10000;
int random_mutiplier = 15;
int bucket = 0;
#define statusOff 0
#define statusPending 1
#define statusOn 2

const char deviceTV[3] = "11";
const char deviceMusic[3] = "12";
const char deviceLamp[3] = "13";
const char deviceFan[3] = "14";

boolean statePending = false;
boolean signal_response = false;

// EXPERIMENT
const int MODE_IR = 1;
const int MODE_LIST = 2;
int exp_mode = MODE_IR;

void setup()  
{
  Serial.begin(9600);
  pinMode(ledStatePin, OUTPUT);
  pinMode(ledSignalPin, OUTPUT); 
  pinMode(controlledPin, OUTPUT);
  pinMode(ledTargetPin, OUTPUT); 
  Serial.println("system begins!");
  // set the data rate for the SoftwareSerial port
  XBee.begin(9600);
  irrecv.enableIRIn(); // Start the receiver
  randomSeed(analogRead(5));
  readXBeeDeviceId();

  toggle_time = millis(); //for pending led purpose
  signal_time = millis();
  Serial.print("random_mutiplier: ");
  Serial.println(random_mutiplier);
  Serial.println("ready!");
}

void loop()
{


  if(statePending) {

    

    end_time = millis();

    if(end_time - toggle_time > ledStateInterval) {
      // if (blinkShort) {
      //   if (digitalRead(ledStatePin)) {
      //     digitalWrite(ledStatePin, LOW);
      //     bucket = 0;
      //   }
      //   else if (bucket == blinkShort_ratio)
      //     digitalWrite(ledStatePin, HIGH);
      //   else
      //     bucket++;
      // }
      if(!blinkShort) {
        digitalToggle(ledStatePin);
      } else{
        digitalWrite(ledStatePin, HIGH);  
      }
      toggle_time = millis();
    }
  } 

  if(irrecv.decode(&results)) {
    delay(5);
    // DEBUG_TAGGING("IR: ", results.value);
    if(results.value == 0xFFFF){
      digitalWrite(ledSignalPin, HIGH);
      signal_time = millis();
      signal_response = true;
    } else if(results.value <= 0x32){
      // limit the session ID to be a random number between 0~50
      sendBackDeviceID();
      //setting itself to pending state and start blinking slow
      statePending = true;
      blinkShort = true;
      
      
    } else {
      //garbage message
    }
    irrecv.resume();    
  }

  if(signal_response){
    if(millis() - signal_time > signal_threshold){
      signal_response = false;
      digitalWrite(ledSignalPin, LOW);
    }
  }
  
  
  if (XBee.available()) {
    delay(5);
    struct XBeePacket p = readXBeePacket(&XBee);
    printXBeePacket(p);
    DEBUG_TAGGING("id: ", p.id);
    DEBUG_TAGGING(", func: ", p.func);
    DEBUG_TAGGING(", var: ", p.var);
    DEBUG_TAGGING(", data: ", p.data);
    DEBUG_TAGGING("", "\n");

    if ( strcmp(p.id, "FF") == 0 ) {
      // broadcast message
      sendBackDeviceID();
    }
    else if(strcmp(p.var, "MOD") == 0) {
      if(strcmp(p.data, "001") == 0) {
        //EXP IR MODE
        exp_mode = MODE_IR;
      } else if(strcmp(p.data, "002") == 0) {
        //EXP list MODE
        exp_mode = MODE_LIST;
      }
    }
    else if(strcmp(p.var, "SEL") == 0) {
      // if it's selection related, process in this level
      DEBUG_PRINTLN("selection msg received! ");
      
      if(strcmp(p.data, " ON") == 0 || strcmp(p.data, "AON") == 0) {
        //one is selected => turn on led
        //the rest => turn off led
        //" ON" means selected manually by user
        //"AON" means only 1 client responded so auto on
        // if(atoi(p.id) == atoi(deviceId)) {
        if(strcmp(p.id, deviceId) == 0) {
          digitalWrite(ledStatePin, HIGH);  

          //turn off target led if selected correctly
          digitalWrite(ledTargetPin, LOW);  
          replyStatus();
        } else {
          digitalWrite(ledStatePin, LOW);  
        }
        statePending = false;

      } else if(strcmp(p.data, "OFF") == 0 || strcmp(p.data, "CAN") == 0 ) {
        //turn off all the led just in case

        digitalWrite(ledStatePin, LOW);
        statePending = false;

      } else if(strcmp(p.data, "080") == 0 || strcmp(p.data, "1st") == 0) {
        //the one is candidate => blink fast
        //1st means selected by system as default candidate
        //080 means switched by user
        //do the same thing but has different meaning in terms of logging
        DEBUG_TAGGING("my id: ", atoi(deviceId));
        DEBUG_TAGGING(", select id: ", atoi(p.id));
        DEBUG_TAGGING(", equal: ", atoi(deviceId)==atoi(p.id));
        DEBUG_TAGGING("", "");

        if(exp_mode == MODE_IR) {
          if(statePending) {  
            //only change led if it's in pending (is one of the candidates)
            //blink at low frequency
            if(atoi(p.id) == atoi(deviceId)) {
              
              blinkShort = false;
            } else {
              blinkShort = true;  
              digitalWrite(ledStatePin, HIGH);
            }  
          }
        } else {
            //MODE_LIST
            //only hovered is blinking (fast), all the others doesn't blink
            if(atoi(p.id) == atoi(deviceId)) {
              
              statePending = true;
              blinkShort = false;
                
            } else {
              statePending = false;
              blinkShort = true;
              digitalWrite(ledStatePin, LOW);
              //in case it happened to be on at the moment
            }

        }
        
      } else if(strcmp(p.data, "TAR") == 0) {
          if(strcmp(p.id, deviceId) == 0) {
            //turn on target light and return ack
            DEBUG_TAGGING("Target received", "");
            sendXBeePacketFromRaw(&XBee, deviceId, "A", "SEL", "TAR");
            digitalWrite(ledTargetPin, HIGH);
          } else {
            digitalWrite(ledTargetPin, LOW);
          }
        

        

      } 

    }
    else if (atoi(p.id) == atoi(deviceId)) {
      
      // pass this message to the function of client
      if(strcmp(deviceId, deviceTV) == 0
        || strcmp(deviceId, deviceMusic) == 0) {
        laptopBridging(p);
      } else if(strcmp(deviceId, deviceLamp) == 0
        || strcmp(deviceId, deviceFan) == 0) {
        lampClient(p);

      }
    }
  }
}

void sendBackDeviceID() {
  // randomDelay = random(500);
  // avoid conflicts
  
  if(atoi(deviceId) >10) {
    randomDelay = (atoi(deviceId) - 10) * random_mutiplier;
    // if it's 11 - 14 ==> test 2
  } else {
    randomDelay = atoi(deviceId) * random_mutiplier;
  }
  delay(randomDelay);
  DEBUG_TAGGING(randomDelay, " delay, sending back device ID\n" );
  // send back acknowledge packet
  sendXBeePacketFromRaw(&XBee, deviceId, "A", " ID", "XXX");
  
}

void readXBeeDeviceId() {
  delay(500);

  memset(XBeeInString, 0, 50);
  DEBUG_PRINTLN("sending +++");
  XBee.print("+++");
  delay(3000);
  // DEBUG_PRINTLN("reading...");
  readStringfromSerial(&XBee, XBeeInString);
  // DEBUG_PRINTLN(XBeeInString);
  
  delay(1000);
  
  memset(XBeeInString, 0, 50);
  DEBUG_PRINTLN("sending ATMY");
  XBee.print("ATMY\r");
  delay(3000);
  readStringfromSerial(&XBee, XBeeInString);

  // deviceId = XBeeInString[];
  Serial.println(XBeeInString);
  int id = (int)strtol(XBeeInString, NULL, 16);
  // string_copy(deviceId, XBeeInString, 0, 1);

  if(id<10) { //append 0 at beginning
    DEBUG_PRINTLN("id<10");
    deviceId[0] = '0';
    char a[2];
    itoa(id, a, 10);
    deviceId[1] = a[0];
  } else {
    itoa(id, deviceId, 10);  
  }

  Serial.print("my device ID: ");
  Serial.println(deviceId);  
}

void lampClient(struct XBeePacket p) {
  DEBUG_PRINTLN("command issued");
  if (strcmp(p.func, "R") == 0 && strcmp(p.var, "BRI") == 0 ) {
    // read the current status and reply
    int status = digitalRead(controlledPin);
    if (status == 0) {
      sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", "OFF");
    } else {
      sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", " ON");
    }
  }
  else if (strcmp(p.func, "C") == 0 && strcmp(p.var, "BRI") == 0 && strcmp(p.data, " ON") == 0) {
    DEBUG_PRINTLN("turned on");
    digitalWrite(controlledPin, HIGH);
    sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", " ON");
  }
  else if (strcmp(p.func, "C") == 0 && strcmp(p.var, "BRI") == 0 && strcmp(p.data, "OFF") == 0) {
    DEBUG_PRINTLN("turned off");
    digitalWrite(controlledPin, LOW);
    sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", "OFF");
  }
  else {
    // error message
    sendXBeePacketFromRaw(&XBee, deviceId, "E", "XXX", "XXX");
  }
}

void replyStatus() {
  DEBUG_PRINTLN("ask for client status");
  if(strcmp(deviceId, deviceLamp) == 0) {
    int status = digitalRead(controlledPin);
    if (status == 0) {
      sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", "OFF");
    } else {
      sendXBeePacketFromRaw(&XBee, deviceId, "A", "BRI", " ON");
    }
  } else if(strcmp(deviceId, deviceTV) == 0) {
    //send a read status cmd to the laptop and wait for reply
    //curently only volume is needed
    Serial.print(deviceId);
    Serial.println("RVOLXXX");
    delay(500);
    char strArray[20];
    int i = 0;
    // read the serial return value, and return back message
    while (Serial.available()) {
      strArray[i] = Serial.read();
      i++;
    }
    strArray[i] = '\0';
    XBee.println(strArray);
  }
}

void laptopBridging(struct XBeePacket p) {
  // make sure you send back ack
  DEBUG_PRINTLN("command issued");
  char str[20];
  string_concat(str, p.id, 0);
  string_concat(str, p.func, 2);
  string_concat(str, p.var, 3);
  string_concat(str, p.data, 6);
  str[9] = '\0';
  Serial.println(str);


  // char strArray[20];
  // int i = 0;

  // if(p.func[0] == 'R') {
  //   delay(500);
  //   // read the serial return value, and return back message
  //   while (Serial.available()) {
  //     strArray[i] = Serial.read();
  //     i++;
  //   }
  //   strArray[i] = '\0';
  //   XBee.println(strArray);
  // } else {
  //   // sendXBeePacketFromRaw(&XBee, deviceId, "A", p.var, p.data);
  // }   
}
