#include "SpaState.h"
#include "Log.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#define PIN_DS18S20 D2

OneWire oneWireSpa(PIN_DS18S20);

DallasTemperature externalTempSensor(&oneWireSpa);

extern SpaState state;
extern Log logger;


#define GPIO_IN ((volatile uint32_t*) 0x60000318)     //GPIO Read Register
#define GPIO_OUT ((volatile uint32_t*) 0x60000300)    //GPIO Write Register

#define GPIO_SET(pin) GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1 << pin)
#define GPIO_CLR(pin) GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1 << pin)
#define GPIO_READ_OUTPUT(pin) (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << pin))
#define GPIO_READ(pin) (GPIO_REG_READ(GPIO_IN_ADDRESS) & (1 << pin))

//#define GPIO16_SET() GPIO_SET(D8)
//#define GPIO16_CLR() GPIO_CLR(D8)

#define GPIO16_SET() (GP16O |=  1)
#define GPIO16_CLR() (GP16O &= ~1)

// clock the data bits into the buffer
static void ICACHE_RAM_ATTR handleClockInterrupt()
{
	state.handleClockInterrupt();
}

static void ICACHE_RAM_ATTR handleLatchInterrupt()
{
	state.handleLatchInterrupt();
}


void SpaState::init(uint8_t clockPin, uint8_t latchPin, uint8_t dataInPin, uint8_t dataOutPin)
{
	pinClock = clockPin;
	pinLatch = latchPin;
	pinDataIn = dataInPin;
	pinDataOut = dataOutPin;
	pinMode(clockPin, INPUT);
	pinMode(latchPin, INPUT);
	pinMode(dataInPin, INPUT);
	pinMode(pinDataOut, OUTPUT);
	pinMode(D8, OUTPUT);

	digitalWrite(D8, HIGH);
	digitalWrite(pinDataOut, HIGH);

	attachInterrupt(digitalPinToInterrupt(pinClock), ::handleClockInterrupt, RISING);
	attachInterrupt(digitalPinToInterrupt(pinLatch), ::handleLatchInterrupt, RISING);
	
	initialized = true;
	externalTempSensor.begin();
	int deviceCount = externalTempSensor.getDeviceCount();
	logger.addLine("Temperature Sensors available: " + String(deviceCount));


	externalTempSensor.setWaitForConversion(false);
	//externalTempSensor.requestTemperatures();

}

void SpaState::disableInterrupts()
{
	detachInterrupt(digitalPinToInterrupt(pinClock));
	detachInterrupt(digitalPinToInterrupt(pinLatch));
}

void SpaState::handleClockInterrupt()
{
	clkCount++;
	clkBuf = clkBuf << 1; //Shift buffer along

	if (bitRead(*GPIO_IN, pinDataIn) == 1)
		bitSet(clkBuf, 0); //Flip data bit in buffer if needed.
}

void SpaState::handleLatchInterrupt()
{
	// called at each latch
	if (btnPulse)
	{
		// reset pulse
		GPIO16_SET(); // pinDataOut
		btnPulse = false;
	} 
	else if (clkCount == 16)
	{
		ringBufferPush(clkBuf);
		simulateButtonPress();
	}
	// reset buffer
	clkBuf = 0;
	clkCount = 0;
}

bool SpaState::simulateButtonPress()
{
	if (0 != btnRequest)
	{
		uint16_t b = clkBuf | 0x0100; // mask buzzer

		if (btnRequest == b)
		{
			// start button pulse (until next latch)
			GPIO16_CLR();
			btnPulse = true;
			if (--btnCount <= 0)
			{
				btnRequest = 0;
			}
			return true;
		}
	}
	return false;
}

