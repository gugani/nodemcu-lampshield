#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include "BasicStepperDriver.h"
#include <IRremoteESP8266.h>
#include <TaskScheduler.h>
#include <RGBConverter.h>
#include <EEPROM.h>

//Definitions and States --------------------------------------------------------------------------------------------------
bool lamp_on = true;
bool rgblamp_on = false;
int crgb[] = {254,254,254};
float crgb_[3]; // Para sumar los incrementos de color
//int nrgb[3];
int rgbsteps[3];
float rgbinc[3];
int fadetime = 3000;
int fadesteps = 200;
int offlineblinkfrec = 500;
int mqttconblinkfrec = 250;
int onlineblinkfrec = 2000;
int cstatus = 0;  // 0 - Disconnected, 1 - MQTT connecting, 2 - MQTT connected
int motorstatus = 0;  // 0 - Stopped, 1 - UP, 2 - Down
int motordir = 0; //0 - UP, 1 - DOWN
int rpm = 120;
int totalsteps = 50 * 200;
int currentstep;
float rpm_frec = 250 * (60/rpm); // 250 porque avanzamos 1/4 de vuelta con el callback de "motoron" task
int currentposition_address = 0;
int lampstate_address = 4; // lamp_on
int rgblampstate_address = 2; // rgblamp_on
int rstate_address = 5; // crgb[0]
int gstate_address = 6; // crgb[1]
int bstate_address = 7; // crgb[2

// Task Scheduler-------------------------------------------------------------------------------------------
Scheduler runner;
// Callback methods prototypes
void lampOpenCallback();
void lampCloseCallback();
void statusLEDOff();
void statusLEDOn();
void reconnect();
void clientloop();
void motorup();
void motordown();
void irrec();
void rgbColorFade();
void rgbRandomColors();

//Tasks
Task blinkStatusLed(offlineblinkfrec, TASK_FOREVER, &statusLEDOff, &runner, true, NULL, &returnblinkstatus);
Task reconnecttask(5000, TASK_FOREVER, &reconnect, &runner);
Task wifitask(5, TASK_FOREVER, &clientloop, &runner);
Task motoron(int(rpm_frec), TASK_FOREVER, &motorup, &runner);
Task rgbColorFadeTask(fadetime/fadesteps, fadesteps, &rgbColorFade, &runner, false, NULL);
Task IRrecTask(100, TASK_FOREVER, &irrec, &runner, true);
Task rgbRandomcolorsTask(fadetime, TASK_FOREVER, &rgbRandomColors, &runner);

// Pin definitions -----------------------------------------------------------------------------------------
#define lamp_pin    5
#define red_pin     4
#define green_pin   0
#define blue_pin    2
#define dir_pin     13
#define step_pin    15
#define statusled_pin 10
#define irled_pin 12

//Motor definitions ---------------------------------------------------------------------------------------
#define MOTOR_STEPS 200             // Motor steps per revolution. Most steppers are 200 steps or 1.8 degrees/step
#define MICROSTEPS 1                // 1=full step, 2=half step etc.
BasicStepperDriver stepper(MOTOR_STEPS, dir_pin, step_pin); // 2-wire basic config, microstepping is hardwired on the driver

// Wifi------------------------------------------------------------------------------------------------------
WiFiClient espClient;

// MQTT client
#define mqtt_server "192.168.21.250"
#define mqtt_user "your_username"
#define mqtt_password "your_password"
PubSubClient client;

// IR receiver-----------------------------------------------------------------------------------------------
IRrecv irrecv(irled_pin);
decode_results results;

// Util------------------------------------------------------------------------------------------------------
char message_buff[100];

// Task Callbacks -------------------------------------------------------------------------------------------
// Status LED
void statusLEDOn () {
  digitalWrite(statusled_pin , HIGH);
  blinkStatusLed.setCallback( &statusLEDOff);
}

void statusLEDOff () {
  digitalWrite(statusled_pin , LOW);
  blinkStatusLed.setCallback( &statusLEDOn);
}

void returnblinkstatus(){
  switch (cstatus){
    case 0:
      blinkStatusLed.setInterval(offlineblinkfrec);
      break;
    case 1:
      blinkStatusLed.setInterval(mqttconblinkfrec);
      break;
    case 2:
      blinkStatusLed.setInterval(onlineblinkfrec);
      break;
  }
  blinkStatusLed.setIterations(TASK_FOREVER);
  blinkStatusLed.enable();
}

