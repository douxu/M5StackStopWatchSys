#pragma once
#include <cstdint>
namespace standby {
void init();
void tick();
bool isVisible();
bool filterTouch(bool pressed);
bool suppressButtons();
void setStopwatchRunning(bool value);
void setDirectDisplay(bool value);
void setAlarmActive(bool value);
}  // namespace standby
