#ifndef CONNECT_MANAGER_H
#define CONNECT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config/ConfigManager.h"

enum class ConnectionState {
    DISCONNECTED,       // 0 와이파이 연결 끊김
    CAPTIVE_PORTAL_OPEN,// 1 비동기 캡티브 포털(AP 모드) 웹 설정창 실행 중
    CONNECTING,         // 2 와이파이 연결 중
    WIFI_CONNECTED,     // 3 와이파이 연결 완료 (MQTT 서버 연결 대기/시도 중)
    SERVER_CONNECTED    // 4 와이파이 / MQTT 서버 연결 완료
};

class ConnectManager {
public:
    ConnectManager(ConfigManager& configMgr);
    ~ConnectManager();

    bool begin();
    void loop();

    // 서버 연결 상태 설정(mqtt 모듈에서)
    void setServerConnected(bool isConnected);
    bool isConfigSaved() const;
    ConnectionState getState() const;
    IPAddress getLocalIP() const;

private:
    ConfigManager& _configMgr;
    WebServer _webServer;
    DNSServer _dnsServer;

    ConnectionState _state;
    bool _configSaved;
    bool _isMqttConnected;
    unsigned long _lastReconnectAttempt;
    unsigned long _portalStartTime;
    uint8_t _reconnectFailCount;

    String _failureReason;

    void startCaptivePortal(const String& failureReason);
    void stopCaptivePortal();
    void processCaptivePortal();

    String htmlEscape(const String& str) const;
    String buildPortalHtml() const;

    void handleRoot();
    void handleSave();

    String _scannedNetworksHtml;
    void scanNetworks();
    void handleScan();
};

#endif