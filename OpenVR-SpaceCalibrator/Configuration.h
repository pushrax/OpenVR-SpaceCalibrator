#pragma once

#include "Calibration.h"

#ifdef __linux__
#include "StaticConfig.h"
#endif

#define LINUX_CONFIG_FILE LINUX_CONFIG_DIR "spacecal-config"

void LoadProfile(CalibrationContext &ctx);
void SaveProfile(CalibrationContext &ctx);
