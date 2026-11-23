#include "ButtonHandler.h"

ButtonHandler::ButtonHandler(uint8_t buttonPin, bool activeLow)
    : _pin(buttonPin),
      _activeLow(activeLow),
      _lastReading(false),
      _stableState(false),
      _lastDebounceTime(0),
      _clickCount(0),
      _lastReleaseTime(0) {
}

void ButtonHandler::begin() {
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT);
    bool raw = digitalRead(_pin);
    
    // 값 초기화
    _stableState = _activeLow ? (raw == LOW) : (raw == HIGH); //물리 신호와 버튼 입력 상태 매핑
    _lastReading = _stableState; // 이전 값 초기화
}

void ButtonHandler::loop() {
    unsigned long now = millis();
    bool raw = digitalRead(_pin);
    bool isPressed = _activeLow ? (raw == LOW) : (raw == HIGH);

    if (isPressed != _lastReading) {
        _lastDebounceTime = now;
    }
    _lastReading = isPressed;

    // 디바운싱 시간이 지나면 stableState 변경
    if ((now - _lastDebounceTime) >= DEBOUNCE_MS) {
        if (isPressed != _stableState) {
            _stableState = isPressed;

            // 버튼을 떼는(Release) 순간 클릭 카운트 증가
            if (!_stableState) {
                _clickCount++;
                _lastReleaseTime = now;
            }
        }
    }

    // 싱글 클릭 확정 (더블 클릭 대기 시간 초과 시)
    if (_clickCount == 1 && (now - _lastReleaseTime) >= DOUBLE_CLICK_MS) {
        _clickCount = 0;
        if (_onClickCallback) {
            _onClickCallback();
        }
    }

    // 더블 클릭 확정 (더블 클릭 대기 시간 초과 시)
    if (_clickCount >= 2) {
        _clickCount = 0;
        if (_onDoubleClickCallback) {
            _onDoubleClickCallback();
        }
    }
}

void ButtonHandler::onClick(ButtonCallback callback) {
    _onClickCallback = callback;
}

void ButtonHandler::onDoubleClick(ButtonCallback callback) {
    _onDoubleClickCallback = callback;
}