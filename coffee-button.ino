/**
 * Button that sends a POST to a Slack channel to notify about fresh coffee.
 * Adjust delay to your coffee maker.
 * Uses an RGB LED as a status indicator.
 * 
 * References & inspiration:
 * http://blog.eikeland.se/2015/07/20/coffee-button/
 * https://www.hackster.io/mplewis/big-red-slack-button-23083d
 * https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi/examples
 */
#include <ESP8266WiFi.h>
#include <Bounce2.h>
#include <ArduinoJson.h>

// Configure WiFi, PINs, Slack data & coffee brew time in config.h
#include "config.h"

// Messages where one will be randomly picked for each notification
#define MESSAGE_COUNT 4
String messages[MESSAGE_COUNT] = {
  MESSAGE_1,
  MESSAGE_2,
  MESSAGE_3,
  MESSAGE_4
};

// Delay used for gradual led color changes
#define LED_DELAY 5

// We are creating a json object with 4 values
// more info: https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model
#define SLACK_JSON_SIZE (JSON_OBJECT_SIZE(4))

// Use Bounce2 for detecting button presses
Bounce button = Bounce();

void setup() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  // turn off leds initially
  int off = LOW;
  #ifdef COMMON_ANODE
    off = HIGH;
  #endif
  digitalWrite(RED_PIN, off);
  digitalWrite(GREEN_PIN, off);
  digitalWrite(BLUE_PIN, off);

  button.attach(BUTTON_PIN);
  button.interval(5);
  pinMode(BUTTON_PIN, INPUT);

  Serial.begin(115200);
  delay(10);

  Serial.println("Ready. Waiting for button press...");
}

/**
 * For some reason, when #include <ESP8266WiFi.h> is present, functions must be defined above the point 
 * where they are called. So start reading from the bottom.
 */

// Set rgb led to the specified color
void setColor(int red, int green, int blue) {
  #ifdef COMMON_ANODE
    red = 255 - red;
    green = 255 - green;
    blue = 255 - blue;
  #endif

  analogWrite(RED_PIN, red * RGB_LED_MULTIPLIER);
  analogWrite(GREEN_PIN, green * RGB_LED_MULTIPLIER);
  analogWrite(BLUE_PIN, blue * RGB_LED_MULTIPLIER);  
}

// turn off each color
void ledsOff() {
  int off = 0;
  #ifdef COMMON_ANODE
    off = 255;
    if (RGB_LED_MULTIPLIER == 4) {
      off = 1023; // set exact full value, because multiplier gives 1020
    }
  #endif
  
  analogWrite(RED_PIN, off);
  analogWrite(GREEN_PIN, off);
  analogWrite(BLUE_PIN, off);
}

// Pulse a led by gradually increasing and decreasing color values for a given number of times
void pulseColor(const int red, const int green, const int blue, const int times) {
  int currentRed = 0;
  int currentGreen = 0;
  int currentBlue = 0;
  int adder = 1;

  // loop until enough cycles done
  int directionChanges = 0;
  while (directionChanges < times * 2) {
    if ((adder > 0 && currentRed < red)
        || (adder < 0 && currentRed > 0)) {
      currentRed += adder;
    }
    if ((adder > 0 && currentGreen < green)
        || (adder < 0 && currentGreen > 0)) {
      currentGreen += adder;
    }
    if ((adder > 0 && currentBlue < blue)
        || (adder < 0 && currentBlue > 0)) {
      currentBlue += adder;
    }

    // check if need to change direction
    if ((adder < 0 && currentRed == 0 && currentGreen == 0 && currentBlue == 0)
        || (adder > 0 && currentRed == red && currentGreen == green && currentBlue == blue)) {
      adder = -adder;
      directionChanges++;
    }
    
    setColor(currentRed, currentGreen, currentBlue);
    
    delay(LED_DELAY);
  }

  ledsOff();
}

int updateColorAdder(const int currentColorAdder, const int colorValue) {
  // if light currently on, start decreasing it
  if (colorValue == 255) {
    return -1;
  } else if (currentColorAdder < 0) {
    return 0; // if it was previously decresing, keep it 0 for next cycle
  } else {
    return 1; // otherwise start increasing again
  }
}

// global variables for updateColors()
// keeping values in globals so that we can call it from loops inside other functions
int redVal = 0;
int greenVal = 0;
int blueVal = 255;
int redAdder = 0;
int greenAdder = 1;
int blueAdder = -1;

