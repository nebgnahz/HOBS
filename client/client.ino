#include <SoftwareSerial.h>
#include <IRremote.h>
#include <string.h>

#define DEBUG
#include "utils.h"

// Trying to elucidate the way communication works first
// current design enforces a simplest design on the receiver side
// no scheduling, purely acknowledge everything it has received with a random delay
#define IDLE 0
#define PENDING 1
#define CONNECTED 2
#define DELAY_IN_WAIT 1000000

SoftwareSerial XBee(2, 3); // RX, TX
int ledStatePin = 13;
int ledSignalPin = 11;
int controlledPin = 12;

int RECV_PIN = 8;
IRrecv irrecv(RECV_PIN);
decode_results results;
int deviceStatus = 0;
int pendingTimer;
char deviceId[3] = "??";
char XBeeInString[50];
int state = IDLE;
unsigned int ledStateInterval = 500;
unsigned long randomDelay = 0;
unsigned long start_time;
unsigned long end_time;
unsigned long toggle_time;
unsigned long signal_time;


#define statusOff 0
#define statusPending 1
#define statusOn 2

// const unsigned long signalInit = 0xA90;
const unsigned long signalInit = 0xC1AA09F6;
const unsigned long signalVerify = 0xAF0;
const unsigned long signalConfirm = 0xA70;


void setup()  
{
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  pinMode(ledStatePin, OUTPUT);
  pinMode(ledSignalPin, OUTPUT); 
  pinMode(controlledPin, OUTPUT); 
  Serial.println("system begins!");
  // set the data rate for the SoftwareSerial port
  XBee.begin(9600);
  irrecv.enableIRIn(); // Start the receiver

  randomSeed(analogRead(5));

  readXBeeDeviceId();
  toggle_time = millis();
  signal_time = millis();

}

