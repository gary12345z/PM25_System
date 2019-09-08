#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include "secret.h"
#ifdef ESP8266
extern "C" {
#include "user_interface.h"
#include "wpa2_enterprise.h"
}
#endif  

#define BUFFER_SIZE 1024
#define GREEN_LED 12
#define RED_LED 13
#define MODE_PIN 14
#define BATTERY_PIN A0
#define RXD1 4
#define TXD1 5
#define RS485_SEND 2
#define UART_PM Serial
SoftwareSerial UART_RS485(RXD1, TXD1);

const unsigned long PMSTABLETIME = 15000;  //ms
const unsigned long MAXTIME = 40000;  //ms
const unsigned long RS485WAITTIME = 5000;  //ms
const unsigned long ERRORTIME = 60000000;  //us
const byte OPEN_CMD[]={0x42,0x4D,0xE4,0x00,0x01,0x01,0x74};
const byte CLOSE_CMD[]={0x42,0x4D,0xE4,0x00,0x00,0x01,0x73};
byte MQTTCONNECT[]={0x10,0x10,0x00,0x04,'M','Q','T','T',0x04,0x02,0x00,0x3C,0x00,0x04,'P','M','0','0'};
byte MQTTSUBSCRIBE_SET[]={0x82,0x14,0x00,0x0A,0x00,0x0F,'N','S','Y','S','U','/','P','M','/','0','0','/','S','E','T',0x01};
byte MQTTUNSUBSCRIBE_SET[]={0xA2,0x13,0x00,0x0A,0x00,0x0F,'N','S','Y','S','U','/','P','M','/','0','0','/','S','E','T'};
byte MQTTSUBSCRIBE_SEND[]={0x82,0x15,0x00,0x0A,0x00,0x10,'N','S','Y','S','U','/','P','M','/','0','0','/','S','E','N','D',0x01};
byte MQTTUNSUBSCRIBE_SEND[]={0xA2,0x14,0x00,0x0A,0x00,0x10,'N','S','Y','S','U','/','P','M','/','0','0','/','S','E','N','D'};
byte MQTTDISCONNECT[]={0xE0,0x00};
char  ESP_SSID[32]  = "ESP_";

bool PMEnable, sendEmpty = false;
int PMState, MQTTState, serverPORT;
uint16_t rawData[15], checkSum;
uint32_t startTime, RS485startTime;
unsigned long sleeptime = 60000000;
char wifiSSID[50], wifiPWD[50], serverIP[20], Mode, ID, PMCounter;
byte wifiBuffer[BUFFER_SIZE], rs485Buffer[BUFFER_SIZE];
WiFiClient wifiClient;
ESP8266WebServer ESPWEB(80);

void SET_setup();
void GetSetting();
void ACTION_setup();

void SET_loop();
void ACTION_loop();
void PMS7003();
void MQTT();
void ErrorSleep();
int GetElectricity();
int MQTT_Receive();
int RS485_Receive();
void MQTT_Send(int len);

//===============setup===============//
void setup() {
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(MODE_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);
  UART_RS485.begin(115200);
  pinMode(RS485_SEND, OUTPUT);
  UART_PM.begin(9600);
  EEPROM.begin(256);
  delay(1000);
  Mode = digitalRead(MODE_PIN);
  digitalWrite(RS485_SEND, LOW);
  if(Mode == HIGH){
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    SET_setup();
  }
  else{
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    ACTION_setup();
  }
}
//===============setup===============//

void SET_setup(){
  ESP_SSID[4] = (EEPROM.read(122)/10) +'0';
  ESP_SSID[5] = (EEPROM.read(122)%10) +'0';
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ESP_SSID);
  ESPWEB.on("/", GetSetting);
  ESPWEB.begin();
}

void ACTION_setup(){
  for(int i=0 ; i<50 ; i++){
    wifiSSID[i] = EEPROM.read(i);
    wifiPWD[i] = EEPROM.read(i+50);
  }
  for(int i=0 ; i<20 ; i++)serverIP[i] = EEPROM.read(i+100);
  serverPORT = EEPROM.read(120)*256 + EEPROM.read(121);
  ID = EEPROM.read(122);
  
  Serial.write(OPEN_CMD,7);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPWD);
  PMEnable = true;
  PMState = 0;
  MQTTState = 0;
  startTime = millis();
}