// Reconnect Wifi and MQTT
void reconnect() {
  // Loop until we're reconnected    
  Serial.print("Attempting MQTT connection...");
  // Attempt to connect
  // If you do not want to use a username and password, change next line to
  if (client.connect("ESP8266Client")) {
  //if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {    
    Serial.println("connected");
    reconnecttask.disable();
    wifitask.enable();
    cstatus = 2;   
    blinkStatusLed.setInterval(onlineblinkfrec);  
    client.subscribe("lamp/#");
    client.subscribe("rgblamp/#");
    client.subscribe("motor/#");
    if (lamp_on == true){
      client.publish("lamp/status", "ON", true);
    }
    else{
      client.publish("lamp/status", "OFF", true);
    }
    if (rgblamp_on == true){
      client.publish("rgblamp/status", "ON", true);
    }
    else{
      client.publish("rgblamp/status", "OFF", true);
    }
  } 
  else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
  }
}

// Wifi client loop
void clientloop(){
  client.loop();
}

// Motor
void motorup(){
  stepper.move(50*MICROSTEPS);
  currentstep = currentstep + 50;
  if (currentstep > totalsteps){
    currentstep = totalsteps;
    motoron.disable();
    client.publish("lamp/motor", "STOP", true);
    Serial.println("Lamp fully closed");
  }
  EEPROMWritelong(currentposition_address, currentstep);
//  EEPROM.commit();
}

void motordown(){
  stepper.move(-50*MICROSTEPS);
  currentstep = currentstep - 50;  
  if (currentstep < 0){
    currentstep = 0;
    motoron.disable();
    client.publish("lamp/motor", "STOP", true);
    Serial.println("Lamp fully opened");
  }
  EEPROMWritelong(currentposition_address, currentstep);
//  EEPROM.commit();
}

// IR receiver
void irrec(){
  if (irrecv.decode(&results)) {
    Serial.println(results.value, HEX);
    irrecv.resume(); // Receive the next value
    // Blink status led
    blinkStatusLed.setInterval(100);
    blinkStatusLed.setIterations(10);
    switch(results.value) {
      case 0x8F750AF:  // UP
        motoron.setCallback(&motorup);
        motoron.enable();
        client.publish("lamp/motor", "OPEN", true);
        break;
      case 0x8F7D02F:  // DOWN
        motoron.setCallback(&motordown);
        motoron.enable();
        client.publish("lamp/motor", "CLOSE", true);
        break;
      case 0x8F7906F:  // SEL
        motoron.disable();
        client.publish("lamp/motor", "STOP", true);
        Serial.println(currentstep);
        break;
      case 0x8F7708F: // MENU
        Serial.println("RED");
        stopallRGBtasks();
        rgbColorFadeTask.setIterations(fadesteps);
        rgbtransition(255,0,0);        
        rgbColorFadeTask.enable();
        client.publish("rgblamp/status", "ON", true);
        Serial.println("RGB Lamp ON");
        break;          
      case 0x8F708F7: // INFO
        Serial.println("GREEN");
        // Stop all RGB tasks
        stopallRGBtasks();
        rgbColorFadeTask.setIterations(fadesteps);
        rgbtransition(0,255,0);        
        rgbColorFadeTask.enable();
        client.publish("rgblamp/status", "ON", true);
        Serial.println("RGB Lamp ON");
        break;      
      case 0x8F7C837: // AUTO
        Serial.println("BLUE");
        stopallRGBtasks();        
        rgbColorFadeTask.setIterations(fadesteps);
        rgbtransition(0,0,255);        
        rgbColorFadeTask.enable();
        client.publish("rgblamp/status", "ON", true);
        Serial.println("RGB Lamp ON");
        break;
      case 0x8F7F00F: // POWER
        if (lamp_on == false){
          switchwhitelamp(1);
        }
        else{
          switchwhitelamp(0);
        }  
        break;        
      case 0x8F718E7: // SOURCE
        if (rgblamp_on == false){
          switchrgblamp(1);
        }
        else{
          switchrgblamp(0);
        } 
      
        // Stop all RGB tasks
//        Serial.println("RGB: Random colors");
//        stopallRGBtasks();
//        rgbRandomcolorsTask.enable();
     } // switch statement end
  } // if statement end 
}

void rgbtransition(int r, int g, int b){
  int color[] = {r,g,b};
  for (int i = 0; i < 3; i++){
    rgbsteps[i] = color[i] - crgb[i];          
    rgbinc[i] = float(rgbsteps[i])/fadesteps;          
    crgb_[i] = crgb[i];
  }  
}

// RGB leds
void rgbColorFade(){
  int oldrgb[3];  
  for (int i = 0; i < 3; i++){
    oldrgb[i] = crgb[i];
    crgb_[i] = crgb_[i] + rgbinc[i];
    crgb[i] = int(crgb_[i]);    
  }
  if (crgb[0] != oldrgb[0]){    
    analogWrite(red_pin, crgb[0]);  
  }
  if (crgb[1] != oldrgb[1]){    
    analogWrite(green_pin, crgb[1]);
  }
  if (crgb[2] != oldrgb[2]){    
    analogWrite(blue_pin, crgb[2]);  
  }
}