void SpaState::readSegment(uint16_t msg, int seg)
{
	msg = ~msg;

	// 7 seg bits
	// 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
	// dp     a  b     d  c     e        g  f

	uint8_t gfedcba = 0;
	gfedcba |= ((msg >> (13 - 0)) & 0x1); // a
	gfedcba |= ((msg >> (12 - 1)) & 0x2); // b
	gfedcba |= ((msg >> (9  - 2)) & 0x4); // c
	gfedcba |= ((msg >> (10 - 3)) & 0x8); // d
	gfedcba |= ((msg >> (7  - 4)) & 0x10); // e
	gfedcba |= ((msg << (-3 + 5)) & 0x20); // f
	gfedcba |= ((msg << (-4 + 6)) & 0x40); // g

	if (gfedcba == 0x3F)
		digit[seg] = '0';
	else if (gfedcba == 0x06)
		digit[seg] = '1';
	else if (gfedcba == 0x5B)
		digit[seg] = '2';
	else if (gfedcba == 0x4F)
		digit[seg] = '3';
	else if (gfedcba == 0x66)
		digit[seg] = '4';
	else if (gfedcba == 0x6D)
		digit[seg] = '5';
	else if (gfedcba == 0x7D)
		digit[seg] = '6';
	else if (gfedcba == 0x07)
		digit[seg] = '7';
	else if (gfedcba == 0x7F)
		digit[seg] = '8';
	else if (gfedcba == 0x6F)
		digit[seg] = '9';
	else if (gfedcba == 0x67)
		digit[seg] = '9';
	else if (gfedcba == 0x39)
		digit[seg] = 'C';
	else if (gfedcba == 0x71)
		digit[seg] = 'F';
	else if (gfedcba == 0x79)
		digit[seg] = 'E';
	else if (gfedcba == 0x00)
		digit[seg] = ' '; //blank

	// if this is the last digit, decide what temp value this is
	if (seg == 3)
		classifyTemperature();
}

void SpaState::readLEDStates(uint16_t msg)
{
	setPowerEnabledInternal(bitRead(msg, LED_POWER) == 0); // LED_POWER
	setBubblesEnabledInternal(bitRead(msg, LED_BUBBLE) == 0 ); // LED_BUBBLES
	setIsHeatingInternal(bitRead(msg, LED_HEATER_RED) == 0);
	setHeatingEnabledInternal(bitRead(msg, LED_HEATER_RED) == 0 || bitRead(msg, LED_HEATER_GREEN) == 0);
	setFilterEnabledInternal(bitRead(msg, LED_FILTER) == 0);
}

/*
Error codes:
E90 No water flow
• Turn off and unplug the control unit.
• Ensure the outlet cover grid is clean and free
 from obstructions.
• Clean or replace the cartridge, see
 Maintenance and Storage section.
• Ensure the in/outlet connections on the spa tub
 and fi lter pump are not blocked.
• Keep the spa water properly sanitized to
 ensure a clean and unclogged fi lter cartridge.
• If problem persists, contact Intex Service
 Center.
 
E94 Water temperature too low
• If the ambient temperature is below 39°F (4°C),
 we recommend not to use the spa.
• Turn off and unplug the control unit, add some
 warm water to raise the spa water temperature
 above 41°F (5°C), then press the button to
 heat up the water to the desired temperature.
• If problem persists, contact Intex Service
 center.

E95 Water temperature too high
• Turn off and unplug the control unit. When the
 water has cooled down, plug the GFCI/RCD
 and restart all over again.
• Turn the heater off, then press the fi lter and
 bubble buttons to lower the water temperature.
• If problem persists, contact Intex Service
 Center.

E96 System Error
• Turn off and unplug the control unit. Plug the
 GFCI/RCD and restart all over again.
• If problem persists, contact Intex Service
 Center.

E97 Dry-fi re Protection
• Contact Intex Service Center.

E99 Water temperature sensor broken
• Contact Intex Service Center.

END
After 72 hours of continuous
heating operation, the pump will
hibernate automatically. The rapid
heating and water fi ltration
functions are disabled.
• Press the button to re-active the
  filter pump.

*/

void SpaState::classifyTemperature()
{
	if (digit[0] != ' ')
	{ // non blank display
		// remember the last valid temp reading
		char tmpDigit[5];
		for (int i = 0 ; i < 5; ++i)
			tmpDigit[i] = digit[i];
		lstTemp = atoi(tmpDigit);

		--dispCycles;

		if (dispCycles > 20 && dispCycles < 80)
		{
			targTempTmp = lstTemp;
			targTempTmpValid = true;
		}
		else if (dispCycles <= 0)
		{
			// temperature, that is not followed by an empty display for 90 (>82) cycles, is the current temperature
			if (curTempTmpValid)
			{
				if (curTemp != curTempTmp)
					curTemp = curTempTmp;
				// controller_current_temperature_changed_event(curTemp);
				for (int i = 0; i < 4; ++i)
				{
					if ('F' == digit[i] || 'C' == digit[i])
					{
						setTemperatureUnitsInternal('C' == digit[i]);
					}	
				}
			}
			dispCycles = 0;
			curTempTmp = lstTemp;
			curTempTmpValid = true;
			targTempTmpValid = false;
		}
		blankCount = 0;
	}
	else
	{ 
		if (blankCount > 0)
		{
			// blank display during blinking
			// temperature before an blank display is the target temperature
			int minTemp = getIsTempInC() ? 20 : 68;
			int maxTemp = getIsTempInC() ? 40 : 104;

			if (targTempTmpValid && minTemp <= targTempTmp && targTempTmp <= maxTemp)
			{
				setTargetTemperatureInternal(targTempTmp);
			}

			dispCycles = reqCycles;
			curTempTmpValid = false;
		}
		++blankCount;
	}
}

