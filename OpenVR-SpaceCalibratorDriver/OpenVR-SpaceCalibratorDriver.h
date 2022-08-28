#pragma once

#ifdef OPENVRSPACECALIBRATORDRIVER_EXPORTS

#if  !defined(_WIN32) && !defined(_WIN64)
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" 
#else
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllexport)
#endif

#else

#if  !defined(_WIN32) && !defined(_WIN64)
#pragma warn "Linux not implimented"
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" 
#else
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllimport)
#endif

#endif
