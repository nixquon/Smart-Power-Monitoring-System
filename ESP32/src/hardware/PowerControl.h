#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <Arduino.h>
#include "config/ConfigManager.h"
#include "hardware/LedControl.h"

class PowerControl {
public:
    PowerControl(ConfigManager& configMgr, LedControl& LedControler, uint8_t relayPin, bool relayActiveLow = true);

    void begin(bool initialPowerState = false);

    // Power 변경
    void setPower(bool on);

    // 현장 수동 조작 요청용 (ModeState::RemoteOnly일 경우 차단 및 경고 점멸)
    bool tryManualToggle();

    bool isPowerOn() const;

private:
    // 릴레이 조작
    void applyRelayState(bool on);

    ConfigManager& _configMgr;
    LedControl& _LedControler;
    uint8_t _relayPin;
    bool _relayActiveLow;

    bool _isPowerOn;
};

#endif // POWER_CONTROL_H