void rgbRandomColors(){  
  rgbColorFadeTask.disable();
  rgbColorFadeTask.setIterations(fadesteps);
  rgbtransition(random(255),random(255),random(255));        
  rgbColorFadeTask.enable();
//  client.publish("rgblamp/status", "ON", true);
//  Serial.println("RGB Lamp ON");
}


// SETUP --------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  delay(500);
  Serial.print("Ikea Lamp Control");

  // Read EEPROM values
  currentstep = EEPROMReadlong(currentposition_address);

  crgb[0] = map(EEPROM.read(rstate_address), 0, 255, 0, 1023);
  crgb[1] = map(EEPROM.read(gstate_address), 0, 255, 0, 1023);
  crgb[2] = map(EEPROM.read(bstate_address), 0, 255, 0, 1023);
  
  if (EEPROM.read(lampstate_address) == 1){
    lamp_on = true;    
  }
  else{
    lamp_on = false;
  }
  if (EEPROM.read(rgblampstate_address) == 1){
    rgblamp_on = true;    
  }
  else{
    rgblamp_on = false;
  }

  // Print status
  Serial.println(" ");
  Serial.print("Current position (0-10000): ");
  Serial.println(currentstep);
  Serial.print("Lamp: ");
  Serial.println(lamp_on);
  Serial.print("RGB Lamp: ");
  Serial.println(rgblamp_on);
  Serial.print("R:");
  Serial.println(crgb[0]);
  Serial.print("G:");
  Serial.println(crgb[1]);
  Serial.print("B:");
  Serial.println(crgb[2]);

  // Pins config
  pinMode(lamp_pin, OUTPUT);
  pinMode(statusled_pin, OUTPUT);  
  pinMode(red_pin, OUTPUT);
  pinMode(green_pin, OUTPUT);
  pinMode(blue_pin, OUTPUT);

  // Last state before switch off
  if (lamp_on == true){
    digitalWrite(lamp_pin, HIGH);
  }
  else{
    digitalWrite(lamp_pin, LOW);
  }
  if (rgblamp_on == true){
    analogWrite(red_pin, crgb[0]);
    analogWrite(green_pin, crgb[1]);
    analogWrite(blue_pin, crgb[2]);
  }
  else{
    analogWrite(red_pin, 0);
    analogWrite(green_pin, 0);
    analogWrite(blue_pin, 0);
  }

  
  // Wifi
  WiFiManager wifiManager;  
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.autoConnect("Euclides", "nobebacafe");
  delay(500);
  // MQTT client
  client = PubSubClient(mqtt_server, 1883, mqttcallback, espClient);  
  
  // IR receiver
  irrecv.enableIRIn();

  // Motor
  stepper.setRPM(rpm);
  delay(500);  
  // set point-in-time for scheduling start  
  runner.startNow();
}

//LOOP ------------------------------------------------------------------------------------------------
void loop() {  
  if (!client.connected()) {
    if (cstatus != 1){
      blinkStatusLed.setInterval(mqttconblinkfrec);
      wifitask.disable();
      reconnecttask.enable();
      cstatus = 1;
    }
  }
  
  //Task Scheduler loop
  runner.execute();
}

