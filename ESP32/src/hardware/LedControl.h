#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>

class LedControl {
public:
    explicit LedControl(uint8_t ledPin, bool activeLow = false);

    void begin(bool initialState = false);
    void loop();

    // 기본 상시 상태 설정 (예: 전원 ON이면 true, OFF면 false)
    void setSteady(bool on);

    // 일시적인 알림 점멸 (예: 잠금 경고 3회 점멸 후 기본 상태로 자동 복귀)
    void blink(uint8_t count, unsigned long intervalMs = 500);

private:
    void writePin(bool on);

    uint8_t _pin;
    bool _activeLow;
    bool _steadyState;

    uint8_t _remainToggles;
    unsigned long _intervalMs;
    unsigned long _lastToggleTime;
    bool _blinkState;
};

#endif // STATUS_LED_H