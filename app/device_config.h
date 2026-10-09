#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#define DEVICE_SERVER_IP        "192.168.0.115"
#define DEVICE_MQTT_PORT        1883U
#define DEVICE_HTTP_PORT        8080U

#define DEVICE_ID               "device-stm32-01"
#define DEVICE_MQTT_CLIENT_ID   "device-stm32-01"
#define DEVICE_MQTT_USERNAME    "mqtt-stm32-01"
#define DEVICE_MQTT_PASSWORD    "2cdf5526fad9ea2c2a61fa8aa41e7e9b8afc5b773905e2357bf3ce297771d781"
#define DEVICE_HTTP_TOKEN       "f13dab88887c2d3e838abb001af2c693708d7af211082ed7e1b32c49d513d3d5"

#define DEVICE_RESULT_TOPIC \
    "plate/devices/device-stm32-01/recognition-results"

#endif
