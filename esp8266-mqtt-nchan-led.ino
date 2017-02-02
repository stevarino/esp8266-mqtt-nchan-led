/*
   ESP8266 MQTT Multiple Channel Lights for Home Assistant

   Allows multiple LED strands to be controlled using an ESP8266
   and MQTT broker (Home Assistant). 
   
   Based on code from:
     https://github.com/corbanmailloux/esp-mqtt-rgb-led
*/

// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

// http://pubsubclient.knolleary.net/
#include <PubSubClient.h>
#include "settings.h"

// array management
const int pin_count = sizeof(pins)==0 ? 0 : sizeof(pins) / sizeof(pins[0]);
const int scale_length = sizeof(base_scaling)==0 ? 0 : sizeof(base_scaling) / sizeof(base_scaling[0]);

String client_id;
const int BUFFER_SIZE = JSON_OBJECT_SIZE(8);

typedef struct LED {
  byte index;               // the index of this led (0, 1...)
  bool is_on;               // true if on
  byte brightness;          // 0-255 brightness as ordered

  float target;             // physical command (is_on * brightness)
  float step;               // how much to adjust brightness by per loop
  float current;            // current state

  String topic_state;       // mqtt topic for LED state
  String topic_set;         // mqtt topic to set LED
} LED;

LED leds[pin_count];

WiFiClient espClient;
byte wifi_mac[6] = {0, 0, 0, 0, 0, 0};
PubSubClient client(espClient);

ulong millis_prev;
const int millis_step = 50;

/**
 * Setup function - runs once.
 */
void setup() {
  Serial.begin(115200);
  Serial.printf("Setting up %n pins.\n", pin_count);
  for (int i = 0; i < pin_count; i++) {
    leds[i].index = i;
    leds[i].is_on = false;
    leds[i].brightness = 255;

    leds[i].current = 0;
    leds[i].target = 0;
    leds[i].step = 0;

    leds[i].topic_state = topic_base + String(i);
    leds[i].topic_set = topic_base + String(i) + "/set";

    pinMode(pins[i], OUTPUT);
  }

  analogWriteRange(255);

  setup_wifi();
  WiFi.macAddress(wifi_mac);
  client_id = "ESP8266_" + bytes_to_string(wifi_mac, 6, "");
  Serial.println("MAC Address: " + bytes_to_string(wifi_mac, 6, ":"));
  client.setServer(ARB_SERVER, ARB_SERVERPORT);
  client.setCallback(mqtt_callback);
}

/**
 * Loop function - runs repeatedly.
 */
void loop() {
  if (!client.connected()) {
    mqtt_reconnect();
  }
  client.loop();

  led_loop();
}

/**
 * Esatblishes WiFi connection. Called in setup()
 */
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

/*
  SAMPLE PAYLOAD:
  {
    "brightness": 120,
    "transition": 5,
    "state": "ON"
  }
*/

/**
 * Recveive a message from the MQTT library and parse it.
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!isDigit(topic[topic_base.length()]) && topic[topic_base.length()] != '/') {
    Serial.println("ERROR: Topic does not include an index.");
    return;
  }

  String target = String(topic).substring(topic_base.length());
  int i = -1; // all LEDs by default
  if (isDigit(target[0])) {
    i = String(topic).substring(topic_base.length()).toInt();
  }
  Serial.println("Targeting index " + String(i));

  if (i > pin_count) {
    Serial.println("ERROR: Requested index exceeds configured pins.");
    return;
  }

  if (!mqtt_process(message, i)) {
    Serial.println("ERROR: Unable to parse json.");
    return;
  }
}

/**
 * Process individual settings from an MQTT message.
 */
bool mqtt_process(char* message, int i) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& json = jsonBuffer.parseObject(message);

  if (!json.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  int led_start = i;
  int led_end = i+1;
  
  float scale = 1.0;
  
  bool is_base_command = (i == -1);
  if (is_base_command) {
    led_start = 0;
    led_end = pin_count;
  }

  for (int j=led_start; j<led_end; j++) {
      
    if (json.containsKey("state")) {
      leds[j].is_on = (strcmp(json["state"], on_cmd) == 0);
    }
  
    if (json.containsKey("brightness")) {
      scale = 1.0;
      if (is_base_command && scale_length > j) {
        scale = base_scaling[j];
      }
      leds[j].brightness = scale * (int)json["brightness"];
    }
  
    leds[j].target = leds[j].brightness * leds[j].is_on;
  
    if (json.containsKey("transition")) {
      leds[j].step = (leds[j].target - leds[j].current) / (((int)json["transition"])*1000.0 / millis_step);
    } else if (led_default_transition > 0) {
      leds[j].step = (leds[j].target - leds[j].current) / (led_default_transition / millis_step);
    } else {
      leds[j].step = 255;
    }
  
    Serial.printf("LED: %d; Is On: %d; Target: %d; Current: %d; Step: %d\n",
                  leds[j].index, leds[j].is_on, (int)leds[j].target, (int)leds[j].current, (int)leds[j].step);
                  
    mqtt_respond(&leds[j]);
  }
  mqtt_respond_base();

  return true;
}

