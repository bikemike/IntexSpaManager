#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "SpaState.h"


class Webserver : public SpaState::Listener
{
public:
	Webserver() {}
	void init(class SpaState* state, String deviceName);
	void stop();
	void start();

	void handleRoot();
	void handleNotFound();
	void handleConsole();
	void handleRestart();
	void process();

	virtual void handleSpaStateChange(const SpaState::ChangeEvent& c) override;

private:
	String deviceName;
	ESP8266WebServer* server = nullptr;
	SpaState* state = nullptr;
	ESP8266HTTPUpdateServer* httpUpdater = nullptr;

};
#endif
