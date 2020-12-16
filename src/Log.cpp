#include "Log.h"

void Log::addLine(const String& line)
{
	logSize += line.length();
	logLines.push_back(std::make_pair(current_id++,line));
	while (logSize > maxSize)
	{
		logSize -= logLines.front().second.length();
		logLines.pop_front();
	}
#ifdef SERIAL_DEBUG
Serial.println(line);
#endif
}

void Log::addBytes(const String& header, const uint8_t* bytes, uint8_t len)
{
	String s;
	s += header;
	for (int i = 0 ; i < len; ++i)
	{
		s += " ";
		if (bytes[i] < 10)
			s += "0";
		s += String(bytes[i], HEX);
	}
	addLine(s);
}


String Log::getLines(uint32_t from_idx) const
{
	String lines;
	for (const auto &line : logLines )
	{
		if (line.first >= from_idx)
		{
			lines += line.second;
			lines += "\n";
		}
	}
	return lines;
}