void SpaState::writeButton(ButtonT button)
{
	if (btnRequest == 0)
	{
		btnRequest = buttonCodes[button];
		btnCount = btnCycles;
	}
}

// controller: power_state_set, power_state_get, power_state_changed_event
void SpaState::setPowerEnabled(bool power)
{
	Command c(Command::COMMAND_SET_POWER, power);
	commands.push_back(c);
}


bool SpaState::getPowerEnabled() const
{
	return isPowerEnabled;
}

// controller: pump_state_set, pump_state_get, pump_state_changed_event
void SpaState::setFilterEnabled(bool newValue)
{
	Command c(Command::COMMAND_SET_FILTER, newValue);
	commands.push_back(c);
}

bool SpaState::getFilterEnabled()
{
	return isFilterEnabled;
}

// controller: current_heating_state_get, current_heating_state_changed_event
bool SpaState::getIsHeating() const
{
	return isHeating;
}

// controller: target_heating_state_set, target_heating_state_get, target_heating_state_changed_event
bool SpaState::getHeatingEnabled() const
{
	return isHeatingEnabled;
}

void SpaState::setHeatingEnabled(bool newValue)
{
	Command c(Command::COMMAND_SET_HEATING, newValue);
	commands.push_back(c);

}

bool SpaState::getBubblesEnabled() const
{
	return areBubblesEnabled;
}

void SpaState::setBubblesEnabled(bool newValue)
{
	Command c(Command::COMMAND_SET_BUBBLES, newValue);
	commands.push_back(c);
}

void SpaState::setTemperatureUnitsInternal(bool isC)
{
	if (isCelsius != isC)
	{
		isCelsius = isC;
		
		emitChange(ChangeEvent::CHANGE_TYPE_TEMP_UNITS);
		
		uint32_t timeNow = millis();
		if (timeNow - lastTemperatureUnitChangeMS < 500)
		{
			++numQuickTempUnitChanges;
			if (4 == numQuickTempUnitChanges)
			{
				// go into wifi config mode
				if (wifiConfigCallback)
					wifiConfigCallback();
			}
		}
		else
		{
			numQuickTempUnitChanges = 0;
		}		

		lastTemperatureUnitChangeMS = timeNow;
	}
}

bool SpaState::getIsTempInC() const
{
	return isCelsius;
}
	
bool SpaState::getIsTempInF() const
{
	return !isCelsius;
}

void SpaState::setTempInC(bool newValue)
{
	Command c(Command::COMMAND_SET_UNITS, newValue);
	commands.push_back(c);
}

// controller: current_temperature_get, current_temperature_changed_event
int SpaState::getCurrentTemperature() const
{
	return curTemp;
}

float SpaState::getExternalTemperature() const
{
	return externalTemperature;
}

// controller: target_temperature_get,set,changed_event
void SpaState::setTargetTemperature(int newValue)
{
	if (getIsTempInC())
	{
		newValue = constrain(newValue, 20, 40);
	}
	else
	{
		newValue = constrain(newValue, 68, 104);
	}
	
	if (getPowerEnabled())
	{
		Command c(Command::COMMAND_SET_TEMPERATURE, newValue);
		commands.push_back(c);
	}
}

int SpaState::getTargetTemperature() const
{
	return targTemp;
}


void SpaState::setPowerEnabledInternal(bool newValue)
{
	// make sure the value is the same two times
	// in a row before allowing change
	if (isPowerEnabledTmp != newValue)
	{
		isPowerEnabledTmp = newValue;
		return;
	}

	if (isPowerEnabled != newValue)
	{
		isPowerEnabled = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_POWER);
	}
}

