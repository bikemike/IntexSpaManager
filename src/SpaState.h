#ifndef SPA_STATE_H
#define SPA_STATE_H

#include <Arduino.h>
#include <set>
#include <sys/time.h>


class MessageInterface
{
public:
	virtual ~MessageInterface() {}
	virtual const uint8_t* getBytes() const = 0;
	virtual uint32_t getLength() const = 0;
	virtual bool isValid() const = 0;
};


class SpaState
{
public:
	SpaState() {}

	void init(uint8_t clockPin, uint8_t latchPin, uint8_t dataInPin, uint8_t dataOutPin);
	void disableInterrupts();
	void loop();
	ICACHE_RAM_ATTR void handleClockInterrupt();
	ICACHE_RAM_ATTR void handleLatchInterrupt();

	void setTimeAvailable(bool available) { timeAvailable = available; }
	bool getTimeAvailable() const { return timeAvailable; }

	typedef std::function<void()> WifiConfigCallback;
	void setWifiConfigCallback(WifiConfigCallback cb)
	{
		wifiConfigCallback = cb;
	}

	bool getPowerEnabled() const;
	void setPowerEnabled(bool power);

	bool getFilterEnabled();
	void setFilterEnabled(bool newValue);

	bool getIsHeating() const;

	bool getHeatingEnabled() const;
	void setHeatingEnabled(bool newValue);

	bool getBubblesEnabled() const;
	void setBubblesEnabled(bool newValue);

	bool getIsTempInC() const;
	bool getIsTempInF() const;
	String getTemperatureUnitString() const { return( celsius ? "C" : "F" );}

	void setTempInC(bool c);

	int getCurrentTemperature() const;

	int getTargetTemperature() const;
	void setTargetTemperature(int newValue);

	String toString()
	{
		String str;
		str += "Power: ";
		if (getPowerEnabled())
			str  += "ON\n";
		else
			str += "OFF\n";
		
		str += "Heating Enabled: ";
		if (getHeatingEnabled())
			str  += "YES\n";
		else
			str += "NO\n";

		str += "Heating: ";
		if (getIsHeating())
			str  += "ON\n";
		else
			str += "OFF\n";

		str += "Filter: ";
		if (getFilterEnabled())
			str  += "ON\n";
		else
			str += "OFF\n";

		str += "Bubbles: ";
		if (getBubblesEnabled())
			str  += "ON\n";
		else
			str += "OFF\n";

		str +=
		    String("TargetTemp: ") + String(getTargetTemperature()) + getTemperatureUnitString() + "\n" +
		    String("Temp: ") + String(getCurrentTemperature()) + getTemperatureUnitString() +
			String("\n\n");
		return str;
	}

	class ChangeEvent
	{
	public:
		enum ChangeType
		{
			CHANGE_TYPE_NONE,
			CHANGE_TYPE_POWER,
			CHANGE_TYPE_HEATING,
			CHANGE_TYPE_FILTERING,
			CHANGE_TYPE_BUBBLES,
			CHANGE_TYPE_SETPOINT_TEMP,
			CHANGE_TYPE_TEMP,
		};
	public:
		ChangeEvent(ChangeType t) : type (t) {}
		ChangeType getType() const { return type; }

		bool operator==(const ChangeEvent& other) const
		{
			return type == other.type;
		}
		bool operator<(const ChangeEvent& other) const
		{
			return type < other.type;
		}
	private:
		ChangeType type = CHANGE_TYPE_NONE;
	};

	class Listener
	{
	public:
		virtual ~Listener(){}
		virtual void handleSpaStateChange(const ChangeEvent& c) = 0;
	};

	void addListener(Listener* listener)
	{
		if (listener)
			listeners.insert(listener);
	}

protected:


	virtual void emitChange(const ChangeEvent& c)
	{
		for(auto l : listeners)
		{
			l->handleSpaStateChange(c);
		}
	};


