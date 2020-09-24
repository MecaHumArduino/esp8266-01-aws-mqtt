#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson (use v6.xx)
#include <time.h>

#define DEBUG true
#define emptyString String()

#include "secrets.h"

const int MQTT_PORT         = 8883;
const char MQTT_SUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/sub";
const char MQTT_PUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/pub";

uint8_t DST = 0;
WiFiClientSecure net;
SoftwareSerial UnoBoard(10, 11); // make RX Arduino line is pin 2, make TX Arduino line is pin 3.
                                 // This means that you need to connect the TX line from the esp to the Arduino's pin 2
                                 // and the RX line from the esp to the Arduino's pin 3

BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);

PubSubClient client(net);

unsigned long lastMillis = 0;
time_t now;
time_t nowish = 1510592825;

void NTPConnect(void)
{
  Serial.print("Setting time using SNTP");
  sendDataToUno("Setting time using SNTP\r\n", 1000, DEBUG);

  configTime(TIME_ZONE * 3600, DST * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);

  while (now < nowish) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println(" done!");
  sendDataToUno(" done!\r\n", 1000, DEBUG);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void messageReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");

  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  Serial.println();
}

void connectToMqtt(bool nonBlocking = false)
{
  Serial.print("MQTT connecting ");
  sendDataToUno("MQTT connecting \r\n", 1000, DEBUG);

  while (!client.connected()) {
    if (client.connect(THINGNAME)) {
      Serial.println("connected!");
      sendDataToUno("connected! \r\n", 1000, DEBUG);
      if (!client.subscribe(MQTT_SUB_TOPIC)) {
        Serial.println(client.state());
      }
    } else {
      Serial.print("failed, reason -> ");
      Serial.println(client.state());
      if (!nonBlocking) {
        Serial.println(" < try again in 5 seconds");
        delay(5000);
      } else {
        Serial.println(" <");
      }
    }
    if (nonBlocking) {
      break;
    }
  }
}

void connectToWiFi(String init_str)
{
  if (init_str != emptyString) {
    Serial.print(init_str);
  }
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  if (init_str != emptyString) {
    Serial.println(" ok!");
  }
}

void checkWiFiThenMQTT(void)
{
  connectToWiFi("Checking WiFi");
  sendDataToUno("Checking WiFi \r\n", 1000, DEBUG);

  connectToMqtt();
}

void sendDataToAWS(void)
{
  DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);

  JsonObject root           = jsonBuffer.to<JsonObject>();
  JsonObject state          = root.createNestedObject("state");
  JsonObject state_reported = state.createNestedObject("reported");

  // read data coming from Uno board

  state_reported["values"] = Serial.readStringUntil('\r\n');

  Serial.printf("Sending  [%s]: ", MQTT_PUB_TOPIC);
  serializeJson(root, Serial);

  char shadow[measureJson(root) + 1];

  serializeJson(root, shadow, sizeof(shadow));
  if (!client.publish(MQTT_PUB_TOPIC, shadow, false)) {
    Serial.println(client.state());
  }
}

String sendDataToUno(String command, const int timeout, boolean debug)
{
  String response = "";
  UnoBoard.print(command); // send the read character to the Uno
  long int time = millis();

  while( (time+timeout) > millis()) {
    while(UnoBoard.available()) {
      // The esp has data so display its output to the serial window
      char c = UnoBoard.read(); // read the next character.
      response+=c;
    }
  }

  if (debug) {
    Serial.print(response);
  }

  return response;
}


void setup()
{
  Serial.begin(9600);
  Serial.println("starting setup");

  UnoBoard.begin(9600); // your esp's baud rate might be different
  delay(5000);

  WiFi.hostname(THINGNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  connectToWiFi(String("Attempting to connect to SSID: ") + String(ssid));

  NTPConnect();

  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(messageReceived);

  connectToMqtt();
}

void loop()
{
  now = time(nullptr);
  if (!client.connected()) {
    checkWiFiThenMQTT();
  } else {
    client.loop();
    if (millis() - lastMillis > 5000) {
      lastMillis = millis();
      sendDataToAWS();
    }
  }
}