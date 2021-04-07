#include "Webserver.h"
//#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>


#include "SpaState.h"

#include "Log.h"

extern Log logger;

void Webserver::init(SpaState* state_, String devName)
{
	state = state_;
	deviceName = devName;
	server = new ESP8266WebServer(80);
	httpUpdater = new ESP8266HTTPUpdateServer();

	server->on("/", HTTP_GET,std::bind(&Webserver::handleRoot, this));
	server->on("/console", HTTP_GET,std::bind(&Webserver::handleConsole, this));
	server->on("/console", HTTP_POST,std::bind(&Webserver::handleConsole, this));
	server->on("/restart", HTTP_GET, std::bind(&Webserver::handleRestart, this)); 
	server->onNotFound(std::bind(&Webserver::handleRoot, this));
	server->begin();

	httpUpdater->setup(server);
	state->addListener(this);
}

void Webserver::handleRoot()
{

	String msg("<html><head><title>Wifi Spa</title></head><body>");
	msg += deviceName;
	msg += " State:<br>";
	msg += "<pre>";
	msg += state->toString();
	msg += "</pre>";
	
	msg += "<a href=\"/update\">Update firmware</a>";
	msg += String("</body></html>");
	server->send(200, "text/html", msg);
}

void Webserver::handleNotFound()
{
	//if (!handleFileRead(server->uri()))
		server->send(404, "text/plain", "Not Found.");
}

void Webserver::handleConsole()
{
	if (server->hasArg("cmd"))
    {
		String cmd = server->arg("cmd");
		cmd.trim();
		logger.addLine("Got a post cmd: " + cmd);
		if (cmd == "units")
		{
			state->setTempInC(!state->getIsTempInC());
		}
		else if(cmd == "power")
		{
			state->setPowerEnabled(!state->getPowerEnabled());
		}
		else if (cmd == "filter")
		{
			state->setFilterEnabled(!state->getFilterEnabled());
		}
		else if (cmd == "heating")
		{
			state->setHeatingEnabled(!state->getHeatingEnabled());
		}
		else if (cmd == "bubbles")
		{
			state->setBubblesEnabled(!state->getBubblesEnabled());
		}
		else if (cmd.startsWith("settemp="))
		{
			int temperature = cmd.substring(8).toInt();
			state->setTargetTemperature(temperature);
		}
		else if (cmd == "test")
		{
			state->startStopTest("");
		}
		else if (cmd.startsWith("test="))
		{
			String testType =  cmd.substring(5);
			state->startStopTest(testType);
		}
	}
	String msg("<html><head><title>Wifi Spa</title>");
	msg += "<script>";
	msg += "function sb(){ var c = document.getElementById('console');c.scrollTop = c.scrollHeight;}";
	msg += "</script></head>";
	msg += "<body onload='sb();'>";
	msg += "Console:<br>";
	msg += "<form method='POST'>";
	msg += "<textarea id='console' style='width:100%;height:calc(100% - 50px);'>";
	msg += logger.getLines();
	msg += "</textarea>";
	msg += "<br/>";
	msg += "<input type='text' name='cmd' style='width:calc(100% - 100px);'></input><input type=submit value='send' style='width:100px;'></input>";
	msg += "</form>";
	msg += "</body></html>";
	server->send(200, "text/html", msg);
}

void Webserver::handleRestart()
{
	ESP.restart();
	delay(10000);
}

void Webserver::start()
{
	server->begin();
}

void Webserver::stop()
{
	server->stop();
}


void Webserver::process()
{
	server->handleClient();
}

void Webserver::handleSpaStateChange(const SpaState::ChangeEvent& c)
{
	/*
	switch(c.getType())
	{
	case SpaState::ChangeEvent::CHANGE_TYPE_POWER:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_MODE:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_ECONOMY:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_SETPOINT_TEMP:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_INTERNAL_TEMP:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_EXTERNAL_TEMP:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_IS_HEATING:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_SCHEDULE:
		break;
	case SpaState::ChangeEvent::CHANGE_TYPE_LOCK:
		break;
	default:
		break;
	}*/
}


