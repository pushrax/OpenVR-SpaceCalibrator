#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"
#include <chrono>
#include <ctime>

FILE *LogFile;

void OpenLogFile()
{
	LogFile = fopen("/tmp/space_calibrator_driver.log", "w");
	if (LogFile == nullptr)
	{
		LogFile = stderr;
	}
}

tm TimeForLog()
{
    if(LogFile == nullptr){
        OpenLogFile();
    }

	auto now = std::chrono::system_clock::now();
	auto nowTime = std::chrono::system_clock::to_time_t(now);
	tm buf;
	auto tm = localtime_r(&nowTime, &buf);
	return *tm;
}



void LogFlush()
{
	fflush(LogFile);
}
