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
int rgbbrightness = 100;
int fadetime = 3000;
int fadesteps = 200;
int offlineblinkfrec = 500;
int mqttconblinkfrec = 250;
int onlineblinkfrec = 2000;
int cstatus = 0;  // 0 - Disconnected, 1 - MQTT connecting, 2 - MQTT connected, 3 - Wifi connected, no MQTT
int motorstatus = 0;  // 0 - Stopped, 1 - UP, 2 - Down
//int motordir = 0; //0 - UP, 1 - DOWN
int rpm = 120;
int totalsteps = 50 * 200;
int currentstep;
float rpm_frec = 250 * (60/rpm); // 250 porque avanzamos 1/4 de vuelta con el callback de "motoron" task
int currentposition_address = 0;
int lampstate_address = 4; // lamp_on (eeprom)
int rgblampstate_address = 2; // rgblamp_on (eeprom)
int rstate_address = 5; // crgb.r (eeprom)
int gstate_address = 6; // crgb.g (eeprom)
int bstate_address = 7; // crgb.b (eeprom)
int rgbbrightness_address = 8; //rgbbrighness (eeprom)
bool effecton = false;

// Colors
struct RGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};
RGB white = {254,254,254};
RGB red = {254,0,0};
RGB salmon = {250,128,114};
RGB orange = {254,165,0};
RGB yellow = {254,254,0};
RGB lawngreen = {124,252,0};
RGB limegreen = {50,205,50};
RGB forestgreen = {34,139,34};
RGB springgreen = {0,255,127};
RGB green = {0,254,0};
RGB cyan = {0,254,255};
RGB turquoise = {64,224,208};
RGB teal = {0,128,128};
RGB blue = {0,0,255};
RGB slateblue = {106,90,205};
RGB violet = {238,130,238};
RGB magenta = {254,0,255};
RGB darkviolet = {148,0,211};
RGB deeppink = {254,20,147};
RGB goldenrod = {218,165,32};
RGB peru = {205,133,63};
RGB brown = {165,42,42};
int colorcounter = 0;
RGB crgb = {254,254,254};
//RGB crgb_;// Para sumar los incrementos de color
float crgb_[3];
int rgbsteps[3];
float rgbinc[] = {0,0,0};
RGB colores[22] = { {254,254.254},{254,0,0}, {250,128,114}, {254,165,0}, {254,254,0}, {124,252,0}, {50,205,50}, {34,139,34}, {0,255,127}, {0,254,0}, {0,254,255}, {64,224,208}, {0,128,128}, {0,0,255}, {106,90,205}, {238,130,238}, {254,0,255}, {148,0,211}, {254,20,147}, {218,165,32}, {205,133,63}, {165,42,42} };


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
void rgbFireColors();
void commiteeprom();

//Tasks
Task blinkStatusLed(offlineblinkfrec, TASK_FOREVER, &statusLEDOff, &runner, true, NULL, &returnblinkstatus);
Task reconnecttask(60000, TASK_FOREVER, &reconnect, &runner);
Task wifitask(5, TASK_FOREVER, &clientloop, &runner);
Task motoron(int(rpm_frec), TASK_FOREVER, &motorup, &runner);
Task rgbColorFadeTask(fadetime/fadesteps, fadesteps, &rgbColorFade, &runner, false, NULL, &commiteeprom);
Task IRrecTask(100, TASK_FOREVER, &irrec, &runner, true);
Task rgbRandomcolorsTask(fadetime, TASK_FOREVER, &rgbRandomColors, &runner, false, NULL, &commiteeprom);
Task rgbFirecolorsTask(100, TASK_FOREVER, &rgbFireColors, &runner);

// Pin definitions -----------------------------------------------------------------------------------------
#define lamp_pin    5
#define red_pin     2
#define green_pin   0
#define blue_pin    4
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
#define mqtt_server "192.168.1.102"
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

