#pragma once

#ifdef OPENVRSPACECALIBRATORDRIVER_EXPORTS

#ifdef __linux__
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" 
#else
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllexport)
#endif

#else

#ifdef __linux__
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" 
#else
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllimport)
#endif

#endif
