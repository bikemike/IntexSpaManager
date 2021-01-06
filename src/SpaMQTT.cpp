#include "SpaMQTT.h"
#include "Log.h"
extern Log logger;
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define mqtt_server "192.168.31.107"
#define mqtt_user "" //enter your MQTT username
#define mqtt_password "" //enter your password

#define topic_availability "availability"
#define topic_power "power"   // on/off
#define topic_heating_enabled "heating_enabled" // true/false
#define topic_heating "heating" // true/false
#define topic_filter "filter"  // on/off
#define topic_bubbles "bubbles" // on/off
#define topic_target_temp "target_temp"
#define topic_temp "temp"
#define topic_air_temp "air_temp"
#define topic_temp_units "temp_units"



SpaMQTT* SpaMQTT::self = nullptr;


SpaMQTT::SpaMQTT(class SpaState* state) : 
	spaState(state)
{
	self = this;

	mqttClient.setClient(wifiClient);
	mqttClient.setServer(mqtt_server, 1883); //CHANGE PORT HERE IF NEEDED
	mqttClient.setCallback(static_callback);

	if (spaState)
	{
		spaState->addListener(this);
	}
}


void SpaMQTT::handleSpaStateChange(const SpaState::ChangeEvent& c)
{
	if (!mqttClient.connected())
	{
		pendingChangeEvents.insert(c);
		return;
	}

	bool published = false;
	switch(c.getType())
	{
	case SpaState::ChangeEvent::CHANGE_TYPE_POWER:
		{
			published = mqttClient.publish((name+topic_power).c_str(), spaState->getPowerEnabled() ? "ON" : "OFF", true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_HEATING_ENABLED:
		{
			published = mqttClient.publish((name+topic_heating_enabled).c_str(), spaState->getHeatingEnabled() ? "TRUE" : "FALSE", true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_HEATING:
		{
			published = mqttClient.publish((name+topic_heating).c_str(), spaState->getIsHeating() ? "ON" : "OFF", true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_FILTER:
		{
			published = mqttClient.publish((name+topic_filter).c_str(), String(spaState->getFilterEnabled()) ? "ON" : "OFF", true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_BUBBLES:
		{
			published = mqttClient.publish((name+topic_bubbles).c_str(), spaState->getBubblesEnabled() ? "ON" : "OFF", true);
		}
		break;

	case SpaState::ChangeEvent::CHANGE_TYPE_TEMP:
		{
			published = mqttClient.publish((name+topic_temp).c_str(), String(spaState->getCurrentTemperature()).c_str(), true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_AIR_TEMP:
		{
			published = mqttClient.publish((name+topic_air_temp).c_str(), String(spaState->getExternalTemperature()).c_str(), true);
		}
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_TEMP_UNITS:
		{
			published = mqttClient.publish((name+topic_temp_units).c_str(), spaState->getIsTempInC() ? "C" : "F", true );
		}
		break;
	default:
		break;
	}
}


void SpaMQTT::loop()
{
	static uint32_t lastServiceTime = 0;
	static uint32_t lastServiceTimeMQTT = 0;

	uint32_t now = millis();
	if (now - lastServiceTime > 1000)
	{
		lastServiceTime = now;
		if (mqttClient.connected() && !pendingChangeEvents.empty())
		{
			SpaState::ChangeEvent c = *pendingChangeEvents.begin();
			pendingChangeEvents.erase(pendingChangeEvents.begin());
			handleSpaStateChange(c);
		}
	}


	if (!mqttClient.connected())
	{
		reconnect();
	}
	else
	{
		// slow down mqtt messages received to
		// limit number of messages to mcu
		if (now - lastServiceTimeMQTT > 25) 
		{
			lastServiceTimeMQTT = now;
			mqttClient.loop();
		}
	}

}

void SpaMQTT::reconnect()
{
	static uint32_t lastConnectAttempt = 0;

	uint32_t now = millis();

	if (now - lastConnectAttempt > 1000)
	{
		lastConnectAttempt = now;
		// Loop until we're reconnected
		if (!mqttClient.connected())
		{
			String s = "MQTT Connect...";

			// Attempt to connect
			if (mqttClient.connect(
				name.c_str(), 
				mqtt_user, mqtt_password,
				(name+topic_availability).c_str(),
				0, true, "offline"))
			{
				s += "connected";
				mqttClient.publish((name+topic_availability).c_str(),"online", true);
				subscribe();
			}
			else
			{
				s += "fail, rc=";
				s += String(mqttClient.state());
			}   

			logger.addLine(s);
		}
	}
}


void SpaMQTT::static_callback(char* topic, byte* payload, unsigned int length)
{
	if (self)
		self->callback(topic,payload,length);
}

void SpaMQTT::subscribe()
{
	mqttClient.subscribe((name + topic_power"/set").c_str());
	mqttClient.subscribe((name + topic_heating_enabled"/set").c_str());
	mqttClient.subscribe((name + topic_filter"/set").c_str());
	mqttClient.subscribe((name + topic_bubbles"/set").c_str());
	mqttClient.subscribe((name + topic_target_temp"/set").c_str());
	mqttClient.subscribe((name + topic_temp_units"/set").c_str());
}

void SpaMQTT::callback(char* topic_, byte* payload, unsigned int length)
{
	char msg_buff[64];
	
	if (length >= 63)
		return;


	memcpy(msg_buff, payload, length);
	msg_buff[length] = 0;
	String msg(msg_buff);
	String topic(topic_);

	logger.addLine(String("MQTT: ") + topic + ": " + msg);


	if (topic.endsWith(topic_power"/set"))
	{
		if (msg.equalsIgnoreCase("on"))
			spaState->setPowerEnabled(true);
		else if (msg.equalsIgnoreCase("off"))
			spaState->setPowerEnabled(false);
	}
	else if (topic.endsWith(topic_heating_enabled"/set"))
	{
		if (msg.equalsIgnoreCase("true"))
			spaState->setHeatingEnabled(true);
		else if (msg.equalsIgnoreCase("false"))
			spaState->setHeatingEnabled(false);
	}
	else if (topic.endsWith(topic_filter"/set"))
	{
		if (msg.equalsIgnoreCase("on"))
			spaState->setFilterEnabled(true);
		else if (msg.equalsIgnoreCase("off"))
			spaState->setFilterEnabled(false);
	}
	else if (topic.endsWith(topic_bubbles"/set"))
	{
		if (msg.equalsIgnoreCase("on"))
			spaState->setBubblesEnabled(true);
		else if (msg.equalsIgnoreCase("off"))
			spaState->setBubblesEnabled(false);
	}
	
	else if (topic.endsWith(topic_target_temp"/set"))
	{
		spaState->setTargetTemperature(msg.toInt());
	}
}




