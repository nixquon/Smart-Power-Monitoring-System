// LedControl.cpp
#include "LedControl.h"

LedControl::LedControl(uint8_t ledPin, bool activeLow)
    : _pin(ledPin),
      _activeLow(activeLow),
      _steadyState(false),
      _remainToggles(0),
      _intervalMs(100),
      _lastToggleTime(0),
      _blinkState(false) {
}

void LedControl::begin(bool initialState) {
    pinMode(_pin, OUTPUT);
    setSteady(initialState);
}

void LedControl::loop() { // 점멸 발생
    if (_remainToggles == 0) {
        return;
    }

    unsigned long now = millis();
    if (now - _lastToggleTime >= _intervalMs) {
        _lastToggleTime = now;
        _blinkState = !_blinkState;
        writePin(_blinkState);
        _remainToggles--;

        // 지정된 점멸이 끝나면 원래 steadyState로 자동 복귀
        if (_remainToggles == 0) {
            writePin(_steadyState);
        }
    }
}

void LedControl::setSteady(bool on) {
    _steadyState = on;
    if (_remainToggles == 0) {
        writePin(_steadyState);
    }
}

void LedControl::blink(uint8_t count, unsigned long intervalMs) {
    if (count == 0) return;

    _intervalMs = intervalMs;
    _remainToggles = count * 2; // ON/OFF 1세트당 토글 2회
    _lastToggleTime = millis();
    _blinkState = !_steadyState; // 현재 상태와 반대부터 시작해 시각적으로 명확히 구분
    writePin(_blinkState);
}

void LedControl::writePin(bool on) {
    digitalWrite(_pin, (on != _activeLow) ? HIGH : LOW);
}