void SpaState::setIsHeatingInternal(bool newValue)
{
	// make sure the value is the same two times
	// in a row before allowing change
	if (isHeatingTmp != newValue)
	{
		isHeatingTmp = newValue;
		return;
	}

	if (isHeating != newValue)
	{
		isHeating = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_HEATING);
	}
}

void SpaState::setHeatingEnabledInternal(bool newValue)
{
	// make sure the value is the same two times
	// in a row before allowing change
	if (isHeatingEnabledTmp != newValue)
	{
		isHeatingEnabledTmp = newValue;
		return;
	}

	if (isHeatingEnabled != newValue)
	{
		isHeatingEnabled = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_HEATING_ENABLED);
	}
}


void SpaState::setFilterEnabledInternal(bool newValue)
{
	// make sure the value is the same two times
	// in a row before allowing change
	if (isFilterEnabledTmp != newValue)
	{
		isFilterEnabledTmp = newValue;
		return;
	}

	if (isFilterEnabled != newValue)
	{
		isFilterEnabled = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_FILTER);
	}
}


void SpaState::setBubblesEnabledInternal(bool newValue)
{
	// make sure the value is the same two times
	// in a row before allowing change
	if (areBubblesEnabledTmp != newValue)
	{
		areBubblesEnabledTmp = newValue;
		return;
	}

	if (areBubblesEnabled != newValue)
	{
		areBubblesEnabled = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_BUBBLES);
	}
}

void SpaState::setTargetTemperatureInternal(int newValue)
{
	if (targTemp != newValue)
	{
		targTemp = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_TARGET_TEMP);
		//logger.addLine("setTargetTemperatureInternal: " + String(newValue) );
	}
}

void SpaState::setAirTemperatureInternal(float newValue)
{
	if (externalTemperature != newValue)
	{
		externalTemperature = newValue;
		emitChange(ChangeEvent::CHANGE_TYPE_AIR_TEMP);
	}
}


void SpaState::processMessages()
{
	// process messages
	uint16_t msg = 0;

	while (ringBufferPop(msg))
	{
		uint16_t b = msg | 0x0100;                       // mask buzzer

		bool isButton = false;
		for (int i = 0; i < 7; ++i)
		{
			if (b == buttonCodes[i])
				isButton = true;
		}
		if (!isButton)
		{
			if (bitRead(msg, 6) == 0)
				readSegment(msg, 0);
			else if (bitRead(msg, 5) == 0)
				readSegment(msg, 1);
			else if (bitRead(msg, 11) == 0)
				readSegment(msg, 2);
			else if (bitRead(msg, 2) == 0)
				readSegment(msg, 3);
			else if (bitRead(msg, 14) == 0)
				readLEDStates(msg);
		}
	}
}

void SpaState::startStopTest(String type)
{
	if (type == "power")
		testType = OPERATION_SET_POWER;
	else if (type == "units")
		testType = OPERATION_SET_UNITS;
	else if (type == "temp")
		testType = OPERATION_SET_TEMPERATURE;

	if (!testRunning)
	{
		testRunning = true;
	}
	else
	{
		testRunning = false;
	}
	
}

class SpaTest
{
public:
	void setTestType(SpaState::OperationType type)
	{
		testType = type;
	}

	void reset(bool all)
	{
		testNumTries = 0;
		testSuccessCount = 0;
		testFailCount = 0;
		cmdTimeAvg = 0;
		cmdTimeMin = 0;
		cmdTimeMax = 0;
		if (all)
		{
			testItr = 0;
			cmdDelay = 0;
		}
	}

