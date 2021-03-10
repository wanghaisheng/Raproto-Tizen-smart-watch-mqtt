/*
 * mqtt.c
 *
 *  Created on: Feb 14, 2021
 *      Author: weimer
 */


#include "mqtt.h"


static void
mqtt_warn(int err, char const *caller_name, char const *id, app_data_s *ad){
	warn_msg(err, caller_name, id);
	task_warn(RAPROTO_ERROR_MSG_MQTT, ad);
	mqtt_destroy(ad);
}


void
mqtt_destroy(app_data_s *ad){

	if (ad->mqtt.client != NULL) {
		MQTTAsync_destroy(ad->mqtt.client);
		free(ad->mqtt.client);
		ad->mqtt.client = NULL;
	}

	task_process_cb(RAPROTO_TASK_MQTT_DESTROY_DONE);
	return;
}



static void
mqtt_stop_failure_cb(void* context, MQTTAsync_failureData* response){
	app_data_s *ad = (app_data_s*)context;
	mqtt_warn(0, __func__, "mqtt stop failed", ad);
	return;
}


static void
mqtt_stop_success_cb(void* context, MQTTAsync_successData* response){
	//app_data_s *ad = (app_data_s*)context;
	task_process_cb(RAPROTO_TASK_MQTT_OFF_DONE);
	return;
}


void
mqtt_stop(app_data_s *ad){
	int err;

	MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
	opts.onSuccess = mqtt_stop_success_cb;
	opts.onFailure = mqtt_stop_failure_cb;
	opts.timeout = 10000;
	opts.context = ad;

	if ((err = MQTTAsync_disconnect(*(ad->mqtt.client), &opts)) != MQTTASYNC_SUCCESS) return mqtt_warn(err, __func__, "mqtt disconnect", ad);

	return;
}



static int
mqtt_message_received(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
	app_data_s *ad = (app_data_s*)context;
	char *payload = message->payload;
	int err;

	dlog_print(DLOG_INFO, LOG_TAG, "%s : %d, %s", topicName, message->payloadlen, payload);

	char *config;
	if ((err = bundle_get_str(ad->settings, RAPROTO_SETTING_MQTT_CONFIG_SUB_TOPIC, &config)) != BUNDLE_ERROR_NONE) {
		mqtt_warn(err, __func__, "get string", ad);
		return 0;
	}

	if (!strncmp(topicName, config, topicLen-1)) {
		config_received(ad, message->payload, message->payloadlen);
	}

	return 1;
}


static void
mqtt_delivered(void *context, MQTTAsync_token dt)
{
	//dlog_print(DLOG_INFO, LOG_TAG, "Message delivered! %d",dt);
	return;
}



static void
mqtt_connection_lost(void *context, char *cause) {
	app_data_s *ad = (app_data_s*)context;
	mqtt_warn(0, __func__, "connection lost", ad);
}




static void
mqtt_start_success_cb(void* context, MQTTAsync_successData* response){
	task_process_cb(RAPROTO_TASK_MQTT_ON_DONE);
}



static void
mqtt_start_failure_cb(void* context, MQTTAsync_failureData* response){
	app_data_s *ad = (app_data_s*)context;
	mqtt_warn(0, __func__, "start failed", ad);
}



void
mqtt_start(app_data_s *ad){
	int err;
	wifi_deinitialize(ad);

	if (ad->mqtt.client != NULL) return mqtt_warn(0, __func__, "client not Null", ad);

	// create a new manager
	ad->mqtt.client = (MQTTAsync*)malloc(sizeof(MQTTAsync));

	// load settings
	char *broker, *client_id, *username, *password;
	int *keep_alive;
	size_t n_size;
	bundle_get_str(ad->settings, RAPROTO_SETTING_MQTT_BROKER, &broker);
	bundle_get_str(ad->settings, RAPROTO_SETTING_MQTT_CLIENT_ID, &client_id);
	bundle_get_str(ad->settings, RAPROTO_SETTING_MQTT_USERNAME, &username);
	bundle_get_str(ad->settings, RAPROTO_SETTING_MQTT_PASSWORD, &password);
	bundle_get_byte(ad->settings, RAPROTO_SETTING_MQTT_KEEP_ALIVE, (void**)&keep_alive, &n_size);

	if ((err = MQTTAsync_create(ad->mqtt.client, broker, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTASYNC_SUCCESS) return mqtt_warn(err, __func__, "MQTT create", ad);

    if ((err = MQTTAsync_setCallbacks(*(ad->mqtt.client), ad, mqtt_connection_lost, mqtt_message_received, mqtt_delivered)) != MQTTASYNC_SUCCESS) return mqtt_warn(err, __func__, "MQTT callbacks", ad);


	MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
	opts.keepAliveInterval = *keep_alive;
	opts.cleansession = 0;
	opts.username = username;
	opts.password = password;
	opts.connectTimeout = 10;
	opts.retryInterval = 0;
	opts.onSuccess = mqtt_start_success_cb;
	opts.onFailure = mqtt_start_failure_cb;
	opts.context = ad;

	MQTTAsync_SSLOptions sslopts = MQTTAsync_SSLOptions_initializer;
	opts.ssl = &sslopts;
	opts.ssl->enableServerCertAuth = 0;


	if ((err = MQTTAsync_connect(*(ad->mqtt.client), &opts)) != MQTTASYNC_SUCCESS) return mqtt_warn(err, __func__, "MQTT connect", ad);

	return;
}



