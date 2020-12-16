#include "SpaState.h"
#include "Log.h"

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

	if (clkCount == 16)
	{
		ringBufferPush(clkBuf);
		simulateButtonPress();
	}
	// reset buffer
	clkBuf = 0;
	clkCount = 0;
}

void SpaState::readSegment(uint16_t msg, int seg)
{
	uint16_t d = msg & 13976; // mask and keep segment bits

	if (d == 16)
		digit[seg] = '0';
	else if (d == 9368)
		digit[seg] = '1';
	else if (d == 520)
		digit[seg] = '2';
	else if (d == 136)
		digit[seg] = '3';
	else if (d == 9344)
		digit[seg] = '4';
	else if (d == 4224)
		digit[seg] = '5';
	else if (d == 4096)
		digit[seg] = '6';
	else if (d == 1176)
		digit[seg] = '7';
	else if (d == 0)
		digit[seg] = '8';
	else if (d == 128)
		digit[seg] = '9';
	else if (d == 1152)
		digit[seg] = '9';
	else if (d == 4624)
		digit[seg] = 'C';
	else if (d == 5632)
		digit[seg] = 'F';
	else if (d == 4608)
		digit[seg] = 'E';
	else if (d == 13976)
		digit[seg] = ' '; //blank

	// if this is the last digit, decide what temp value this is
	if (seg == 3)
		classifyTemperature();
}

void SpaState::readLEDStates(uint16_t msg)
{
	uint8_t ls = ledStates;
	ledStates = 0;

	if (bitRead(msg, 0) == 0)
		bitSet(ledStates, LED_POWER);
	if (bitRead(msg, 10) == 0)
		bitSet(ledStates, LED_BUBBLE);
	if (bitRead(msg, 9) == 0)
		bitSet(ledStates, LED_HEATER_GREEN);
	if (bitRead(msg, 7) == 0)
		bitSet(ledStates, LED_HEATER_RED);
	if (bitRead(msg, 12) == 0)
		bitSet(ledStates, LED_FILTER);

	if (ls != ledStates)
	{
		// changed, use or to what
		ls = ls ^ ledStates;
		if (bitRead(ls, LED_POWER) != 0)
			; //controller_power_state_changed_event();
		if (bitRead(ls, LED_FILTER) != 0)
			; //controller_pump_state_changed_event();
		if ((bitRead(ls, LED_HEATER_GREEN) ^ bitRead(ls, LED_HEATER_RED)) != 0)
			; //controller_target_heating_state_changed_event();
		if (bitRead(ls, LED_HEATER_RED) != 0)
			; //controller_current_heating_state_changed_event();
	}
}