	bool getTime(int &dayOfWeek, int &hour, int &minutes)
	{
		bool gotTime = false;
		struct tm* new_time = nullptr;
		if (getTimeAvailable())
		{
			struct timezone tz = {0};
			timeval tv = {0};
			
			gettimeofday(&tv, &tz);
			time_t tnow = time(nullptr);

			new_time = localtime(&tnow);
			// sunday = 0, sat = 6
			dayOfWeek = new_time->tm_wday;
			hour = new_time->tm_hour;
			minutes = new_time->tm_min;
			
			gotTime = true;
		}
		return gotTime;
	}


enum ButtonT {
  BTN_POWER  = 0,
  BTN_UP     = 1,
  BTN_DOWN   = 2,
  BTN_FILTER = 3,
  BTN_HEATER = 4,
  BTN_BUBBLE = 5,
  BTN_FC     = 6
  
};


	void readSegment(uint16_t msg, int seg);
	void readLEDStates(uint16_t msg);
	void classifyTemperature();
	void writeButton(ButtonT button);
	void handleButton(ButtonT button);
	inline ICACHE_RAM_ATTR bool simulateButtonPress();

private:
	std::set<Listener*> listeners;
	WifiConfigCallback wifiConfigCallback;
	bool timeAvailable = false;

	uint8_t pinClock = 0;
	uint8_t pinLatch = 0;
	uint8_t pinDataIn = 0;
	uint8_t pinDataOut = 0;

	char digit[5] = {};
	int  lstTemp = 0;                 //last valid display reading
	int  curTempTmp = 0;              //current temperature candidate
	bool curTempTmpValid = false; //current temperature candidate is valid / has not timed out
	int  curTemp = 15;            //current temperature
	int  setTemp = 25;            //target temperature
	bool celsius = true;

	volatile uint16_t buttonCodes[7] = {
		0xFBFF, // BTN_POWER
		0xEFFF, //BTN_UP
		0xFF7F, // BTN_DOWN; //65407
		0xFFFD, // BTN_FILTER; //65533
		0x7FFF, // BTN_HEATER; //32767
		0xFFF7, // BTN_BUBBLE; //65527
		0xDFFF }; // BTN_FC;buttonCode
	volatile uint16_t btnRequest = 0;
	volatile uint8_t  btnCount = 0;

	volatile uint8_t  ringBufferSize = 64;
	volatile uint8_t  ringBufferStart = 0;
	volatile uint8_t  ringBufferEnd = 0;
	volatile uint16_t ringBuffer[64];

	bool ringBufferPop(uint16_t &value)
	{	
		if (ringBufferStart != ringBufferEnd)
		{
			value = ringBuffer[ringBufferStart];
   			ringBufferStart = (ringBufferStart + 1) % ringBufferSize;
			return true;
		}
		return false;
	}

	ICACHE_RAM_ATTR void ringBufferPush(uint16_t data)
	{
		ringBuffer[ringBufferEnd] = data;
		ringBufferEnd = (ringBufferEnd + 1)  % ringBufferSize;
		if (ringBufferEnd == ringBufferStart)
		{
			ringBufferStart = (ringBufferStart + 1)  % ringBufferSize;
		}
	}

	void processMessages();

	const int btnCycles  = 5;
	volatile bool btnPulse = false;

	// Prototypen
	volatile uint8_t clkCount = 0;

	//16-Bit Shift Register, 
	volatile uint16_t clkBuf = 0;

	enum Led {
	LED_POWER        =0,
	LED_BUBBLE       =1,
	LED_HEATER_GREEN =2,
	LED_HEATER_RED   =3,
	LED_FILTER       =4  
	};
	volatile uint8_t ledStates = 0;

	const int reqCycles = 90;
	volatile int dispCycles = reqCycles; //non blank display cycles


enum OperationType {
  OPERATION_NONE            = 0,
  OPERATION_SET_POWER       = 1,
  OPERATION_SET_HEATING     = 2,
  OPERATION_SET_FILTER      = 3,
  OPERATION_SET_BUBBLES     = 4,
  OPERATION_SET_TEMPERATURE = 5,
  OPERATION_SET_UNITS       = 6,
  
};
	bool startBoolOperation(OperationType type, bool value);
	bool startIntOperation(OperationType type, int value);
	OperationType operationType = OPERATION_NONE;
	uint32_t operationStartTime = 0;
	int operationIntValue = 0;
	bool operationBoolValue = false;
	int operationTries = 0;
};


#endif
