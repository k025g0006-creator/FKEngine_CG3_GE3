#include "Logger.h"

#include <Windows.h>
#include <ostream>

namespace Logger
{
	void Log(const std::string& message)
	{
		OutputDebugStringA(message.c_str());
	}

	void Log(std::ostream& output, const std::string& message)
	{
		output << message << std::endl;
		Log(message);
	}
}