	bool loop()
	{
		if (testType!= SpaState::OPERATION_SET_POWER &&
		testType != SpaState::OPERATION_SET_TEMPERATURE &&
		testType!= SpaState::OPERATION_SET_UNITS)
			return false;
		// test results:
		// power: 200 timeout, 500 delay
		// C/F:  500 timeout 300 delay
		// set temp: 550 timeout, 0 delay
		bool running = true;
		uint32_t timeNow = millis();

		if (!testTryStarted)
		{
			if ((timeNow - cmdLastMS) > cmdDelay)
			{
				bool bVal = false;
				int iVal = 0;
				if (testType == SpaState::OPERATION_SET_POWER)
					bVal = state.getPowerEnabled();
				else if (testType == SpaState::OPERATION_SET_TEMPERATURE)
					iVal = state.getTargetTemperature();
				else if (testType == SpaState::OPERATION_SET_UNITS)
					bVal = state.getIsTempInC();
					
				testValueExpectedBool = !bVal;
				if (iVal % 2 == 0)
					testValueExpectedInt = iVal + 1;
				else
					testValueExpectedInt = iVal - 1;
				//logger.addLine("TargetTemp: " + String(testValueExpectedInt));
				
				cmdLastMS = timeNow;
				testTryStarted = true;

				//logger.addLine("TEST START: " + String(timeNow));

				if (testType == SpaState::OPERATION_SET_POWER)
					state.setPowerEnabled(testValueExpectedBool);
				else if (testType == SpaState::OPERATION_SET_TEMPERATURE)
					state.setTargetTemperature(testValueExpectedInt);
				else if (testType == SpaState::OPERATION_SET_UNITS)
					state.setTempInC(testValueExpectedBool);
				
			}
		}
		else
		{
			uint32_t testTime = timeNow - cmdLastMS;
			
			if (testTime < cmdTimeout)
			{
				bool matches = false;
				if (testType == SpaState::OPERATION_SET_POWER)
					matches = (state.getPowerEnabled() == testValueExpectedBool);
				else if (testType == SpaState::OPERATION_SET_TEMPERATURE)
					matches = (state.getTargetTemperature() == testValueExpectedInt);
				else if (testType == SpaState::OPERATION_SET_UNITS)
					matches = (state.getIsTempInC() == testValueExpectedBool);

				if (matches)
				{
					// success
					++testSuccessCount;
					++testNumTries;
					testTryStarted = false;
					cmdLastMS = timeNow;

					if (testTime < cmdTimeMin || cmdTimeMin == 0)
						cmdTimeMin = testTime;
					if (testTime > cmdTimeMax)
						cmdTimeMax = testTime;
					cmdTimeAvg += testTime;


					//logger.addLine("TEST: Success to " + String(testValueExpectedBool));

				}
			}
			else
			{
				//logger.addLine("TEST: timeout");
				//logger.addLine("TEST END: " + String(timeNow));

				++testFailCount;
				++testNumTries;
				testTryStarted = false;
				cmdLastMS = 0;
			}
		}
		
		if (testNumTries >= testMaxTries)
		{
			// print test results
			String testStr = "TEST " + String(testItr) + ": ";
			testStr += "Success = " + String(testSuccessCount) + ", ";
			testStr += "Fail = " + String(testFailCount) + ", ";
			testStr += "AvgTime = " + (testSuccessCount ? String( cmdTimeAvg/testSuccessCount ) : String("N/A")) + ", ";
			testStr += "MinTime = " + String(cmdTimeMin) + ", ";
			testStr += "MaxTime = " + String(cmdTimeMax) + ", " ;
			testStr += "Delay = " + String(cmdDelay) ;

			logger.addLine(testStr);

			++testItr;
			cmdDelay += 50;

			reset(false);


			if (testItr == 6)
			{
				reset(true);
				running = false;
			}
			testTryStarted = false;
		}
		return running;
	}

	SpaState::OperationType testType = SpaState::OPERATION_NONE;
	bool testTryStarted = false;
	uint32_t cmdTimeout = 1500;
	uint32_t cmdDelay = 0;
	uint32_t cmdLastMS = 0;
	int testItr = 0;
	int testNumTries = 0;
	const int testMaxTries = 20;
	int testFailCount = 0;
	int testSuccessCount = 0;
	bool testValueExpectedBool = false;
	int testValueExpectedInt = 0;
	uint32_t cmdTimeMin = 0;
	uint32_t cmdTimeMax = 0;
	uint32_t cmdTimeAvg = 0;

};


