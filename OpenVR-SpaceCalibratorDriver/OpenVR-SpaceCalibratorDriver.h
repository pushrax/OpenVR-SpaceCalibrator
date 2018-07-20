#pragma once

#ifdef OPENVRSPACECALIBRATORDRIVER_EXPORTS
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllexport)
#else
#define OPENVRSPACECALIBRATORDRIVER_API extern "C" __declspec(dllimport)
#endif
