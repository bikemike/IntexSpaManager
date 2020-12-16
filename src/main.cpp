#include <Arduino.h>

#include <ESP8266WiFi.h> 
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <LittleFS.h> // LittleFS is declared
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <TZ.h>

// time includes
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>                  // settimeofday_cb()

#include <set>


#include "SpaState.h"

#include "Webserver.h"
#include "Log.h"
#include "SpaMQTT.h"

WiFiManager wifiManager;

SpaState state;

SpaMQTT spaMQTT(&state);
Webserver webserver;
Log logger;
bool relayOn = false;

const int PIN_CLK     = D7; 
const int PIN_LAT     = D6; 
const int PIN_DAT_IN  = D5; 
const int PIN_DAT_OUT = D0; //emulate button press      


#define TIMEZONE 	TZ_America_Vancouver

static void setupOTA();
static void initTime();


struct Config
{
	char deviceName[64] = {0};
};

Config config;


static void saveConfig()
{
	const char* filename = "/config.json";
	File file = LittleFS.open(filename, "w");
	bool saved = false;
	if (file)
	{
		StaticJsonDocument<256> doc;
		doc["deviceName"] = config.deviceName;
		size_t bytes_written = serializeJson(doc, file);
		saved = (0 != bytes_written);
		file.close();

	}
	if (!saved)
		logger.addLine("ERROR: unable to save /config.json");
}

static void loadConfig()
{
	String devName = String("IntexSpa-") + String(ESP.getChipId(),HEX);
	const char* filename = "/config.json";
	bool loaded = false;

	if (LittleFS.exists(filename))
	{
		File file = LittleFS.open(filename, "r");

		StaticJsonDocument<512> doc;

		DeserializationError error = deserializeJson(doc, file);
		if (DeserializationError::Ok == error)
		{
			strlcpy(
				config.deviceName,
				doc["deviceName"] | devName.c_str(),
				sizeof(config.deviceName));
			file.close();
			loaded = true;
		}
		
	}
	if (!loaded)
	{
		logger.addLine("ERROR: unable to load /config.json");

		strlcpy(
			config.deviceName,
			devName.c_str(),
			sizeof(config.deviceName));

		saveConfig();
	}
}


void setup()
{
	LittleFS.begin();
	loadConfig();
	//strlcpy(config.deviceName, "MoesThermostat",16);
	//saveConfig();

	spaMQTT.setName(config.deviceName);
	ArduinoOTA.setHostname(config.deviceName);
	WiFi.hostname(config.deviceName);
	WiFi.enableAP(false);
	WiFi.enableSTA(true);
	WiFi.begin();
	wifiManager.setConfigPortalTimeout(120);
	wifiManager.setDebugOutput(false);
#ifdef SERIAL_DEBUG
	Serial.begin(115200);
#endif
	state.setWifiConfigCallback([]() {
		logger.addLine("Configuration portal opened");
		webserver.stop();
        wifiManager.startConfigPortal(config.deviceName);
		webserver.start();
		WiFi.enableAP(false);
		logger.addLine("Configuration portal closed");
    });


	webserver.init(&state, config.deviceName);
	setupOTA();

	MDNS.begin(config.deviceName);
	MDNS.addService("http", "tcp", 80);

	initTime();

	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(PIN_DAT_OUT, HIGH);
	state.init(PIN_CLK, PIN_LAT, PIN_DAT_IN, PIN_DAT_OUT);
}

timeval cbtime;			// when time set callback was called
int cbtime_set = 0;
static void timeSet()
{
	gettimeofday(&cbtime, NULL);
	cbtime_set++;

	time_t tnow = time(nullptr);
	struct tm* new_time = nullptr;
	new_time = localtime(&tnow);
	char stbuff[32] = {0};
	strftime(stbuff, 32, "%F %T", new_time);

	logger.addLine(stbuff);
	#ifdef SERIAL_DEBUG
	Serial.println("Time set from NTP");
	Serial.println(stbuff);
	#endif
}

void checkWiFiConnection()
{
  static uint32_t nextRetry = 30000;
  static wl_status_t lastWiFiStatus = WiFi.status();
  wl_status_t newWiFiStatus = WiFi.status();
  static uint32_t timeLastChange = 0;
  uint32_t timeNow = millis();
  // if wifi status hasn't changed in 60 seconds and it's not WL_CONNECTED
  // try to reconnect
  if (lastWiFiStatus != newWiFiStatus)
  {
	logger.addLine("IP Address: " + WiFi.localIP().toString());
    lastWiFiStatus = newWiFiStatus;
    timeLastChange = timeNow;
  }
  else if (timeNow - timeLastChange > nextRetry && WL_CONNECTED != newWiFiStatus)
  {
    String wifiInfo;
    switch (newWiFiStatus)
    {
    case WL_NO_SHIELD:
      wifiInfo = "WL_NO_SHIELD";
      break;
    case WL_IDLE_STATUS:
      wifiInfo = "WL_IDLE_STATUS";
      break;
    case WL_NO_SSID_AVAIL:
      wifiInfo = "WL_NO_SSID_AVAIL";
      break;
    case WL_DISCONNECTED:
      wifiInfo = "WL_DISCONNECTED";
      break;
    case WL_CONNECTION_LOST:
      wifiInfo = "WL_CONNECTION_LOST";
      break;
    case WL_CONNECT_FAILED:
      wifiInfo = "WL_CONNECT_FAILED";
      break;
    case WL_SCAN_COMPLETED:
      wifiInfo = "WL_SCAN_COMPLETED";
      break;
    case WL_CONNECTED:
      wifiInfo = "WL_CONNECTED";
      break;
    }
    wifiInfo = "WiFi has been stuck in " + wifiInfo + " for more than " + String(nextRetry/1000) + " second(s), attempting reconnect...";
    
	logger.addLine(wifiInfo);
    
    WiFi.reconnect();
    timeLastChange = timeNow;

    nextRetry = nextRetry * 2;
    if (nextRetry >= 960000)
      nextRetry = 60000;
  }
}


void loop()
{
	checkWiFiConnection();

	state.setTimeAvailable(cbtime_set > 0);
	state.loop();

	webserver.process();
	ArduinoOTA.handle();
	MDNS.update();
	spaMQTT.loop();

	// led blink
	static uint32_t lastBlink = 0;
	static uint32_t onTime = 1000;
	uint32_t timeNow = millis();
	if (timeNow - lastBlink > onTime)
	{
		if (digitalRead(LED_BUILTIN))
		{
			onTime = 200;
			digitalWrite(LED_BUILTIN, LOW);
		}
		else
		{
			onTime = 800;
			digitalWrite(LED_BUILTIN, HIGH);
		}
		lastBlink = timeNow;
	}
}

static void setupOTA()
{
	ArduinoOTA.onStart([]() 
	{
		logger.addLine("Disabling interrupts");
		state.disableInterrupts();
	});
	ArduinoOTA.onEnd([]()
	{
	});

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
	{
	});
	
	ArduinoOTA.onError([](ota_error_t error)
	{
		/*
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
		*/
	});
	ArduinoOTA.begin();
}



static void initTime()
{
	settimeofday_cb(timeSet);
	configTime(TIMEZONE, "pool.ntp.org"); // check TZ.h, find your location
}

