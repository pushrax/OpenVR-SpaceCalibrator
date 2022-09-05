#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"
#include <chrono>
#include <ctime>

#ifdef  __linux__
#include "StaticConfig.h"
#else
#define DRIVER_LOG_FILE "space_calibrator_driver.log"
#endif


FILE *LogFile;

void OpenLogFile()
{
	LogFile = fopen(DRIVER_LOG_FILE, "a");
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
#ifdef __linux__
	tm buf;
	auto tm = localtime_r(&nowTime, &buf);
	return *tm;
#else
	tm value;
	auto tm = localtime_s(&value, &nowTime);
	return value;
#endif
}



void LogFlush()
{
	fflush(LogFile);
}
