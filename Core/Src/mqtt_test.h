
#ifndef MQTT_EXAMPLE_H
#define MQTT_EXAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

mqtt_client_t * mqtt_example_init(void);
static void
mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);

#ifdef __cplusplus
}
#endif
#endif
