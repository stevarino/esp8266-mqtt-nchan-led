/*
 settings_sample.h - ESP8266 MQTT N-Channel LED Settings File
 Change the settings below and save as settings.h to include 
 in the project.
*/

/*****************************************
 * WiFi Settings
 ****************************************/
#define WLAN_SSID       "my_ssid"
#define WLAN_PASSWORD   "p@ssw0rd"

/*****************************************
 * MQTT Broker Settings
 ****************************************/
#define ARB_SERVER      "192.168.."
#define ARB_SERVERPORT  1883
#define ARB_USERNAME    "homeassistant"
#define ARB_PASSWORD    "api-password"

/*****************************************
 * LED settings.
 ****************************************/
// pins that LEDs are connected to
const byte pins[] = {12, 14};
// base mqtt topic, first led will be home/topic_name0 and
// home/topic_name0/set
String topic_base = "home/topic_name";
// if root topic is used, how to scale each strand.
const float root_scaling[] = {1, 0.5};

// default transition time in ms.
const float led_default_transition = 3000.0;
// curve applied to led scaling, 2.0 is quadratic, 0.5 is 
// square-rooted, 1.0 is linear
const float led_ramp_coefficient = 2.0;

// language settings
const char* on_cmd = "ON";
const char* off_cmd = "OFF";