// change rgb led colors, call from a loop with small delay
void updateColors() {
  if ((redAdder > 0 && redVal < 255)
      || (redAdder < 0 && redVal > 0)) {
    redVal += redAdder;
  }
  if ((greenAdder > 0 && greenVal < 255)
      || (greenAdder < 0 && greenVal > 0)) {
    greenVal += greenAdder;
  }
  if ((blueAdder > 0 && blueVal < 255)
      || (blueAdder < 0 && blueVal > 0)) {
    blueVal += blueAdder;
  }

  setColor(redVal, greenVal, blueVal);

  // check if it's time to update adders
  if (redVal == 255 || greenVal == 255 || blueVal == 255) {
    redAdder = updateColorAdder(redAdder, redVal);
    greenAdder = updateColorAdder(greenAdder, greenVal);
    blueAdder = updateColorAdder(blueAdder, blueVal);
  }
}

// Connect to wifi and change led colors while connecting
boolean connect() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA); // station mode
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  long timeout = millis() + 30000; // 30s

  while (WiFi.status() != WL_CONNECTED) {
    // Fail after no connection during timeout
    if (millis() > timeout) {
      Serial.println("WiFi connection failed");  
      
      return false;
    }

    updateColors();
    delay(LED_DELAY);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  return true;
}

void disconnect() {
  WiFi.disconnect(true);
  Serial.println("WiFi disconnected");
}

String selectRandomMessage() {
  int index = rand() % MESSAGE_COUNT;

  return messages[index];
}

String createJson(const String message) {
  String json;
  StaticJsonBuffer<SLACK_JSON_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  
  root["channel"] = SLACK_CHANNEL;
  root["username"] = SLACK_USERNAME;
  root["icon_emoji"] = SLACK_ICON_EMOJI;
  root["text"] = message.c_str();
  root.printTo(json);

  return json;
}

String createRequest() {
  String json = createJson(
    selectRandomMessage()
  );

  String request = "";
  request += "POST ";
  request += SLACK_URL_PATH;
  request += " HTTP/1.1\r\n";

  request += "Host: ";
  request += SLACK_HOSTNAME;
  request += "\r\n";

  request += "Content-Length: ";
  request += json.length();
  request += "\r\n";

  request += "Accept: application/json\r\n";
  request += "Connection: close\r\n";
  request += "Content-Type: application/json\r\n";

  request += "\r\n";

  request += json;

  return request;
}

boolean sendNotification() {
  Serial.println("Connecting to host...");
  
  WiFiClientSecure client;
  
  if (!client.connect(SLACK_HOSTNAME, 443)) {
    Serial.println("Connection failed");
    client.stop();
    
    return false;
  }
  
  Serial.println("Connected to host");

  // Send a POST request to a Slack webhook
  String request = createRequest();
  
  Serial.println("Request:");
  Serial.print(request);
  Serial.println();
  
  client.print(request);

  long timeout = millis() + 5000;
  while (!client.available()) {
    if (millis() > timeout) {
      Serial.println("Request timed out");
      client.stop();
      
      return false;
    }

    updateColors();
    delay(LED_DELAY);
  }
  
  Serial.println("Response:");
  while (client.available()) {
    Serial.write(client.read());
  }
  
  Serial.println();
  Serial.println("Request complete");

  return true;
}

boolean notify() {
  Serial.println("Send notification");

  boolean connected = connect();
  
  if (!connected) {    
    return false;
  }

  return sendNotification();
}

boolean buttonPressed() {
  return button.update() && button.fell();
}

// the loop function runs over and over again forever
void loop() {
  if (buttonPressed()) {
    Serial.println("Button pressed");
    // pulse light-blue 3 times (as a visual feedback to the person who pressed the button)
    pulseColor(0, 255, 255, 3);
    ledsOff();
    
    Serial.println("Waiting for coffee to brew...");
    delay(COFFEE_BREW_DELAY);
    Serial.println("Coffee is ready");
    
    if (notify()) {
      Serial.println("Success");
      pulseColor(0, 255, 0, 5); // pulse green 5 times
    } else {
      Serial.println("Failure");
      pulseColor(255, 0, 0, 5); // pulse red 5 times
    }

    disconnect();

    ledsOff();
  }
}