void SpaState::classifyTemperature()
{
	if (digit[0] != ' ')
	{ // non blank display
		// remember the last valid temp reading
		char tmpDigit[5];
		for (int i = 0 ; i < 5; ++i)
			tmpDigit[i] = digit[i];
		lstTemp = atoi(tmpDigit);
		if (--dispCycles < 0)
		{
			// temperature, that is not followed by an empty display for 90 (>82) cycles, is the current temperature
			if (curTempTmpValid && curTemp != curTempTmp)
			{
				curTemp = curTempTmp;
				// controller_current_temperature_changed_event(curTemp);
				for (int i = 0; i < 4; ++i)
				{
					if ('F' == digit[i] || 'C' == digit[i])
					{
						celsius = 'C' == digit[i];
					}	
				}
			}
			dispCycles = reqCycles;
			curTempTmp = lstTemp;
			curTempTmpValid = true;
		}

	}
	else
	{ // blank display during blinking
		// temperature before an blank display is the target temperature
		if (setTemp != lstTemp)
		{
			setTemp = lstTemp;
			//controller_target_temperature_changed_event(setTemp);
		}
		dispCycles = reqCycles;
		curTempTmpValid = false;
	}
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

void SpaState::writeButton(ButtonT button)
{
	if (btnRequest == 0)
	{
		btnRequest = buttonCodes[button];
		btnCount = btnCycles;
	}
}

void SpaState::handleButton(ButtonT button)
{
	writeButton(button);
}

// controller: power_state_set, power_state_get, power_state_changed_event
void SpaState::setPowerEnabled(bool power)
{
	String str;
	str = "Set Power Enabled ";
	str += power ? "ON" : "OFF";
	if (getPowerEnabled() != power)
	{
		str += ", diff";
		if (startBoolOperation(OPERATION_SET_POWER, power))
		{
			writeButton(BTN_POWER);
			str += ", write button";	
		}
		str += ", operation = " + String(operationType);
		
	}
	logger.addLine(str);

}

bool SpaState::getPowerEnabled() const
{
	return (bitRead(ledStates, LED_POWER) == 1);
}

// controller: pump_state_set, pump_state_get, pump_state_changed_event
void SpaState::setFilterEnabled(bool newValue)
{
	if (getFilterEnabled() != newValue)
	{
		if (startBoolOperation(OPERATION_SET_FILTER, newValue))
		{
			writeButton(BTN_FILTER);
		}
	}
}

bool SpaState::getFilterEnabled()
{
	return (bitRead(ledStates, LED_FILTER) == 1);
}

// controller: current_heating_state_get, current_heating_state_changed_event
bool SpaState::getIsHeating() const
{
	return (bitRead(ledStates, LED_HEATER_RED) == 1);
}

// controller: target_heating_state_set, target_heating_state_get, target_heating_state_changed_event
bool SpaState::getHeatingEnabled() const
{
	return (bitRead(ledStates, LED_HEATER_GREEN) == 1 || bitRead(ledStates, LED_HEATER_RED) == 1);
}

void SpaState::setHeatingEnabled(bool newValue)
{
	if (getHeatingEnabled() != newValue)
	{
		if (startBoolOperation(OPERATION_SET_HEATING, newValue))
		{
			writeButton(BTN_HEATER);
		}
	}
}

bool SpaState::getBubblesEnabled() const
{
	return (bitRead(ledStates, LED_BUBBLE) == 1);
}

void SpaState::setBubblesEnabled(bool newValue)
{
	if (getBubblesEnabled() != newValue)
	{
		if (startBoolOperation(OPERATION_SET_BUBBLES, newValue))
		{
			writeButton(BTN_BUBBLE);
		}
	}
}

bool SpaState::getIsTempInC() const
{
	return celsius;
}
	
bool SpaState::getIsTempInF() const
{
	return !celsius;
}

void SpaState::setTempInC(bool newValue)
{
	if (getIsTempInC() != newValue)
	{
		if (startBoolOperation(OPERATION_SET_UNITS, newValue))
		{
			writeButton(BTN_FC);
		}
	}
}

// controller: current_temperature_get, current_temperature_changed_event
int SpaState::getCurrentTemperature() const
{
	return curTemp;
}

// controller: target_temperature_get,set,changed_event
void SpaState::setTargetTemperature(int newValue)
{
	String str;
	str = "Set temp ";
	str += String(newValue);

	if (getPowerEnabled())
	{

		if (getTargetTemperature() != newValue)
		{
			str += ", diff";

			if (startIntOperation(OPERATION_SET_TEMPERATURE, newValue))
			{
				if (getTargetTemperature() > newValue)
				{
					writeButton(BTN_DOWN);
					str += ", write button down";	
				}
				else
				{
					writeButton(BTN_UP);
					str += ", write button up";	
				}
			}
			str += ", operation = " + String(operationType);

		}
	}
	logger.addLine(str);
}

int SpaState::getTargetTemperature() const
{
	return setTemp;
}

bool SpaState::startBoolOperation(OperationType type, bool value)
{
	if (operationType == OPERATION_NONE)
	{
		operationType = type;
		operationBoolValue = value;
		operationStartTime = millis();
		return true;
	}
	return false;
}
bool SpaState::startIntOperation(OperationType type, int value)
{
	if (operationType == OPERATION_NONE)
	{
		operationType = type;
		operationIntValue = value;
		operationStartTime = millis();
		return true;
	}
	return false;
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

void SpaState::loop()
{
	processMessages();

	OperationType lastOperationType = operationType;
	if (lastOperationType != OPERATION_NONE)
	{
		uint32_t timeNow = millis();
		if (timeNow - operationStartTime > 500)
		{
			operationType = OPERATION_NONE;
			switch(lastOperationType)
			{
				case OPERATION_NONE:
				break;
				case OPERATION_SET_POWER:
					if (operationTries < 2)
						setPowerEnabled(operationBoolValue);
				break;
				case OPERATION_SET_HEATING:
					if (operationTries < 2)
						setHeatingEnabled(operationBoolValue);
				break;
				case OPERATION_SET_FILTER:
					if (operationTries < 2)
						setFilterEnabled(operationBoolValue);
				break;
				case OPERATION_SET_BUBBLES:
					if (operationTries < 2)
						setBubblesEnabled(operationBoolValue);
				break;
				case OPERATION_SET_TEMPERATURE:
					if (operationTries < 30)
						setTargetTemperature(operationIntValue);
				break;
				case OPERATION_SET_UNITS:
					if (operationTries < 2)
						setTempInC(operationBoolValue);
				break;
				default:
				break;
			}

			if (operationType == OPERATION_NONE)
				operationTries = 0;
			else
				++operationTries;

		}
	}
}