void changeblinkstatus(){
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

void blinkcommandrec(){
  blinkStatusLed.setInterval(100);
  blinkStatusLed.setIterations(10);
}

void commiteeprom(){
  Serial.print("R:");
  Serial.println(crgb.r);
  Serial.print("G:");
  Serial.println(crgb.g);
  Serial.print("B:");
  Serial.println(crgb.b);
  if (motorstatus == 0 && rgblamp_on == false && effecton == false){
    EEPROM.commit();
  }
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
    client.subscribe("kimlamp/lamp/#");
    client.subscribe("kimlamp/rgblamp/#");
    client.subscribe("kimlamp/motor/#");
    if (lamp_on == true){
      client.publish("kimlamp/lamp/status", "ON", true);
    }
    else{
      client.publish("kimlamp/lamp/status", "OFF", true);
    }
    if (rgblamp_on == true){
      client.publish("kimlamp/rgblamp/status", "ON", true);
    }
    else{
      client.publish("kimlamp/rgblamp/status", "OFF", true);
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
    client.publish("kimlamp/motor", "STOP", true);
    Serial.println("Lamp fully closed");
    commiteeprom();
    motorstatus = 0;
  }
  EEPROMWritelong(currentposition_address, currentstep);  
}

void motordown(){
  stepper.move(-50*MICROSTEPS);
  currentstep = currentstep - 50;  
  if (currentstep < 0){
    currentstep = 0;
    motoron.disable();
    client.publish("kimlamp/motor", "STOP", true);
    Serial.println("Lamp fully opened");
    commiteeprom();
    motorstatus = 0;
  }
  EEPROMWritelong(currentposition_address, currentstep);  
}

// IR receiver
void irrec(){
  int rgb_values[3];
  if (irrecv.decode(&results)) {
    Serial.println(results.value, HEX);
    irrecv.resume(); // Receive the next value

    switch(results.value) {
      case 0x8F750AF:  // UP (Cierra, volvemos a pulsar para parar)
        blinkcommandrec();
        if (motorstatus == 0){
          closelamp();  
        }
        else{
          motoron.disable();
          client.publish("kimlamp/motor", "STOP", true);
          Serial.println(currentstep);
          motorstatus = 0;
          commiteeprom();
        }
        break;
      case 0x8F7D02F:  // DOWN (Abre, volvemos a pulsar para parar)
        blinkcommandrec();
        if (motorstatus == 0){
          openlamp();  
        }
        else{
          motoron.disable();
          client.publish("kimlamp/motor", "STOP", true);
          Serial.println(currentstep);
          motorstatus = 0;
          commiteeprom();
        }
        break;
      case 0x8F7906F:  // SEL (Test)
        blinkcommandrec();
        Serial.println("RGB: Random colors");
        stopallRGBtasks();
        rgbFirecolorsTask.setIterations(TASK_FOREVER);
        rgbFirecolorsTask.setInterval(100);
        rgbFirecolorsTask.enable();
        break;
      case 0x8F7708F: // MENU (Ciclamos RGB)
        blinkcommandrec();
        Serial.println("RGB: Random colors (fade)"); 
        stopallRGBtasks();       
        rgbRandomcolorsTask.setIterations(TASK_FOREVER);
        rgbRandomcolorsTask.setInterval(fadetime);
        rgbRandomcolorsTask.enable();
        effecton = true;
        break;          
      case 0x8F708F7: // INFO 
        Serial.println("RGB: Switch color (fade)");           
        if (colorcounter == 21){
           colorcounter = 0;
        }
        colorcounter +=1;
        rgbchange(colores[colorcounter]);
//        rgbfadeto(colores[colorcounter]);
        blinkcommandrec();             
        break;      
      case 0x8F7C837: // AUTO // Cambiamos color RGB
        Serial.println("RGB: Switch color (fade)");           
        if (colorcounter == 21){
           colorcounter = 0;
        }
        colorcounter +=1;
//        rgbchange(colores[colorcounter]);
        rgbfadeto(colores[colorcounter]);
        blinkcommandrec();        
        break;
      case 0x8F7F00F: // POWER
        blinkcommandrec();
        if (lamp_on == false){
          switchwhitelamp(1);
        }
        else{
          switchwhitelamp(0);
        }  
        break;        
      case 0x8F718E7: // SOURCE
        blinkcommandrec();
        if (rgblamp_on == false){
          switchrgblamp(1);
        }
        else{
          switchrgblamp(0);
        }
        break;
      case 0x8F7B04F: // LEFT (Aumentamos brillo RGB)
        blinkcommandrec();
        if (rgbbrightness <= 0){
          rgbbrightness = 0;
        }
        else{
          rgbbrightness -= 5;  
        }
        rgbchange(crgb); 
        break;
      case 0x8F730CF: // RIGHT (Disminuimos brillo RGB)
        blinkcommandrec();
        if (rgbbrightness >= 100){
          rgbbrightness = 100;
        }
        else{
          rgbbrightness += 5;  
        }
        rgbchange(crgb);
        break;

     } // switch statement end
  } // if statement end 
}


// Función que prepara todos los parámetros para la transición.
void rgbtransition(RGB values){
  RGB tempcolor = values;
  rgbsteps[0] = tempcolor.r - crgb.r;
  rgbsteps[1] = tempcolor.g - crgb.g;
  rgbsteps[2] = tempcolor.b - crgb.b;
  rgbinc[0] = float(rgbsteps[0])/fadesteps;
  rgbinc[1] = float(rgbsteps[1])/fadesteps;
  rgbinc[2] = float(rgbsteps[2])/fadesteps;
  crgb_[0]= crgb.r;
  crgb_[1]= crgb.g;
  crgb_[2]= crgb.b;
}

// RGB leds
void rgbColorFade(){
  RGB oldrgb;  
  oldrgb = crgb;
  crgb_[0] = crgb_[0] + rgbinc[0];
  crgb_[1] = crgb_[1] + rgbinc[1];
  crgb_[2] = crgb_[2] + rgbinc[2];
  crgb.r = int(crgb_[0]); 
  crgb.g = int(crgb_[1]);    
  crgb.b = int(crgb_[2]);

  //  rgbchange(crgb);
  if (crgb.r != oldrgb.r){    
    analogWrite(red_pin,    map(float(crgb.r)*0.01*float(rgbbrightness), 0, 255, 0, 1023));    
  }
  if (crgb.g != oldrgb.g){    
    analogWrite(green_pin,  map(float(crgb.g)*0.01*float(rgbbrightness), 0, 255, 0, 1023));      
  }
  if (crgb.b != oldrgb.b){    
    analogWrite(green_pin,  map(float(crgb.g)*0.01*float(rgbbrightness), 0, 255, 0, 1023));      
  }

    //Serial.print("Brightness: ");
    //Serial.println(rgbbrightness);
    //Serial.print("R:");
    //Serial.println(crgb.r);
    //Serial.print("G:");
    //Serial.println(crgb.g);
    //Serial.print("B:");
    //Serial.println(crgb.b);
    //Serial.println("------");
  
}

// Arranca la rgbColorFadeTask forever con valores aleatorios
void rgbRandomColors(){
  
  if (colorcounter == 21){
     colorcounter = 0;
  }
  colorcounter +=1;       
  rgbfadeto(colores[colorcounter]);
//  rgbchange(colores[colorcounter]);
//  client.publish("kimlamp/rgblamp/status", "ON", true);
//  Serial.println("RGB Lamp ON");
}

void rgbFireColors(){  
    crgb.r = random(120)+135;
    crgb.g = random(120)+135;
    crgb.b = random(120)+135;
  rgbchange(crgb); 
}
















// SETUP --------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  delay(500);
  Serial.print("Ikea Lamp Control");

  // Read EEPROM values
  currentstep = EEPROMReadlong(currentposition_address);

  if (currentstep > 10000){
    currentstep = 10000;
  }

  crgb.r = map(EEPROM.read(rstate_address), 0, 255, 0, 1023);
  crgb.g = map(EEPROM.read(gstate_address), 0, 255, 0, 1023);
  crgb.b = map(EEPROM.read(bstate_address), 0, 255, 0, 1023);
  
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
    commiteeprom();
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
  Serial.println(crgb.r);
  Serial.print("G:");
  Serial.println(crgb.g);
  Serial.print("B:");
  Serial.println(crgb.b);

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
    analogWrite(red_pin, crgb.r);
    analogWrite(green_pin, crgb.g);
    analogWrite(blue_pin, crgb.b);
  }
  else{
    analogWrite(red_pin, 0);
    analogWrite(green_pin, 0);
    analogWrite(blue_pin, 0);
  }

  
  // Wifi
  WiFiManager wifiManager;
  // Descomentar para resetear configuración
  // wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConfigPortalTimeout(60);
  if(!wifiManager.autoConnect("KimLamp")){
    Serial.println("failed to connect and hit timeout");
  }
  else{
    cstatus = 3;
  }
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
    if (cstatus == 2 || cstatus == 3){
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
  blinkcommandrec();
  
  // create character buffer with ending null terminator (string)
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  
  String msgString = String(message_buff);
  
  Serial.println("Payload: " + msgString);

  // White lamp switch
  if (String(topic).equals("kimlamp/lamp/switch")){
    if (msgString.equals("ON")){
      switchwhitelamp(1);
    }
    else if (msgString.equals("OFF")){
      switchwhitelamp(0);
    }
    else{
      Serial.println("Unknown payload");
    }
    commiteeprom();
  }

  // RGB lamp switch
  else if (String(topic).equals("kimlamp/rgblamp/switch")){
    if (msgString.equals("ON")){
      switchrgblamp(1);
    }
    else if (msgString.equals("OFF")){
      switchrgblamp(0);
    }
    else{
      Serial.println("Unknown payload");
    }    
  }

  // RGB lamp color
  else if (String(topic).equals("kimlamp/rgblamp/setcolor")){
    // Stop all RGB tasks
    RGB colortoset;
    int commaIndex = msgString.indexOf(',');
    int secondCommaIndex = msgString.indexOf(',', commaIndex+1);
    String firstValue = msgString.substring(0, commaIndex);
    String secondValue = msgString.substring(commaIndex+1, secondCommaIndex);
    String thirdValue = msgString.substring(secondCommaIndex+1); // To the end of the string
    //crgb.r = map(firstValue.toFloat(), 0, 255, 0, 1023);
    //crgb.g = map(secondValue.toFloat(), 0, 255, 0, 1023);
    //crgb.b = map(thirdValue.toFloat(), 0, 255, 0, 1023);
    colortoset.r = firstValue.toInt();
    colortoset.g = secondValue.toInt();
    colortoset.b = thirdValue.toInt();
    rgbfadeto(colortoset);
  }

  // Open/close lamp
  else if (String(topic).equals("kimlamp/motor/set")){
    if (msgString.equals("OPEN")){
      openlamp();
    }
    else if (msgString.equals("CLOSE")){
      closelamp();
    }
    else if (msgString.equals("STOP")){
      motoron.disable();
      client.publish("kimlamp/motor", "STOP", true);
      Serial.println(currentstep);
      motorstatus = 0;
      commiteeprom();
    }   
  }

  else if (String(topic).equals("kimlamp/rgblamp/fire")){
    Serial.println("RGB: Random colors");
    stopallRGBtasks();
    rgbFirecolorsTask.setIterations(TASK_FOREVER);
    rgbFirecolorsTask.setInterval(100);
    rgbFirecolorsTask.enable();
  }

  else if (String(topic).equals("kimlamp/rgblamp/random")){
    Serial.println("RGB: Random colors (fade)"); 
    stopallRGBtasks();       
    rgbRandomcolorsTask.setIterations(TASK_FOREVER);
    rgbRandomcolorsTask.setInterval(fadetime);
    rgbRandomcolorsTask.enable();
    effecton = true;
  }

  else if (String(topic).equals("kimlamp/rgblamp/nextcolor")){
    if (colorcounter == 21){
      colorcounter = 0;
    }
    colorcounter +=1;
    rgbchange(colores[colorcounter]);
  }

  else if (String(topic).equals("kimlamp/rgblamp/previouscolor")){
    if (colorcounter == 0){
      colorcounter = 21;
    }
    colorcounter -=1;
    rgbchange(colores[colorcounter]);
  }

  // RGB lamp brightness
  else if (String(topic).equals("kimlamp/rgblamp/brightness")){
    if (msgString.toInt() <= 0){
      rgbbrightness = 0;
    }
    else if (msgString.toInt() >= 100){
      rgbbrightness = 100;
    }
    else{
      rgbbrightness = msgString.toInt();
    }
    rgbchange(crgb);
  }
  
  else{
    Serial.println("Unknown topic");
  }
}

