#pragma once

#include "Calibration.h"
#include "StaticConfig.h"

#define LINUX_CONFIG_FILE LINUX_CONFIG_DIR "spacecal-config"

void LoadProfile(CalibrationContext &ctx);
void SaveProfile(CalibrationContext &ctx);
