#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define AP_SSID "SSID_OF_ACCESSPOINT"  //SSID of accesspoint
#define AP_PASSPHRASE "BetterUseWPA2"  //Passphrase/Password of accesspoint
#define SERVER_IP "184.106.153.149" //IP of Server, f.e. thingspeak.com (only IP adresses)
#define SERVER_PORT "80"  //Port of Server, f.e. 80 for thingspeak.com
String GET = "GET /update?key=YOURKEY"; //Put your thingspeak key after =
#define ONE_WIRE_BUS 8 //connect the DS18B20 temperature sensors

SoftwareSerial monitor(2, 3); //RX, TX ports for debugging purposes
//NOTE: you shouldn't change Serial and monitor, as the ESP8266 only works
//smooth with the main Serial port (I really don't know why)

//setup the dallas libs
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

//set addresses (the temperature of every address (thermometer) will be uploaded to a seperate field on the thingspeak server, accordingly to its position in the array
DeviceAddress myAddresses[] = {
{ 0x28, 0xB7, 0x38, 0xEB, 0x5, 0x0, 0x0, 0xCE }, // address of DS18B20 device 1 - f.e. 28B738EB050000CE
{ 0x28, 0x64, 0x73, 0xEB, 0x5, 0x0, 0x0, 0xCA } // address of DS18B20 device 2 - f.e. 286473EB050000CA
}; // IMPORTANT!!! To get the length of the array, use sizeof(myAddresses) and divide it by 8!!! sizeof(myAddresses)/8

// set some variables
boolean moduleOk = 0; // my ESP8266 modules crash sometimes. If they do, the moduleOk var is turned to 0
boolean wifiReady = 0; // 1 if module is ready (AT+RST)
boolean wifiConnected = 0; // 1 if the wifi is connected to the AP - not very useful in this code but will be used in following projects for receiving data

byte pinStatus = 13; // is turned on if the module is connected (wifiConnected = 1)
unsigned long updateInterval = 60000;  // update intervall for temperature to thingspeak.com in milliseconds
unsigned long time = 0; // holds the time of the last update

// SETUP
void setup() {
    pinMode(pinStatus, OUTPUT);
    digitalWrite(pinStatus, LOW);
    sensors.begin(); // initialize the dallas lib
    
    monitor.begin(9600); // start debug monitor
    Serial.begin(9600); // you need the updated firmware for the ESP8266 and set its baude rate to 9600. Modules purchased after Oktober 2014 should already have the updated firmware installed and the baude rate set
    monitor.println("Initialize");
    
    moduleInit(); // start up the ESP8266 and connect it to the AP
}


// MAIN LOOP
void loop() {
  // check if the module has crashed
  if(!moduleOk){
    moduleInit(); // if crashed, reboot and reconnect (very harsh mode. If you know a better method, please let me know!)
  }
   
  if ( (unsigned long)(millis() - time) >= updateInterval ) { // use it like this to compensate the millis() reset after 50 days. This way it will continue to work.
    sensors.requestTemperatures();
    String tempstring=""; // build the "&fieldX=tempC" string with the temperatures
    for(int i=0; i<sizeof(myAddresses)/8; i++){
      float tempC = sensors.getTempC(myAddresses[i]);
      char buffer[20];
      String tempCString = dtostrf(tempC, 0, 3, buffer); // convert the tempC float to string
      tempstring += "&field";
      tempstring += i+1;
      tempstring += "=";
      tempstring += tempCString;
      monitor.print(getTimeStamp(millis()));
      monitor.print(" Current temperature ");
      monitor.print(i+1); // device number, counting from 1
      monitor.print(" in C: ");
      monitor.println(tempCString);
    }
    time = updateTemp(tempstring);
  }
}



// Reset the ESP8266 and return TRUE if module is ready
boolean wifiReset(){
  Serial.setTimeout(5000); // sets the maximum milliseconds to wait for serial data when using Serial.readBytesUntil() or Serial.readBytes()
  dbgPrint("AT+RST\r\n"); // Reset the ESP8266
  delay(2000);
  if(Serial.find("ready")){
    moduleOk = 1;
    return 1; // ESP8266 ready
  }else{
    return 0; // ESP8266 not ready
  }
}