void loop()
{

  //testing purpose
  if(Serial.available()) {
    //DEBUG_PRINT(Serial.read());
    //XBee.println("msg from serial");
  }



  if(XBee.available()) {
    digitalWrite(ledSignalPin, HIGH);
    signal_time = millis();
  } else if(millis() - signal_time > 200) {
    digitalWrite(ledSignalPin, LOW);
  }

  switch(state) {
  case IDLE:
    // purely listening and then react by response
    // for now test use random
    // if(irrecv.decode(&results)) {


    digitalWrite(ledStatePin, LOW);

    if(XBee.available()) {

      delay(5);
      char packet[50];
      int len = readXBeeString(packet);

      DEBUG_PRINT("\nPacket received: ");
      DEBUG_PRINT(packet);
      DEBUG_PRINT("  Packet len: ");
      DEBUG_PRINTLN(len);
      if (len > 5)
        break;
      
      randomDelay = random(1000);
      delay(randomDelay);

      // send back acknowledge packet
      struct XBeePacket p;
      string_copy(p.id, deviceId, 0, 1);
      string_copy(p.type, "a", 0, 0);
      string_copy(p.data, packet, 0, 3);
      string_copy(p.cksum, "0", 0, 0);
      printXBeePacket(p);
      sendXBeePacket(&XBee, p);
      start_time = millis();

      DEBUG_PRINTLN("[PENDING] entering PENDING state");
      state = PENDING;
    } else if(irrecv.decode(&results)) {
      delay(5);


      DEBUG_PRINT("\nIR received: ");
      DEBUG_PRINTLN(results.value);

      randomDelay = random(1000);
      delay(randomDelay);

      // send back acknowledge packet
      
      sendXBeePacketFromRaw(&XBee, deviceId, "a", "0000");
      start_time = millis();

      DEBUG_PRINTLN("[PENDING] entering PENDING state");
      state = PENDING;

      irrecv.resume();
    } 

    break;
  case PENDING:
    end_time = millis();

    if(end_time - toggle_time > ledStateInterval) {
      digitalToggle(ledStatePin);
      toggle_time = millis();
    }

    // soft timer expires, this should be long enough, 5 seconds
    if (end_time - start_time > DELAY_IN_WAIT/200) {
      DEBUG_PRINTLN("[PENDING] Timer expires, back to IDLE");
      // timeout, return to IDLE
      state = IDLE;
      irrecv.resume();
    }
    
    if(XBee.available()) {
      start_time = millis();
      // delay for the complete of transmission
      delay(10);
      DEBUG_PRINTLN("[WAIT] Reading Packet");
      struct XBeePacket p = readXBeePacket(&XBee);
      printXBeePacket(p);

      DEBUG_PRINT("ID1:");
      DEBUG_PRINT(atoi(p.id));
      DEBUG_PRINT("  ID2:");
      DEBUG_PRINTLN(atoi(deviceId));
      
      // packet not error
      if (p.type[0] != 'e') {
        start_time = millis();
      }

      if (p.type[0] == 'r') {
        // reset
        DEBUG_PRINTLN("[IDLE] reset to default state");
        state = IDLE;
        irrecv.resume();
      }
      
      if (atoi(p.id) == atoi(deviceId) && p.type[0] == 'c') {
        // have been confirmed
        DEBUG_PRINTLN("[CONNECTED] entering state");
        state = CONNECTED;
      }
      // verifying this selection, speed up led toggling
      else if (atoi(p.id) == atoi(deviceId) && p.type[0] == 'v') {
        // have been confirmed
        DEBUG_PRINTLN("[WAIT] being verified");

        // may flash the light to indicate this
        ledStateInterval = 100;
        state = PENDING;
      }
      // verifying others... still in pending, slower down led
      else if (atoi(p.id) != atoi(deviceId) && p.type[0] == 'v') {
        // have been confirmed
        DEBUG_PRINTLN("[WAIT] verifying others");
        ledStateInterval = 600;
        state = PENDING;
      }
      // connect others, go back to idle
      else if (atoi(p.id) != atoi(deviceId) && p.type[0] == 'c') {
        // have been confirmed
        DEBUG_PRINTLN("[IDLE] connected with others");
        state = IDLE;
        irrecv.resume();
      }
      // actually reset everything else to IDLE for simplicity
      else {
        DEBUG_PRINTLN("[IDLE] id not equal");
        state = IDLE;
        irrecv.resume();
      }
    }
    break;
  case CONNECTED:
    // temporarily for debugging 
    // turn on the ligth to indicate
    digitalWrite(ledStatePin, HIGH);
    
    // listen to commands and take actions

    if(XBee.available()) {
      // delay for the complete of transmission
      delay(10);
      DEBUG_PRINTLN("[WAIT] Reading Packet");
      struct XBeePacket p = readXBeePacket(&XBee);
      printXBeePacket(p);

      // packet not error
      if (p.type[0] != 'e') {
        start_time = millis();
      }
      
      if (atoi(p.id) == atoi(deviceId) && p.type[0] == 'i') {
        // all sorts of instructions
        DEBUG_PRINTLN("[CONNECTED] command issued");
        if (strcmp(p.data, "0001") == 0) {
          digitalWrite(controlledPin, HIGH);
          Serial.print("p");
        }
        else if (strcmp(p.data, "0002") == 0) {
          digitalWrite(controlledPin, LOW);
          Serial.print("p");
        }
        else if (strcmp(p.data, "v_up") == 0) {
          // digitalWrite(controlledPin, LOW);
          Serial.print("u");
        }
        else if (strcmp(p.data, "v_dn") == 0) {
          // digitalWrite(controlledPin, LOW);
          Serial.print("d");
        }
      }
      
      // reset     
      if (p.type[0] == 'r') {
        DEBUG_PRINTLN("[IDLE] reset to default state");
        state = IDLE;
        irrecv.resume();
      }
      if (atoi(p.id) == atoi(deviceId) && p.type[0] == 'd') {
        // disconnected message
        DEBUG_PRINTLN("[IDLE] entering state");
        state = IDLE;
        irrecv.resume();
      }
    }
    break;
  default:
    break;
  }
  
}

void sendMsg(String msg) {
  DEBUG_PRINT(deviceId);
  DEBUG_PRINT(": ");
  DEBUG_PRINT(msg);
  DEBUG_PRINTLN();

  XBee.print(deviceId);
  XBee.print(": ");
  XBee.print(msg);
  XBee.println();

}

int readXBeeString (char *strArray) {
  DEBUG_PRINT("start read XBee: ");
  int i = 0;
  if(!XBee.available()) {
    return -1;
    // delay(100);
  }
  while (XBee.available()) {
    strArray[i] = XBee.read();
    i++;
  }
  strArray[i] = '\0';
  DEBUG_PRINT("read XBee: ");
  DEBUG_PRINTLN(strArray);
  return i;
  
  
  // sendMsg(XBeeInString);
}

void readXBeeDeviceId() {
  delay(1000);

  memset(XBeeInString, 0, 50);
  DEBUG_PRINTLN("sending +++");
  XBee.print("+++");
  delay(3000);
  // DEBUG_PRINTLN("reading...");
  readXBeeString(XBeeInString);
  // DEBUG_PRINTLN(XBeeInString);
  
  delay(1000);
  
  memset(XBeeInString, 0, 50);
  DEBUG_PRINTLN("sending ATMY");
  XBee.print("ATMY\r");
  delay(3000);
  readXBeeString(XBeeInString);

  // deviceId = XBeeInString[];
  int id = atoi(XBeeInString);
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

  Serial.print("my devide ID: ");
  Serial.println(deviceId);  
}
