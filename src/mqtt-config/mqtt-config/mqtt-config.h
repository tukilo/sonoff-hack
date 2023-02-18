#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "mosquitto.h"
#include "config.h"
#include "sql.h"

#define MQTT_CONF_FILE    "/mnt/mmc/sonoff-hack/etc/mqtt-sonoff.conf"
#define CONF_FILE_PATH    "/mnt/mmc/sonoff-hack/etc"

#define SWITCH_ON_SCRIPT  "/mnt/mmc/sonoff-hack/script/privacy.sh"

typedef struct
{
    char       *user;
    char       *password;
    char        host[128];
    char        bind_address[128];
    int         port;
    char       *client_id;
    char       *mqtt_prefix_cmnd;
} mqtt_conf_t;

typedef struct
{
    char* topic;
    char* msg;
    int len;
} mqtt_msg_t;

void handle_signal(int s);
void connect_callback(struct mosquitto *mosq, void *obj, int result);
void disconnect_callback(struct mosquitto *mosq, void *obj, int result);
void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);
void handle_config(const char *key, const char *value);
int mqtt_init_conf(mqtt_conf_t *conf);
void mqtt_check_connection();
int mqtt_connect();
void stop_mqtt(void);