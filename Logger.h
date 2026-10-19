#pragma once

#include <iosfwd>
#include <string>

namespace Logger
{
	void Log(const std::string& message);
	void Log(std::ostream& output, const std::string& message);
}
