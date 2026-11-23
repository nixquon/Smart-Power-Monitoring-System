#include "PowerControl.h"

PowerControl::PowerControl(ConfigManager& configMgr, LedControl& LedControler, uint8_t relayPin, bool relayActiveLow)
    : _configMgr(configMgr),
      _LedControler(LedControler),
      _relayPin(relayPin),
      _relayActiveLow(relayActiveLow),
      _isPowerOn(false) {
}

void PowerControl::begin(bool initialPowerState) {
    // 부팅 시 릴레이 튐 방지를 위해 출력 레벨 먼저 설정 후 OUTPUT 전환
    uint8_t offLevel = _relayActiveLow ? HIGH : LOW;
    digitalWrite(_relayPin, offLevel);

    pinMode(_relayPin, OUTPUT);

    setPower(initialPowerState);
}

void PowerControl::setPower(bool on) {
    _isPowerOn = on;
    applyRelayState(_isPowerOn);
    _LedControler.setSteady(_isPowerOn);

    Serial.println(String("[PowerControl] 전원 상태 변경: ") + (_isPowerOn ? "ON" : "OFF"));
}

bool PowerControl::tryManualToggle() {
    if (_configMgr.getMode() == ModeState::RemoteOnly) {
        Serial.println("[PowerControl] RemoteOnly 모드 활성 중: 현장 수동 조작 차단됨");
        // 경고: 3회 점멸
        _LedControler.blink(3, 100);
        return false;
    }

    setPower(!_isPowerOn);
    return true;
}

bool PowerControl::isPowerOn() const {
    return _isPowerOn;
}

void PowerControl::applyRelayState(bool on) {
    uint8_t level = (on == _relayActiveLow) ? LOW : HIGH;
    digitalWrite(_relayPin, level);
}