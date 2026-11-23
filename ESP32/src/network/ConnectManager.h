#ifndef NETWORK_CONNECT_MANAGER_H
#define NETWORK_CONNECT_MANAGER_H

#include <Arduino.h>
#include <WiFiManager.h>
#include "config/ConfigManager.h"

enum class ConnectionState {
    DISCONNECTED,        // 와이파이 연결 끊김
    CONNECTING,          // NVS 정보로 non-blocking 연결 시도 중
    WIFI_CONNECTED,      // 와이파이 연결 완료 (MQTT 서버 연결 대기/시도 중)
    SERVER_CONNECTED,    // 와이파이 및 MQTT 서버까지 모두 연결 완료
    CAPTIVE_PORTAL_OPEN  // 비동기 캡티브 포털(AP 모드) 웹 설정창 실행 중
};

class ConnectManager {
public:
    explicit ConnectManager(ConfigManager& configMgr);
    ~ConnectManager();

    bool begin();
    void loop();
    void startCaptivePortal(const String& failureReason = "");
    void setServerConnected(bool isConnected);

    bool isConfigSaved() const;
    ConnectionState getState() const;
    IPAddress getLocalIP() const;

private:
    ConfigManager& _configMgr;
    WiFiManager _wm;
    ConnectionState _state;
    bool _configSaved;
    bool _isMqttConnected;

    unsigned long _lastReconnectAttempt;
    uint8_t _reconnectFailCount;

    // 커스텀 HTML 파라미터 (상태 안내창 + MQTT Server Host/Port)
    WiFiManagerParameter* _customStatusInfo;
    WiFiManagerParameter* _customServerHost;
    WiFiManagerParameter* _customServerPort;

    void setupParameters(const String& failureReason);
    void processCaptivePortal();
};

#endif // NETWORK_CONNECT_MANAGER_H