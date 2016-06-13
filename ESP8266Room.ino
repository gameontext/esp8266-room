
/*******************************************************************************
 * Copyright (c) 2016 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <ArduinoJson.h>

#define LED_RED     15
#define LED_GREEN   12
#define LED_BLUE    13
#define LDR         A0
#define BUTTON      4
#define USE_SERIAL Serial

ESP8266WiFiMulti WiFiMulti;

WebSocketsServer webSocket = WebSocketsServer(7599);

long bookmark=0;

String RGBLedColor = "000000";

//Sends a message to the room, or to a user, if sending to the entire room, the message
//is broadcast, otherwise it is sent to the client passed via 'num'.
void sendMessageToRoom(String messageForRoom, String messageForUser, String userid, uint8_t num) {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["type"] = "event";
  JsonObject& content = jsonBuffer.createObject();  

  //for some reason I had issues passing String as a value to a JsonObject assignment
  //even tho the messageForRoom/messageForUser could be printed to the Serial here, 
  //assigning it via content[userid]=messageForUser resulted in null being set. 
  //moving the data from the String to a char* seemed to solve that issue.
  char *userData=NULL;
  char *roomData=NULL;
  if( messageForRoom.length()>0){
    roomData = (char*)malloc(messageForRoom.length()+1);
    messageForRoom.toCharArray(roomData,messageForRoom.length()+1);
    content["*"] = roomData;
  }
  if(messageForUser.length()>0){
    userData = (char*)malloc(messageForUser.length()+1);
    messageForUser.toCharArray(userData,messageForUser.length()+1);
    content[userid] = userData;
  }
  root["content"] = content;
  root["bookmark"] = bookmark++;

  //now we have the JsonObject built, we need to append it to the 
  //routed message prefix ready to send, and the websocket library 
  //expects a char*, so we'll need to allocate that too.

  //calc the max length for our buffer, based on possible data.
  int bufferLen = root.measureLength()+1+userid.length()+9;
  char* buffer = (char*)malloc(bufferLen);

  //start by building the prefix using a String, then write that
  //to the start of the char* buffer.
  String data="player,";
  if(messageForRoom.length()==0){
    data += userid+",";
  }else{
    data += "*,";
  }
  data.toCharArray(buffer,bufferLen);

  //remember how much data we have written, and 
  //create a new pointer into the buffer where the terminating zero went.  
  int prefixLen = strlen(buffer);
  char* jsonStart = buffer + prefixLen;

  //write the json object, remembering to tell it how many bytes we have left
  //for it to use.
  root.printTo(jsonStart,bufferLen-prefixLen);

  //finally, write the buffer to the websocket, either direct, or broadcast
  if(messageForRoom.length()==0){
    USE_SERIAL.print("DirectSend: ");
    USE_SERIAL.println(buffer);
    webSocket.sendTXT(num, buffer);
  }else{
    USE_SERIAL.print("Broadcast: ");
    USE_SERIAL.println(buffer);
    webSocket.broadcastTXT(buffer);
  }

  //deallocate any buffers we allocated.
  free(buffer);
  if(userData!=NULL)
    free(userData);
  if(roomData!=NULL)
    free(roomData);
}

//send a chat message to all users.
void sendChatMessage(String message, String username, String userid) {
  
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  //a little simpler, we had no issues with string assignment here, 
  //not sure why.
  root["type"] = "chat";
  root["username"] = username;
  root["content"] = message;
  root["bookmark"] = bookmark++;

  //calc the max length for our buffer, based on possible data.
  int bufferLen = root.measureLength()+1+1+9;
  char* buffer = (char*)malloc(bufferLen);

  //start by building the prefix using a String, then write that
  //to the start of the char* buffer.
  String data = "player,*,";
  data.toCharArray(buffer,bufferLen);

  //remember how much data we have written, and 
  //create a new pointer into the buffer where the terminating zero went.  
  int prefixLen = strlen(buffer);
  char* jsonStart = buffer + prefixLen;

  //write the json object, remembering to tell it how many bytes we have left
  //for it to use.
  root.printTo(jsonStart,bufferLen-prefixLen);

  //finally, write the buffer to the websocket, via broadcast
  USE_SERIAL.print("Broadcast: ");
  USE_SERIAL.println(buffer);
  webSocket.broadcastTXT(buffer);

  //deallocate any buffers we allocated.
  free(buffer);
}

//processes a 6char hex rgb value and sets the rgb led appropriately/
void processRGBPayload(String value, String userid, String username, uint8_t num) {
    USE_SERIAL.print("Processing RGB value of ");
    USE_SERIAL.println(value);
    //if it's not 6 chars long, we don't even want to try.
    if(value.length()==6){
      char data[] = "FFFFFFx";
      value.toCharArray(data,7);

      //log the raw value before we do anything daft.
      USE_SERIAL.printf("raw rgb value string: %s\n",data);
      
      //use strtol to convert from hex to a large number.. 
      //if the value doesn't convert, it'll return zero.
      uint32_t rgb = (uint32_t) strtol((const char *) &data[0], NULL, 16);
      //if we got back zero, was that what the string said? 
      //or did we fail to parse.
      if(rgb==0 && value!="000000"){
        sendMessageToRoom("","Hmm.. I couldn't make sense of that rgb value.. ",userid,num);
        return;
      }

      //log out the rgb values.
      USE_SERIAL.printf("Raw uint32: %d\n",rgb);
      USE_SERIAL.printf("RED: %d\n",((rgb >> 16) & 0xFF));
      USE_SERIAL.printf("GREEN: %d\n",((rgb >> 8) & 0xFF));
      USE_SERIAL.printf("BLUE: %d\n",((rgb >> 0) & 0xFF));

      //write the rgb values to the GPIO pins using analogWrite to set pwm levels.
      analogWrite(LED_RED,    ((rgb >> 16) & 0xFF));
      analogWrite(LED_GREEN,  ((rgb >> 8) & 0xFF));
      analogWrite(LED_BLUE,   ((rgb >> 0) & 0xFF));

      //build a hex string representing the rgb values that we can use to tell people the 
      //state of the rgb. Note here we do not use value, because this will clean up the 
      //casing, and avoid any chance of using a random non hex string made it through strtol      
      sprintf(data,"%02X%02X%02X",((rgb >> 16) & 0xFF),((rgb >> 8) & 0xFF),((rgb >> 0) & 0xFF));
      //store the rgb value into the global.
      RGBLedColor = String(data);

      //create a message telling people what happened.
      String allMessage = username+" alters the color of the RGBLed";
      //important! do not append to constants during string init, it doesnt always work
      //best I can tell, the above line works because username is already a String, where
      //as this line will not because the constant is not yet a String object.
      String userMessage = "RGB Led is now set to #";
      userMessage+=RGBLedColor;
      sendMessageToRoom(allMessage,userMessage,userid,num);
    }else{
      sendMessageToRoom("","Hmm.. I couldn't make sense of that rgb value.. ",userid,num);
    }
}

//breaks up a routed message into an array of char* 
//note we only process up to 3 segments ever.
void splitRouting(char *message, int len, char *split[]){
  //the first segment will always start at the front of the buffer.
  split[0] = message;
  int i=0;
  int j=1;
  while(i<len && j<3){
    if(message[i] == '{'){
      break;
    }
    if(message[i] == ',' && i<(len-1)){
      //store the index for the 'next' segment, 
      split[j++] = &message[i+1];
      //write a null terminator into the buffer to terminate the last segment.
      message[i] = '\0';
    }
    i++;
  }
}

//sends the playerLocation event to the socket, this is sent in reply to roomHello, and 
//also if the player ever issues /look.
void sendLocation(String userId, uint8_t num){
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["type"] = "location";
    root["name"] = "ESP8266";
    root["fullName"] = "ESP8266-IoT Room";
    root["bookmark"] = bookmark++;
    root["description"] = "This room is running on an ESP-8266 (or more precisely, a 'Witty' which is a dev board with an ESP-12F onboard). The code is written using the Arduino IDE, and uses ArduinoJson and ArduinoWebSockets Libraries, and the tiny board has it's own wifi connection for data.";    
    JsonArray& objects = root.createNestedArray("objects");
    objects.add("RGBLed");
    objects.add("LDR");
    objects.add("Button");
    JsonObject& commands = root.createNestedObject("commands");
    commands["/rgb"] = "A command to alter the values of the red green and blue components of the rgb led, by sending a 6 character hex string. Eg, /rgb 01fe9f";
    
    //calc the max length for our buffer, based on possible data.
    int bufferLen = root.measureLength()+1+userId.length()+9;
    char* buffer = (char*)malloc(bufferLen);

    //start by building the prefix using a String, then write that
    //to the start of the char* buffer.
    String data = "player,";
    data+=userId+",";
    data.toCharArray(buffer,bufferLen);

    //remember how much data we have written, and 
    //create a new pointer into the buffer where the terminating zero went.  
    int prefixLen = strlen(buffer);
    char* jsonStart = buffer + prefixLen;

    //write the json object, remembering to tell it how many bytes we have left
    //for it to use.
    root.printTo(jsonStart,bufferLen-prefixLen);

    //finally, write the buffer to the websocket
    USE_SERIAL.print("Location send: ");
    USE_SERIAL.println(buffer);
    webSocket.sendTXT(num,buffer);

    //deallocate the buffer.
    free(buffer);
}

//called when a player enters the room
void addPlayer(char* json, uint8_t num){
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    
    //check if the data we recieved was json..
    if(!root.success()){
      USE_SERIAL.println("addERROR, unable to parse json payload from routed message");
      USE_SERIAL.println(json);
      return;
    }

    //if it was json, did it have the content we expected?
    if(!root.containsKey("username") || !root.containsKey("userId")){
      USE_SERIAL.println("addERROR, json payload missing spec required content");
      USE_SERIAL.println(json);
      return;
    }

    const char *username = root["username"];
    const char *userId = root["userId"];

    //build the messages saying the player has entered..
    String userName = String(username);
    //again be careful here about appending Strings to "" constants.
    String everyoneMessage = "Player ";
    everyoneMessage += userName+" has entered the room";
    String playerMessage = "You have entered the room";
    String userid = String(userId);

    //send the 'player has entered the room' message.
    sendMessageToRoom(everyoneMessage, playerMessage,userid,num);

    //send the location description etc to the player.
    sendLocation(userid,num);    
}

//called when a player issues /go
void processGo(String direction, String userId, uint8_t num){
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["type"] = "exit";
    root["exitId"] = direction;
    root["content"] = "Bye!!";
    root["bookmark"] = bookmark++;
    
    //calc the max length for our buffer, based on possible data.
    int bufferLen = root.measureLength()+1+userId.length()+9+8;
    char* buffer = (char*)malloc(bufferLen);

    //start by building the prefix using a String, then write that
    //to the start of the char* buffer.
    String data = "playerLocation,";
    data+=userId+",";
    data.toCharArray(buffer,bufferLen);

    //remember how much data we have written, and 
    //create a new pointer into the buffer where the terminating zero went.  
    int prefixLen = strlen(buffer);
    char* jsonStart = buffer + prefixLen;

    //write the json object, remembering to tell it how many bytes we have left
    //for it to use.
    root.printTo(jsonStart,bufferLen-prefixLen);

    //finally, write the buffer to the websocket
    USE_SERIAL.print("playerLocation send: ");
    USE_SERIAL.println(buffer);
    webSocket.sendTXT(num,buffer);

    //deallocate the buffer.
    free(buffer);
}

//called when a player is connected to the room, and sends any text to the room
//have to process out any /commands and handle the rest as chat.
void processCommand(char* json, uint8_t num){
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);

    //check if the data was json
    if(!root.success()){
      USE_SERIAL.println("commandERROR, unable to parse json payload from routed message");
      USE_SERIAL.println(json);
      return;
    }

    //check the json had the fields we expected.
    if(!root.containsKey("username") || !root.containsKey("userId") || !root.containsKey("content")){
      USE_SERIAL.println("commandERROR, json payload missing spec required content");
      USE_SERIAL.println(json);
      return;
    }

    const char *userName = root["username"];
    const char *userId = root["userId"];
    const char *contentRaw = root["content"];

    //convert all the char*'s to strings, as they are easier to work with..
    String userid = String(userId);
    String username = String(userName);
    //build 2 versions of the content string to avoid repeatedly creating lowercase ones.
    String content = String(contentRaw);
    //note, arduino String.toLowerCase is in-place, it does not return a new string.
    String contentLower = String(contentRaw);
    contentLower.toLowerCase();
    
    if(contentLower == "/look"){
      sendLocation(userid,num);
    }else if(contentLower.startsWith("/go ")){
       String value = contentLower.substring(4);
       processGo(value,userid,num);
    }else if(contentLower=="/examine rgbled"){
      String allMessage = username+" investigates the RGBLed";
      //another 'be careful not to append String to "" constant' example
      String userMessage = "It appears to be a real RGB Led, attached directly to GPIO pins 15,12 and 13, and currently has the RGB value #";
      userMessage+=RGBLedColor;
      userMessage+=". You can change it's color using the /rgb command, see /help for more information.";
      sendMessageToRoom(allMessage,userMessage,userid,num);
    }else if(contentLower=="/examine button"){
      String allMessage = username+" investigates the button";
      int buttonValue = digitalRead(4);
      //another 'be careful not to append String to "" constant' example
      String userMessage = "It appears to be a momentary contact switch, attached to GPIO4, it currently has the state ";
      userMessage += buttonValue;    
      userMessage += ". Sadly unless the room owner sits there holding the button down, that value is unlikely to change.";  
      sendMessageToRoom(allMessage,userMessage,userid,num);
    }else if(contentLower=="/examine ldr"){
      String allMessage = username+" investigates the LDR";
      int ldrValue = analogRead(A0);
      //another 'be careful not to append String to "" constant' example
      String userMessage = "It appears to be a Light Dependent Resistor, attached to GPIOA0, it currently reads with the value ";      
      userMessage += ldrValue;
      userMessage += ". The range is 0-1023, and the brighter the room is, the larger the value is. If the room owner is asleep, this value may be low.";
      sendMessageToRoom(allMessage,userMessage,userid,num);
    }else if(contentLower.startsWith("/rgb ")){
      String value = contentLower.substring(5);
      processRGBPayload(value,userid,username,num);
    }else if(contentLower.startsWith("/")){
      sendMessageToRoom("","Unrecognized command, sorry!!",userid,num);
    }else{
      sendChatMessage(content,username,userid);
    }
}

void removePlayer(char* json, uint8_t num){
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    
    //was the input json?
    if(!root.success()){
      USE_SERIAL.println("removeERROR, unable to parse json payload from routed message");
      USE_SERIAL.println(json);
      return;
    }

    //did it have the fields we expected?
    if(!root.containsKey("username") || !root.containsKey("userId")){
      USE_SERIAL.println("removeERROR, json payload missing spec required content");
      USE_SERIAL.println(json);
      return;
    }

    //build and send the 'player has left room' messages.

    const char *username = root["username"];
    const char *userId = root["userId"];

    String userName = String(username);
    String everyOneMessage = "Player ";
    everyOneMessage+=userName+" has left the room";
    String userid = String(userId);
    
    sendMessageToRoom(everyOneMessage,"",userid,num);
}

//the main websocket library call method.. 
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);			
          			// send ack to game on.
          			webSocket.sendTXT(num, "ack,{\"version\":[1]}");
            }
            break;
        case WStype_TEXT:
            USE_SERIAL.printf("[%u] get Text: (len:%d) %s\n", num, length, payload);

            if(payload!=NULL){
                char *parts[3];
                splitRouting((char*)payload,length,parts);
                //parts[0] is the routing type
                //parts[1] is the site id for the room (in case the one socket is managing multiple rooms)
                //parts[2] is the json payload for the message.
                String gameonType = String(parts[0]);
                if(gameonType=="roomHello"){
                  addPlayer(parts[2],num);
                }else if(gameonType=="room"){
                  processCommand(parts[2],num);
                }else if(gameonType=="roomGoodbye"){
                  removePlayer(parts[2],num);
                }else{
                  USE_SERIAL.println("ERROR, badly formatted gameon websocket packet");
                  USE_SERIAL.println((char *)payload);
                }              
            }else{
              USE_SERIAL.println("ERROR, empty websocket packet.");
            }
            
            break;
        case WStype_BIN:
            //game-on doesn't use binary payloads, but let's log any we find.
            USE_SERIAL.printf("[%u] get binary length: %u\n", num, length);
            hexdump(payload, length);
            break;
    }
}

void setup() {
    USE_SERIAL.begin(115200);
    USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

    //setup the gpio.. 
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(BUTTON,INPUT);
    pinMode(LDR,INPUT);

    //set the led to full-red while we initialize.
    digitalWrite(LED_RED, 1);
    digitalWrite(LED_GREEN, 0);
    digitalWrite(LED_BLUE, 0);

    //wait for the wifi part to finish booting up.. 
    //(code from other examples.. )
    for(uint8_t t = 4; t > 0; t--) {
        USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
        USE_SERIAL.flush();
        delay(1000);
    }

    //change the led to green to say we finished the init delay
    digitalWrite(LED_RED, 0);
    digitalWrite(LED_GREEN, 1);
    digitalWrite(LED_BLUE, 0);

    //connect to the wifi..
    WiFiMulti.addAP("TUXQ", "HAPPYELF666");
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }
    USE_SERIAL.printf("[SETUP] COMPLETE.\n");
    USE_SERIAL.printf("[SETUP] IP address: ");
    USE_SERIAL.println(WiFi.localIP());

    //change the led to blue to say we're connected
    digitalWrite(LED_RED, 0);
    digitalWrite(LED_GREEN, 0);
    digitalWrite(LED_BLUE, 1);

    //init the websocket, and add the callback handler.
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    //finally set the led to off matching the expected initial state.
    USE_SERIAL.printf("[SETUP] Socket open.\n");     
    digitalWrite(LED_RED, 0);
    digitalWrite(LED_GREEN, 0);
    digitalWrite(LED_BLUE, 0);
}

//the simplest main loop ever ;p
void loop() {
    webSocket.loop();
}