// Connect the ESP8266 to your access point and return TRUE if connection is established
boolean wifiConnect(){
  dbgPrint("AT+CWMODE=1\r\n"); // set wifi mode to (Sta (1), AP (2), both (3))
  delay(1000);
  // Connect to AP with given password
  if( Serial.find("OK") || Serial.find("no change") ){
    String tmp = "AT+CWJAP=\"";
    tmp += AP_SSID;
    tmp += "\",\"";
    tmp += AP_PASSPHRASE;
    tmp += "\"\r\n";
    dbgPrint(tmp);
    delay(2000);
  }
  // is the connection established?
  if(Serial.find("OK")){
    digitalWrite(pinStatus, HIGH);
    return 1;
  } else {
    digitalWrite(pinStatus, LOW);
    dbgPrint("AT+CWQAP\r\n"); // make sure to quit the access point
    delay(2000);
  }
  return 0;
}

// initialize module
void moduleInit() {
  wifiReady = 0;
  wifiConnected = 0;
  moduleOk = 0;
  digitalWrite(pinStatus, LOW);
  while (!wifiReady) {
    wifiReady = wifiReset(); // reset the module or initialize it with the AT+RST command
    delay(1000);
  }
  
  // connect ESP8266 to access point
  while (!wifiConnected) {
      digitalWrite(pinStatus, LOW);
      wifiConnected = wifiConnect(); // connect WiFi. If connection is established turn ledStatus on
      delay(2000);
  }
  
  // set multiple connections
  dbgPrint("AT+CIPMUX=1\r\n");
  delay(1000);
  
}

// post the temperature to the thingspeak server
unsigned long updateTemp(String temp) {
  String cmd = ""; // build the string for the AT command to connect the ESP8266 to thingspeak
  cmd += "AT+CIPSTART=4,\"TCP\",\""; // start connection id4 (from 0-4. you can have up to 5 connections open at a time with the ESP8266)
  cmd += SERVER_IP;
  cmd += "\",";
  cmd += SERVER_PORT;
  cmd += "\r\n";
  dbgPrint(cmd);
  delay(2000);
  if(Serial.find("Error")){
    monitor.print(getTimeStamp(millis()));
    monitor.print(" RECEIVED: Error");
    moduleOk = 0; // will reset the module at the beginning of the next loop. Very harsh and probably unnecessary at this time but also reliable.
    return millis();
  }
  cmd = ""; // build the string for the GET method. I flush it first, because otherwise there is a minimal chance of failure (had it twice before)
  cmd += GET;
  delay(100); // I use here a small delay, because see above
  cmd += temp;
  cmd += "\r\n";
  String cmd1 = ""; // build the string for the update
  cmd1 += "AT+CIPSEND=4,"; // use data port 4 (see above)
  cmd1 += cmd.length();
  cmd1 += "\r\n";
  dbgPrint(cmd1);
  if(Serial.find(">")){
    monitor.print(">");
    monitor.print(cmd);
    Serial.print(cmd);
  } else { // if there are ERRORS
    dbgPrint("AT+CIPCLOSE=4\r\n"); // close data port 4
    monitor.print(getTimeStamp(millis()));
    monitor.println(" connection timeout");
    moduleOk = 0; // will reset the module at the beginning of the next loop. Very harsh method but also reliable.
    return millis();
  }
  // string should have been sent
  if(Serial.find("OK")){
    monitor.print(getTimeStamp(millis()));
    monitor.println(" RECEIVED: OK");
  }else{
    monitor.print(getTimeStamp(millis()));
    monitor.println(" RECEIVED: Error while sending data.");
    moduleOk = 0; // will reset the module at the beginning of the next loop. Very harsh method but also reliable.
  }
  return millis();
}


// return the current running time (millis()) in the HHHH:MM:SS format
String getRunningTime(unsigned long time){
  unsigned long sec = (unsigned long)time/1000; // seconds counted from the boot of the arduino up to 50days, then resets to 0 and starts over again
  static char str[12];
  long h = sec / 3600;
  sec = sec % 3600;
  int m = sec / 60;
  int s = sec % 60;
  sprintf(str, "%04ld:%02d:%02d", h, m, s);
  return str;
}

// generate a time stamp like "[HHHH:MM:SS]"
String getTimeStamp(unsigned long time){
  String tmp = "";
  tmp += "[";
  tmp += getRunningTime(time);
  tmp += "]";
  return tmp;
}


// Send command to Serial and monitor
void dbgPrint(String cmd) {
  monitor.print(getTimeStamp(millis()));
  monitor.print(" SEND: ");
  monitor.print(cmd);
  Serial.print(cmd);
}
