#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include "BasicStepperDriver.h"
#include <IRremoteESP8266.h>
#include <TaskScheduler.h>

//States --------------------------------------------------------------------------------------------------
bool lamp_on = true;
bool rgblamp_on = false;
bool lamp_status = "OFF";
bool lamp_oldstatus = "OFF";
bool rgblamp_status = "OFF";
bool rgblamp_oldstatus = "OFF";
int r = 0;
int g = 0;
int b = 0;
int next_r;
int nest_g;
int next_b;
int rgbsteps;
int dir_r = 0;
int dir_g = 0;
int dir_b = 0;
int offlineblinkfrec = 500;
int mqttconblinkfrec = 250;
int onlineblinkfrec = 2000;
int cstatus = 0;  // 0 - Disconnected, 1 - MQTT connecting, 2 - MQTT connected
int motorstatus = 0;  // 0 - Stopped, 1 - UP, 2 - Down
int motordir = 0; //0 - UP, 1 - DOWN

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
void rgbAnimation();
//Tasks
Task blinkStatusLed(offlineblinkfrec, TASK_FOREVER, &statusLEDOff, &runner, true, NULL, &returnblinkstatus);
Task reconnecttask(5000, TASK_FOREVER, &reconnect, &runner);
Task wifitask(5, TASK_FOREVER, &clientloop, &runner);
Task motoron(1, TASK_FOREVER, &motorup, &runner);
Task rgbAnimationTask(20, rgbsteps, &rgbAnimation, &runner);
Task irrectask(100, TASK_FOREVER, &irrec, &runner, true);


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
  stepper.move(1*MICROSTEPS);
}

void motordown(){
  stepper.move(-1*MICROSTEPS);
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
        break;
      case 0x8F7D02F:  // DOWN
        motoron.setCallback(&motordown);
        motoron.enable();
        break;
      case 0x8F7906F:  // SEL
        motoron.disable();
        break;
      }  
  }  
}

// RGB leds
void rgbAnimation(){
  
}

void rgbAnimationloop(){
  if (dir_r == 0){
    r++;
  }
  else{
    r--;
  }
  if (dir_g == 0){
    g++;
  }
  else{
    g--;
  }
  if (dir_b == 0){
    b++;
  }
  else{
    b--;
  }

  if (r == 254){
    dir_r = 1;
  }
  else if(r == 0){
    dir_r = 0;
  }
  if (g == 254){
    dir_g = 1;
  }
  else if (g==0){
    dir_g = 0;
  }
  if (b == 254){
    dir_b = 1;
  }
  else if (b==0){
    dir_b = 0;
  }
   
  analogWrite(red_pin, r);
  analogWrite(green_pin, g);
  analogWrite(blue_pin, b);    
  
}

// SETUP --------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.print("Ikea Lamp Control");

  pinMode(lamp_pin, OUTPUT);
  pinMode(statusled_pin, OUTPUT);
  
  digitalWrite(lamp_pin, HIGH);
  digitalWrite(statusled_pin, HIGH);
  
  pinMode(red_pin, OUTPUT);
  pinMode(green_pin, OUTPUT);
  pinMode(blue_pin, OUTPUT);
  analogWrite(red_pin, r);
  analogWrite(green_pin, g);
  analogWrite(blue_pin, b);
  
  // Wifi
  WiFiManager wifiManager;  
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.autoConnect("Lamp");
  
  // MQTT client
  client = PubSubClient(mqtt_server, 1883, mqttcallback, espClient);  
  
  // IR receiver
  irrecv.enableIRIn();

  // Motor
//  stepper.setRPM(180);
  
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
      digitalWrite(lamp_pin, HIGH);
      lamp_on = true;
      client.publish("lamp/status", "ON", true);
      Serial.println("Lamp ON");
    }
    else if (msgString.equals("OFF")){
      digitalWrite(lamp_pin, LOW);
      lamp_on = false;
      client.publish("lamp/status", "OFF", true);
      Serial.println("Lamp OFF");
    }
    else{
      Serial.println("Unknown payload");
    }
  }

  // RGB lamp switch
  else if (String(topic).equals("rgblamp/switch")){
    if (msgString.equals("ON")){
      analogWrite(red_pin, r);
      analogWrite(green_pin, g);
      analogWrite(blue_pin, b);
      rgblamp_on = true;
      client.publish("rgblamp/status", "ON", true);
      Serial.println("RGB Lamp ON");
    }
    else if (msgString.equals("OFF")){
      analogWrite(red_pin, 0);
      analogWrite(green_pin, 0);
      analogWrite(blue_pin, 0);
      rgblamp_on = false;
      client.publish("rgblamp/status", "OFF", true);
      Serial.println("RGB Lamp OFF");
    }
    else{
      Serial.println("Unknown payload");
    }
  }

  // RGB lamp color
  else if (String(topic).equals("rgblamp/rgbcommand")){
    int commaIndex = msgString.indexOf(',');
    int secondCommaIndex = msgString.indexOf(',', commaIndex+1);
    String firstValue = msgString.substring(0, commaIndex);
    String secondValue = msgString.substring(commaIndex+1, secondCommaIndex);
    String thirdValue = msgString.substring(secondCommaIndex+1); // To the end of the string
    r = (firstValue.toFloat()/255)*1023;
    g = (secondValue.toFloat()/255)*1023;
    b = (thirdValue.toFloat()/255)*1023;
    
    Serial.print("R:");
    Serial.println(r);
    Serial.print("G:");
    Serial.println(g);
    Serial.print("B:");
    Serial.println(b);
    
//    analogWrite(red_pin, r);
//    analogWrite(green_pin, g);
//    analogWrite(blue_pin, b);;
  }

  
  else if (String(topic).equals("motor/test")){
    Serial.println("Motor test");
    if (motorstatus == 0){
      motoron.enable();
      motorstatus = 1;
    }
    else{
      motoron.disable();
      motorstatus = 0;
      if (motordir == 0){
        motoron.setCallback(&motordown);
        motordir = 1;
      }
      else{
        motoron.setCallback(&motorup);
        motordir = 0;
      }
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