// Wifi Manager callbacks-------------------------------------------------------------------------
// Wifi enters config
void configModeCallback (WiFiManager *myWiFiManager) {
  blinkStatusLed.setInterval(offlineblinkfrec);
  cstatus = 0;
//  Serial.println("Entered config mode");
//  Serial.println(WiFi.softAPIP());
//  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void saveConfigCallback(){
  Serial.println("Save Config Callback"); 
}


// Util ------------------------------------------------------------------------------------------

void switchwhitelamp(int command){
  if (command == 1){
    digitalWrite(lamp_pin, HIGH);
    lamp_on = true;
    client.publish("kimlamp/lamp/status", "ON", true);
    Serial.println("Lamp ON");
    EEPROM.write(lampstate_address, 1);
  }
  else{
    digitalWrite(lamp_pin, LOW);
    lamp_on = false;
    client.publish("kimlamp/lamp/status", "OFF", true);
    Serial.println("Lamp OFF");
    EEPROM.write(lampstate_address, 0);
  }
  commiteeprom();
}

void switchrgblamp(int command){
  if (command == 1){
    rgbchange(crgb);
//    analogWrite(red_pin, crgb.r);
//    analogWrite(green_pin, crgb.g);
//    analogWrite(blue_pin, crgb.b);
    rgblamp_on = true;
    client.publish("kimlamp/rgblamp/status", "ON", true);
    Serial.println("RGB Lamp ON");
    EEPROM.write(rgblampstate_address, 1);
  }
  else{
    stopallRGBtasks();
    analogWrite(red_pin, 0);
    analogWrite(green_pin, 0);
    analogWrite(blue_pin, 0);
    rgblamp_on = false;
    commiteeprom();
    client.publish("kimlamp/rgblamp/status", "OFF", true);
    Serial.println("RGB Lamp OFF");
    EEPROM.write(rgblampstate_address, 0);
  }
  commiteeprom();
}

