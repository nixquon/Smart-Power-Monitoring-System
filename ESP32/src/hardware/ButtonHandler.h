#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include <functional>

class ButtonHandler {
public:
    // ButtonCallback 함수
    using ButtonCallback = std::function<void()>;

    // pin 번호 및, activeLow 세팅
    explicit ButtonHandler(uint8_t buttonPin, bool activeLow = true);

    void begin();
    void loop();

    // 단일 클릭 콜백 등록
    void onClick(ButtonCallback callback);
    // 더블 클릭 콜백 등록
    void onDoubleClick(ButtonCallback callback);

private:
    uint8_t _pin;
    bool _activeLow;

    bool _lastReading;
    bool _stableState;
    unsigned long _lastDebounceTime;

    uint8_t _clickCount;
    unsigned long _lastReleaseTime;

    ButtonCallback _onClickCallback;
    ButtonCallback _onDoubleClickCallback;

    static constexpr unsigned long DEBOUNCE_MS      = 50;
    static constexpr unsigned long DOUBLE_CLICK_MS  = 350;
};

#endif // BUTTON_HANDLER_H