void GetSetting() {
  int argCount = 0;
  if(ESPWEB.hasArg("SSID")){
    char _SSID[50];
    argCount++;
    ESPWEB.arg("SSID").toCharArray(_SSID,50);
    for(int i=0 ; i<50 ; i++)EEPROM.write(i, _SSID[i]);
  }
  if(ESPWEB.hasArg("PWD")){
    char _PWD[50];
    argCount++;
    ESPWEB.arg("PWD").toCharArray(_PWD,50);
    for(int i=0 ; i<50 ; i++)EEPROM.write(i+50, _PWD[i]);
  }
  if(ESPWEB.hasArg("IP")){
    char _IP[20];
    argCount++;
    ESPWEB.arg("IP").toCharArray(_IP,20);
    for(int i=0 ; i<20 ; i++)
      if(_IP[i]=='_')EEPROM.write(i+100, '.');
      else EEPROM.write(i+100, _IP[i]);
  }
  if(ESPWEB.hasArg("PORT")){
    int _PORT;
    argCount++;
    _PORT = ESPWEB.arg("PORT").toInt();
    EEPROM.write(120, char(_PORT/256));
    EEPROM.write(121, char(_PORT%256));
  }
  if(ESPWEB.hasArg("ID")){
    char _ID;
    argCount++;
    _ID = (char)(ESPWEB.arg("ID").toInt());
    EEPROM.write(122, _ID);
  }
  EEPROM.commit();
  
  if(argCount==5)digitalWrite(GREEN_LED, HIGH);
  String text = "<h1>Battery Power: "+String(GetElectricity())+"% OK</h1>";
  ESPWEB.send(200, "text/html", text);
}

//===============loop===============//
void loop() {
  if(Mode == HIGH) SET_loop();
  else ACTION_loop();
}
//===============loop===============//
void SET_loop(){ ESPWEB.handleClient(); }
void ACTION_loop(){
  PMS7003();
  MQTT();
  if (millis() - startTime > MAXTIME || MQTTState == 15) ErrorSleep();   //系統運作時間過長就重新開機
}
void PMS7003(){
  if(UART_PM.available() > 0){
    byte PMByte = UART_PM.read();
    switch(PMState){
      case 0:
        if(PMByte==0x42 && PMEnable && (millis() - startTime > PMSTABLETIME)) PMState = 1;
        break;
      case 1:
        if(PMByte==0x4d) PMState = 2;
        else PMState = 0;
        PMCounter = 0;
        checkSum = 0x42 + 0x4d;
        break;
      case 2:
        rawData[PMCounter/2] = ((rawData[PMCounter/2]<<8) & 0xFF00) | PMByte;
        checkSum += PMByte;
        if(++PMCounter == 28) PMState = 3;
        break;
      case 3:
        rawData[PMCounter/2] = ((rawData[PMCounter/2]<<8) & 0xFF00) | PMByte;
        if(++PMCounter == 30){
          if(rawData[14] == checkSum){
            PMEnable = false;
            PMState = 4;
            rawData[0] = (uint16_t)GetElectricity();
            UART_PM.write(CLOSE_CMD,7);
          }
          else PMState = 0;
        }
        break;
      case 4:
        break;
    }
  }
}
void MQTT(){
  int len;
  switch(MQTTState){
    case 0: // connect to wifi
      if(wifiClient.connect(serverIP, serverPORT)) MQTTState = 1;
      break;
    case 1: // connect to MQTT broker
      MQTTCONNECT[16] = (ID/10)+'0';
      MQTTCONNECT[17] = (ID%10)+'0';
      wifiClient.write(MQTTCONNECT,18);   //傳送資料
      wifiClient.flush();
      MQTTState = 2;
      break;
    case 2: // connectack from broker
      if(MQTT_Receive()) MQTTState = 3;
      break;
    case 3: // subscribe SEND
      MQTTUNSUBSCRIBE_SEND[15] = (ID/10)+'0';
      MQTTUNSUBSCRIBE_SEND[16] = (ID%10)+'0';
      wifiClient.write(MQTTUNSUBSCRIBE_SEND, 23);
      wifiClient.flush();
      MQTTState =4;
      break;
    case 4: // suback
      if(MQTT_Receive()) MQTTState = 5;
      break;
    case 5: // wait and send SEND data
      len = MQTT_Receive();
      digitalWrite(RS485_SEND, HIGH);
      if(len){
        len-=19;
        if(len > 128)UART_RS485.write(&wifiBuffer[3+18],len);
        else if(len > 1)UART_RS485.write(&wifiBuffer[2+18],len);
        else sendEmpty = true;
        RS485startTime = millis();
        MQTTState = 6;
      }
      else if(PMState == 4){
        MQTTState = 6;
        sendEmpty = true;
      }
      break;
    case 6: // unsubscribe SEND
      digitalWrite(RS485_SEND, LOW);
      MQTTUNSUBSCRIBE_SEND[15] = (ID/10)+'0';
      MQTTUNSUBSCRIBE_SEND[16] = (ID%10)+'0';
      wifiClient.write(MQTTUNSUBSCRIBE_SEND,22);
      wifiClient.flush();
      MQTTState = 7;
      break;
    case 7: // unsuback
      if(MQTT_Receive()) MQTTState = 8;
      break;
    case 8: // subscribe SET
      MQTTSUBSCRIBE_SET[15] = (ID/10)+'0';
      MQTTSUBSCRIBE_SET[16] = (ID%10)+'0';
      wifiClient.write(MQTTSUBSCRIBE_SET,22);
      wifiClient.flush();
      MQTTState = 9;
      break;
    case 9: // suback
      if(MQTT_Receive()) MQTTState = 10;
      break;
    case 10: // publish data
      if(PMState == 4){
      for(int i=0;i<256;i++)wifiBuffer[i] = 0;
      wifiBuffer[0]=0x30; //PUBLISH
      wifiBuffer[1]=0xFD; //Remaining Length
      wifiBuffer[2]=0x01; //Remaining Length
      wifiBuffer[3]=0; //Topic Name Length MSB
      wifiBuffer[4]=16; //Topic Name Length LSB
      sprintf((char*)&wifiBuffer[5],"NSYSU/PM/%02d/DATA",ID); //Topic Name
      sprintf((char*)&wifiBuffer[21],"{\"id\":%d,\"battery\":%d,\"pm1_cf\":%d,\"pm25_cf\":%d,\"pm10_cf\":%d,\"pm1\":%d,\"pm25\":%d,\"pm10\":%d,\"num_03\":%d,\"num_05\":%d,\"num_10\":%d,\"num_25\":%d,\"num_50\":%d,\"num_100\":%d}",
      ID, rawData[0], rawData[1], rawData[2], rawData[3], rawData[4], rawData[5], rawData[6], rawData[7], rawData[8], rawData[9], rawData[10], rawData[11], rawData[12]); //Payload
      wifiClient.write(wifiBuffer,256);   //傳送資料
      wifiClient.flush();
      MQTTState = 11;
      }
      break;
    case 11: // waiting RS485 and send
      len = RS485_Receive();
      if(len >= 0){
        MQTT_Send(len);
        wifiClient.flush();
        MQTTState = 12;
      }
      break;
    case 12: // get SET data
      if(MQTT_Receive()) {
        sleeptime = ((wifiBuffer[19]-'0')*100+(wifiBuffer[20]-'0')*10+wifiBuffer[21]-'0')*1000000;
        MQTTState = 13;
      }
      break; 
    case 13: // clean SEND
      wifiBuffer[0]=0x31; //PUBLISH
      wifiBuffer[1]=18; //Remaining Length
      wifiBuffer[2]=0; //Topic Name Length MSB
      wifiBuffer[3]=16; //Topic Name Length LSB
      sprintf((char*)&wifiBuffer[4],"NSYSU/PM/%02d/SEND",ID); //Topic Name
      wifiClient.write(wifiBuffer,20);   //傳送資料
      wifiClient.flush();
      MQTTState = 14;
      break;
    case 14: // disconnect
      wifiClient.write(MQTTDISCONNECT,2);
      MQTTState = 15;
      break;
    case 15:
      break;
  }
}
void ErrorSleep(){
  Serial.write(CLOSE_CMD,7);
  Serial.flush();
  ESP.deepSleep(sleeptime); //進入deepsleep
}