//MQTT utils and callbacks------------------------------------------------------------------------------
// MQTT subscribed topics callback
void mqttcallback(char* topic, byte* payload, unsigned int length) {
  int i = 0;
  Serial.println("Message arrived:  topic: " + String(topic));
  Serial.println("Length: " + String(length,DEC));

  // Blink status led
  blinkStatusLed.setInterval(100);
  blinkStatusLed.setIterations(10);
  
  // create character buffer with ending null terminator (string)
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  
  String msgString = String(message_buff);
  
  Serial.println("Payload: " + msgString);

  // White lamp switch
  if (String(topic).equals("lamp/switch")){
    if (msgString.equals("ON")){
      switchwhitelamp(1);
    }
    else if (msgString.equals("OFF")){
      switchwhitelamp(0);
    }
    else{
      Serial.println("Unknown payload");
    }
    EEPROM.commit();
  }

  // RGB lamp switch
  else if (String(topic).equals("rgblamp/switch")){
    if (msgString.equals("ON")){
      switchrgblamp(1);
//      analogWrite(red_pin, crgb[0]);
//      analogWrite(green_pin, crgb[1]);
//      analogWrite(blue_pin, crgb[2]);
//      rgblamp_on = true;
//      client.publish("rgblamp/status", "ON", true);
//      Serial.println("RGB Lamp ON");
//      EEPROM.write(rgblampstate_address, 1);
    }
    else if (msgString.equals("OFF")){
      switchrgblamp(0);
//      analogWrite(red_pin, 0);
//      analogWrite(green_pin, 0);
//      analogWrite(blue_pin, 0);
//      rgblamp_on = false;
//      client.publish("rgblamp/status", "OFF", true);
//      Serial.println("RGB Lamp OFF");
//      EEPROM.write(rgblampstate_address, 0);
    }
    else{
      Serial.println("Unknown payload");
    }    
  }

  // RGB lamp color
  else if (String(topic).equals("rgblamp/rgbcommand")){
    // Stop all RGB tasks
    int commaIndex = msgString.indexOf(',');
    int secondCommaIndex = msgString.indexOf(',', commaIndex+1);
    String firstValue = msgString.substring(0, commaIndex);
    String secondValue = msgString.substring(commaIndex+1, secondCommaIndex);
    String thirdValue = msgString.substring(secondCommaIndex+1); // To the end of the string
    crgb[0] = map(firstValue.toFloat(), 0, 255, 0, 1023);
    crgb[1] = map(secondValue.toFloat(), 0, 255, 0, 1023);
    crgb[2] = map(thirdValue.toFloat(), 0, 255, 0, 1023);
    Serial.print("R:");
    Serial.println(crgb[0]);
    Serial.print("G:");
    Serial.println(crgb[1]);
    Serial.print("B:");
    Serial.println(crgb[2]);
    stopallRGBtasks();
    analogWrite(red_pin, crgb[0]);
    analogWrite(green_pin, crgb[1]);
    analogWrite(blue_pin, crgb[2]);
    EEPROM.write(rstate_address, firstValue.toInt());
    EEPROM.write(gstate_address, secondValue.toInt());
    EEPROM.write(bstate_address, thirdValue.toInt());
    EEPROM.commit();
  }

  else if (String(topic).equals("lamp/motor/set")){
    if (msgString.equals("OPEN")){
      motoron.setCallback(&motorup);
      motoron.enable();
    }
    else if (msgString.equals("CLOSE")){
      motoron.setCallback(&motordown);
      motoron.enable();
    }
    else if (msgString.equals("STOP")){
      motoron.disable();
    }   
  }
  else{
    Serial.println("Unknown topic");
  }
}

// Wifi Manager callbacks-------------------------------------------------------------------------
// Wifi enters config
void configModeCallback (WiFiManager *myWiFiManager) {
  blinkStatusLed.setInterval(offlineblinkfrec);  
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void saveConfigCallback(){
  Serial.println("Save Config Callback"); 
}


// Util ------------------------------------------------------------------------------------------

void switchwhitelamp(int command){
  if (command == 1){
    digitalWrite(lamp_pin, HIGH);
    lamp_on = true;
    client.publish("lamp/status", "ON", true);
    Serial.println("Lamp ON");
    EEPROM.write(lampstate_address, 1);
  }
  else{
    digitalWrite(lamp_pin, LOW);
    lamp_on = false;
    client.publish("lamp/status", "OFF", true);
    Serial.println("Lamp OFF");
    EEPROM.write(lampstate_address, 0);
  }
  EEPROM.commit();
}

void switchrgblamp(int command){
  if (command == 1){
    analogWrite(red_pin, crgb[0]);
    analogWrite(green_pin, crgb[1]);
    analogWrite(blue_pin, crgb[2]);
    rgblamp_on = true;
    client.publish("rgblamp/status", "ON", true);
    Serial.println("RGB Lamp ON");
    EEPROM.write(rgblampstate_address, 1);
  }
  else{
    stopallRGBtasks();
    analogWrite(red_pin, 0);
    analogWrite(green_pin, 0);
    analogWrite(blue_pin, 0);
    rgblamp_on = false;
    client.publish("rgblamp/status", "OFF", true);
    Serial.println("RGB Lamp OFF");
    EEPROM.write(rgblampstate_address, 0);
  }
  EEPROM.commit();
}

int getMax(int array_[]){
  int mxm = array_[0];
  for (int i=0; i < sizeof(array_); i++) {
    if (array_[i]>mxm) {
    mxm = array_[i];
    }
  }
  return mxm;
}

void stopallRGBtasks(){
  rgbColorFadeTask.disable(); 
  rgbRandomcolorsTask.disable();
}

//This function will write/read a 4 byte (32bit) long to the eeprom at
//the specified address to address + 3.
void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);
  
  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

long EEPROMReadlong(long address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);
  
  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