void rgbfadeto(RGB values){  
//  rgbColorFadeTask.setInterval(fadetime/fadesteps);
  rgbColorFadeTask.setIterations(fadesteps);
  rgbtransition(values);
  rgbColorFadeTask.enable();
//  EEPROM.write(rstate_address, values[0]);
//  EEPROM.write(gstate_address, values[1]);
//  EEPROM.write(bstate_address, values[2]);
  if (rgblamp_on == false){
    client.publish("kimlamp/rgblamp/status", "ON", true);
    Serial.println("RGB Lamp ON");
    rgblamp_on == true; 
  }  
}

void rgbchange(RGB values){  
  analogWrite(red_pin,    map(float(values.r)*0.01*float(rgbbrightness), 0, 255, 0, 1023));
  analogWrite(green_pin,  map(float(values.g)*0.01*float(rgbbrightness), 0, 255, 0, 1023));  
  analogWrite(blue_pin,   map(float(values.b)*0.01*float(rgbbrightness), 0, 255, 0, 1023));
  crgb = values;

//  EEPROM.write(rstate_address, values[0]);
//  EEPROM.write(gstate_address, values[1]);
//  EEPROM.write(bstate_address, values[2]);
//  if (rgblamp_on == false){
//    client.publish("rgblamp/status", "ON", true);
//    Serial.println("RGB Lamp ON");
//    rgblamp_on == true; 
//  }
//  if (motorstatus == 0){
//    EEPROM.commit();
//  }
}

void closelamp(){
  motoron.setCallback(&motorup);
  motoron.enable();
  client.publish("kimlamp/motor", "CLOSE", true);
  motorstatus = 1;
}

void openlamp(){
  motoron.setCallback(&motordown);
  motoron.enable();
  client.publish("kimlamp/motor", "OPEN", true);
  motorstatus = 2;
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
  rgbFirecolorsTask.disable();
  effecton = false;
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