/**
 * Send a single LED status back to the MQTT broker.
 */
void mqtt_respond(struct LED *led) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& json = jsonBuffer.createObject();

  json["state"] = led->is_on ? on_cmd : off_cmd;

  json["brightness"] = led->brightness;

  char buffer[json.measureLength() + 1];
  json.printTo(buffer, sizeof(buffer));

  
  Serial.print("Message sent [");
  Serial.print(led->topic_state);
  Serial.print("] {");
  Serial.print(buffer);
  Serial.println("}");
  client.publish(led->topic_state.c_str(), buffer, true);
}

/**
 * Send the combined LED status back to the MQTT broker.
 */
void mqtt_respond_base() {
  struct LED r;
  r.is_on = false;;
  r.brightness = 0;
  r.topic_state = topic_base;

  float scale = 1.0;
  for (int i=0; i<pin_count; i++) {
    r.is_on = r.is_on || leds[i].is_on;
    
    scale = 1.0;
    if (i < scale_length) {
      scale = base_scaling[i];
    }
    r.brightness = _max(r.brightness, leds[i].is_on * leds[i].brightness / scale);
  }

  mqtt_respond(&r);
}

/**
 * Connect to the MQTT broker.
 */
void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection as " + client_id + "... ");
    // Attempt to connect
    if (client.connect(client_id.c_str(), ARB_USERNAME, ARB_PASSWORD)) {
      Serial.println("connected");
      client.subscribe((topic_base + "/set").c_str());
      Serial.println("Registered base topic");
      for (int i = 0; i < pin_count; i++) {
        client.subscribe(leds[i].topic_set.c_str());
        Serial.println("Registered topic " + leds[i].topic_state + " on pin " + String(pins[i]));
      }

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/**
 * Determine if iteration needs to be performed on an LED.
 */
void led_loop() {
  unsigned long mil = millis();
  // first run or rollover.
  if (millis_prev == 0 || millis_prev > mil) {
    millis_prev = mil;
    return;
  }

  if ((mil - millis_prev) > millis_step) {
    for (int i = 0; i < pin_count; i++) {
      if (leds[i].step != 0) {
        led_step(&leds[i]);
      }
      analogWrite(pins[i], 255 * pow(leds[i].current/255.0, led_ramp_coefficient));
    }
    millis_prev = mil;
  }
}

/**
 * Perform iteration on an LED.
 */
void led_step(struct LED *led) { 
  if ((led->target > led->current && led->step < 0) ||
      (led->target < led->current && led->step > 0)) {
    led->step = -1 * led->step;
  }
  if (abs(led->current - led->target) <= abs(led->step)) {
    led->step = 0;
    led->current = led->target;
  }
  led->current = led->current + led->step;
}


/**
 * General library function.
 * Converts an array of bytes ({253, 12, 4}) to a string ("FD0C04")
 */
String bytes_to_string(byte* b, int n, String sep) {
  int sep_size = sep.length();

  char c[2 * n + (n - 1)*sep_size + 1];
  c[2 * n + (n - 1)*sep_size] = 0;

  for (int i = 0; i < n; i++) {
    if (i > 0) {
      for (int j = 0; j < sep_size; j++) {
        c[(i - 1) * (2 + sep_size) + 2 + j] = sep[j];
      }
    }
    c[i * (2 + sep_size)] = byte_to_char(b[i] >> 4);
    c[i * (2 + sep_size) + 1] = byte_to_char(b[i] & 15);
  }
  return String(c);
}

/**
 * General library function
 * Converts 4 bits to a char (0-9,A-F).
 */
char byte_to_char(byte b) {
  char c = b & 15;
  if (c > 9) {
    return c + 55;  // A-F
  }
  return c += 48;   // 0-9
}
