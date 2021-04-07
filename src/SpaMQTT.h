#ifndef MQTT_H
#define MQTT_H

#include "SpaState.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

class SpaMQTT : public SpaState::Listener
{
public:
	SpaMQTT(SpaState* state);

	virtual void handleSpaStateChange(const SpaState::ChangeEvent& c) override;
	void setName(String n) { name = n + "/"; } 
	void loop();
	void reconnect();

	void sendHAMode();
	void sendHAAction();
	void sendHAFanMode();


	static void static_callback(char* topic, byte* payload, unsigned int length);
	void callback(char* topic, byte* payload, unsigned int length);

	static SpaMQTT* self;

private:
	void subscribe();
private:
	String name = String("default/");
	SpaState* spaState;
	WiFiClient wifiClient;
	PubSubClient mqttClient;
	std::set<SpaState::ChangeEvent> pendingChangeEvents;

};

#endif // MQTT_H