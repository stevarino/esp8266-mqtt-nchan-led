# esp8266-mqtt-nchan-led
Multi-Channel LED controllable over MQTT for ESP8266 devices

Allows multiple LED strands to be controlled using an ESP8266 and MQTT broker (Home Assistant). 
   
Based on code from: https://github.com/corbanmailloux/esp-mqtt-rgb-led

Features include:

 - Single settings file.
 - Non-linear LED scaling.
 - Grouped command channel with independant scaling.
 - Default transition time length if not specified.