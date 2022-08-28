#pragma once

#include "Calibration.h"

#define LINUX_CONFIG_FILE "/tmp/spacecal-config"

void LoadProfile(CalibrationContext &ctx);
void SaveProfile(CalibrationContext &ctx);