int GetElectricity(){
  int electricity = (analogRead(BATTERY_PIN)-750)/2.5;
  if(electricity > 100)electricity = 100;
  else if(electricity < 0)electricity = 0;
  return electricity;
}
int MQTT_Receive(){
  static int len=0, mul, counter, bufferCount=0;
  static char MQTTReceiveState=0;
  if(wifiClient.available()){
    byte B = wifiClient.read();
    wifiBuffer[bufferCount++] = B;
    switch(MQTTReceiveState){
      case 0:
        MQTTReceiveState = 1;
        mul = 1;
        counter = 0;
        len = 0;
        break;
      case 1:
        len += (B & 0x7F) * mul;
        mul *= 128;
        if (!(B & 0x80)) MQTTReceiveState = 2;
        break;
      case 2:
        /*UART_RS485.print(counter);
        UART_RS485.print("(");
        UART_RS485.print(len);
        UART_RS485.print(")-");
        UART_RS485.write(B);
        UART_RS485.println("");*/
        if(++counter >= len){
          bufferCount = 0;
          MQTTReceiveState = 0;
          //UART_RS485.println("DONE");
          return len+1;
        }
        break;
    }
  }
  return 0;
}
int RS485_Receive(){
  static int bufferCount=0;
  if(UART_RS485.available()){
    byte B = UART_RS485.read();
    rs485Buffer[bufferCount++] = B;
  }
  else if(millis() - RS485startTime >= RS485WAITTIME) return bufferCount;
  return -1;
}
void MQTT_Send(int len){
  byte digit;
  int x = len+21, counter=0;
  wifiBuffer[counter++]=0x31;
  while (x > 0){
    digit = x % 128;
    x = x / 128;
    if (x > 0) digit = digit | 0x80;
    wifiBuffer[counter++]=digit;
    //UART_RS485.println(digit);
  }
  wifiBuffer[counter++]=0;
  wifiBuffer[counter++]=19;
  sprintf((char*)&wifiBuffer[counter],"NSYSU/PM/%02d/RECEIVE",ID);
  counter+=19;
  strncpy((char*)&wifiBuffer[counter],(char*)rs485Buffer,len);
  wifiClient.write(wifiBuffer, counter+len);
  //UART_RS485.println(counter+len);
}
