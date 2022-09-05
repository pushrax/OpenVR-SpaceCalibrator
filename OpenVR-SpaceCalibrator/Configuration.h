#pragma once

#include "Calibration.h"

#ifdef __linux__
#include "StaticConfig.h"
#endif

void LoadProfile(CalibrationContext &ctx);
void SaveProfile(CalibrationContext &ctx);