void SpaState::loop()
{
	if (!initialized)
		return;

	if (!targetTempInitialized && millis() > 10000)
	{
		// this will cause target temp
		writeButton(BTN_DOWN);
		targetTempInitialized = true;
	}

	processMessages();

	// process commands
	if (!commands.empty())
	{
		commands.front().process();
		
		if (commands.front().isFinished())
			commands.erase(commands.begin());
	}

	{
		const int tempCheckInterval = 10000;
		static uint32_t airTempWaitTime = tempCheckInterval;
		uint32_t timeNow = millis();
		if (timeNow - timeLastAirTempCmd > airTempWaitTime)
		{
			if (airTempWaitTime == tempCheckInterval)
			{
				externalTempSensor.requestTemperatures();
				airTempWaitTime = externalTempSensor.millisToWaitForConversion(12);
			}
			else
			{
				if (externalTempSensor.isConversionComplete())
				{
					float airtemp = isCelsius ? externalTempSensor.getTempCByIndex(0) :
						externalTempSensor.getTempFByIndex(0);
					setAirTemperatureInternal(airtemp);
				}
				
				airTempWaitTime = tempCheckInterval;
			}
			timeLastAirTempCmd = timeNow;
		}
	}
	
	static SpaTest spaTest;
	if (testRunning)
	{
		spaTest.setTestType(testType);
		testRunning = spaTest.loop();
	}
	else
	{
		spaTest.reset(true);
	}
	
}


void SpaState::Command::process()
{
	if (state.btnRequest != 0)
		return;

	if (finished)
		return;

	uint32_t timeNow = millis();

	if (!commandTryStarted)
	{
		if (timeNow - commandStartTime > commandDelay)
		{
			bool bVal = commandBoolValue;
			int iVal = commandIntValue;
			ButtonT button = BTN_DOWN;
			if (commandType == COMMAND_SET_TEMPERATURE)
			{
				iVal = state.getTargetTemperature();
			
				if (iVal > commandIntValue)
				{
					button = BTN_DOWN;
					commandIntStepValue = iVal - 1;
				}
				else if (iVal < commandIntValue)
				{
					button = BTN_UP;
					commandIntStepValue = iVal + 1;
				}

			}
			else if (commandType == COMMAND_SET_POWER)
			{
				bVal = state.getPowerEnabled();
				button = BTN_POWER;
			}
			else if (commandType == COMMAND_SET_UNITS)
			{
				bVal = state.getIsTempInC();
				button = BTN_FC;
			}
			else if (commandType == COMMAND_SET_BUBBLES)
			{
				bVal = state.getBubblesEnabled();
				button = BTN_BUBBLE;
			}
			else if (commandType == COMMAND_SET_FILTER)
			{
				bVal = state.getFilterEnabled();
				button = BTN_FILTER;
			}
			else if (commandType == COMMAND_SET_HEATING)
			{
				bVal = state.getHeatingEnabled();
				button = BTN_HEATER;
			}

			if (bVal != commandBoolValue || iVal != commandIntValue)
			{
				commandTryStarted = true;
				state.writeButton(button);
				commandStartTime = timeNow;
				commandTries++;
			}
			else
			{
				finished = true;
			}
			
		}
	}
	else
	{
		uint32_t elapsedTime = timeNow - commandStartTime;
		
		if (elapsedTime < commandTimeout)
		{
			bool matches = false;
			bool matchesStep = false;
			if (commandType == COMMAND_SET_POWER)
				matches = (state.getPowerEnabled() == commandBoolValue);
			else if (commandType == COMMAND_SET_TEMPERATURE)
			{
				matches = (state.getTargetTemperature() == commandIntValue);
				matchesStep = (state.getTargetTemperature() == commandIntStepValue);
			}
			else if (commandType == COMMAND_SET_UNITS)
				matches = (state.getIsTempInC() == commandBoolValue);
			else if (commandType == COMMAND_SET_BUBBLES)
				matches = (state.getBubblesEnabled() == commandBoolValue);
			else if (commandType == COMMAND_SET_FILTER)
				matches = (state.getFilterEnabled() == commandBoolValue);
			else if (commandType == COMMAND_SET_HEATING)
				matches = (state.getHeatingEnabled() == commandBoolValue);

			if (matches)
			{
				// success
				finished = true;
				//logger.addLine("Matched: " + String(commandTries));

			}
			else if (matchesStep)
			{
				commandTryStarted = false;
				commandStartTime = timeNow;
				
				if (commandTries  == commandRetries)
					finished = true;

				//logger.addLine("Matched Step: " + String(commandTries));


			}
		}
		else
		{
			//logger.addLine("Timeout: " + String(commandTries));
			commandTryStarted = false;
			if (commandTries  == commandRetries)
				finished = true;
		}
	}
}