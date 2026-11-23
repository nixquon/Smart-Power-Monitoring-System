#include "network/ConnectManager.h"

static bool g_shouldSaveConfig = false;

static void saveConfigCallback() {
    g_shouldSaveConfig = true;
}

ConnectManager::ConnectManager(ConfigManager& configMgr)
    : _configMgr(configMgr),
      _state(ConnectionState::DISCONNECTED),
      _configSaved(false),
      _isMqttConnected(false),
      _lastReconnectAttempt(0),
      _reconnectFailCount(0),
      _customStatusInfo(nullptr),
      _customServerHost(nullptr),
      _customServerPort(nullptr) {
}

ConnectManager::~ConnectManager() {
    if (_customStatusInfo) delete _customStatusInfo;
    if (_customServerHost) delete _customServerHost;
    if (_customServerPort) delete _customServerPort;
}

void ConnectManager::setupParameters(const String& failureReason) {
    if (_customStatusInfo) {
        delete _customStatusInfo;
        delete _customServerHost;
        delete _customServerPort;
        _customStatusInfo = nullptr;
        _customServerHost = nullptr;
        _customServerPort = nullptr;
    }

    String currentSSID = _configMgr.getWifiSSID();
    String currentHost = _configMgr.getServerHost();
    String currentPort = String(_configMgr.getServerPort());
    String deviceId    = _configMgr.getDeviceId();

    // 포털 상단에 표시할 기존 설정(비밀번호 제외) 및 장애 사유 HTML
    String statusHtml = "<div style='background-color:#f8d7da; color:#721c24; padding:12px; border-radius:5px; margin-bottom:15px; border:1px solid #f5c6cb;'>";
    statusHtml += "<b>[ 장치 정보 및 연결 상태 ]</b><br>";
    statusHtml += "- Device ID: " + deviceId + "<br>";
    statusHtml += "- 기존 SSID: " + (currentSSID.length() > 0 ? currentSSID : "설정 없음") + "<br>";
    statusHtml += "- 기존 Server: " + currentHost + ":" + currentPort + "<br><br>";

    if (failureReason.length() > 0) {
        statusHtml += "<b>연결 실패: " + failureReason + "</b>";
    } else {
        statusHtml += "새로운 네트워크 및 서버 설정을 입력해 주세요.";
    }
    statusHtml += "</div>";

    _customServerHost = new WiFiManagerParameter("server_host", "MQTT Server Host/IP", currentHost.c_str(), 64);
    _customServerPort = new WiFiManagerParameter("server_port", "MQTT Server Port", currentPort.c_str(), 6);

    _wm.setCustomHeadElement("<meta charset='UTF-8'>");
    _wm.setCustomHeadElement("<div style='background-color:#f8d7da; color:#721c24;padding:12px; border-radius:5px;margin-bottom:15px; border:1px solid #f5c6cb;'>");
    _wm.setCustomHeadElement(statusHtml.c_str());
    _wm.addParameter(_customServerHost);
    _wm.addParameter(_customServerPort);

    _wm.setSaveConfigCallback(saveConfigCallback);
}

bool ConnectManager::begin() {
    _wm.setConfigPortalBlocking(false);
    _wm.setConnectTimeout(5);

    String savedSSID = _configMgr.getWifiSSID();
    String savedPW   = _configMgr.getWifiPW();

    if (savedSSID.length() > 0) {
        Serial.println("[ConnectManager] Wi-Fi 비동기 접속 시작: " + savedSSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID.c_str(), savedPW.c_str());
        _state = ConnectionState::CONNECTING;
        _lastReconnectAttempt = millis();
    } else {
        Serial.println("[ConnectManager] 저장된 Wi-Fi 없음 -> Captive Portal 오픈");
        startCaptivePortal("저장된 Wi-Fi 설정 정보가 없습니다. 기기 초기화에 문제가 있으니 확인바랍니다.");
    }

    return true;
}

void ConnectManager::startCaptivePortal(const String& failureReason) {
    setupParameters(failureReason);

    String deviceId = _configMgr.getDeviceId();
    String portalSSID = "SmartPlug_Setup_";
    if (deviceId.length() >= 6) {
        portalSSID += deviceId.substring(deviceId.length() - 6);
    } else if (deviceId.length() > 0) {
        portalSSID += deviceId;
    } else {
        uint32_t chipId = (uint32_t)ESP.getEfuseMac();
        char fallbackId[8];
        snprintf(fallbackId, sizeof(fallbackId), "%06lX", chipId & 0xFFFFFF);
        portalSSID += String(fallbackId);
    }

    Serial.println("[ConnectManager] Captive Portal 오픈: " + portalSSID + " | 사유: " + failureReason);

    // Non-blocking 모드에서 웹 서버가 빈 화면을 반환하는 버그 방지: AP_STA 모드 강제 전환
    WiFi.disconnect(false);
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP_STA);

    _wm.setConfigPortalBlocking(false);
    _wm.setConfigPortalTimeout(180);
    _wm.startConfigPortal(portalSSID.c_str());

    // startConfigPortal()은 Non-blocking 모드에서 즉시 반환되므로 무조건 상태 전환
    _state = ConnectionState::CAPTIVE_PORTAL_OPEN;
}

void ConnectManager::processCaptivePortal() {
    _wm.process();

    if (g_shouldSaveConfig) {
        g_shouldSaveConfig = false;

        String newSSID = WiFi.SSID();
        String newPW   = WiFi.psk();
        String newHost = _customServerHost->getValue();
        uint16_t newPort = (uint16_t)atoi(_customServerPort->getValue());

        if (newPort == 0) newPort = 1883;

        _configMgr.setWifiCredentials(newSSID, newPW);
        _configMgr.setServerInfo(newHost, newPort);

        _configSaved = true;
        Serial.println("[ConnectManager] 설정 업데이트 완료");

        _wm.stopConfigPortal();
        _reconnectFailCount = 0;
        _state = ConnectionState::CONNECTING;
        _lastReconnectAttempt = millis();

        WiFi.begin(newSSID.c_str(), newPW.c_str());
    }
}

void ConnectManager::loop() {
    unsigned long now = millis();

    // 1. Captive Portal 활성 상태면 웹 요청 처리만 수행 (Non-blocking)
    if (_state == ConnectionState::CAPTIVE_PORTAL_OPEN) {
        processCaptivePortal();
        
        // 포털 타임아웃(180초) 등으로 자동 종료되었을 경우 상태 복구
        if (!_wm.getConfigPortalActive()) {
            _state = ConnectionState::DISCONNECTED;
            _lastReconnectAttempt = now;
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED){
        // 2. Wi-Fi 및 MQTT 서버까지 모두 정상 연결된 상태
        if(_isMqttConnected) {
            if (_state != ConnectionState::SERVER_CONNECTED) {
                _state = ConnectionState::SERVER_CONNECTED;
                Serial.println("[ConnectManager] Wi-Fi & MQTT 서버 연결됨");
            }
            _reconnectFailCount = 0;
            return;
        }
        // 3. Wi-Fi는 붙었으나 MQTT 서버 연결이 되지 않은경우
        else {
            // 연결 대기 상태가 아니면 대기 상태로 전환
            if (_state != ConnectionState::WIFI_CONNECTED) {
                _state = ConnectionState::WIFI_CONNECTED;
                _reconnectFailCount = 0; // Wi-Fi 연결 직후 MQTT 대기 카운트 초기화
                _lastReconnectAttempt = now; // 대기 시간 초기화
                Serial.println("[ConnectManager] Wi-Fi 연결됨 (MQTT 서버 접속 대기 중...)");
            }
        }
    } 
    // 4. Wi-Fi 자체가 끊어진 상태
    else {
        if (_state == ConnectionState::WIFI_CONNECTED || _state == ConnectionState::SERVER_CONNECTED) {
            _state = ConnectionState::DISCONNECTED;
            _isMqttConnected = false;
            Serial.println("[ConnectManager] Wi-Fi 연결 끊김");
        }
    }

    // 5. 10초 주기 비동기 재연결 및 실패 카운트 (Wi-Fi 미연결 또는 MQTT 미연결 공통 처리)
    if (now - _lastReconnectAttempt > 10000) {
        _lastReconnectAttempt = now;
        _reconnectFailCount++;

        if (WiFi.status() != WL_CONNECTED) {
            String ssid = _configMgr.getWifiSSID();
            String pw   = _configMgr.getWifiPW();
            if (ssid.length() > 0) {
                Serial.printf("[ConnectManager] Wi-Fi 비동기 재연결 시도 (%d/3회)...\n", _reconnectFailCount);
                WiFi.begin(ssid.c_str(), pw.c_str());
                _state = ConnectionState::CONNECTING;
            }
        } else {
            // Wi-Fi는 붙어있고 MqttHandler가 백그라운드에서 재접속을 시도하는 중 대기 카운트만 증가
            Serial.printf("[ConnectManager] MQTT 서버 응답 대기 중 (%d/3회)...\n", _reconnectFailCount);
        }

        // 연속 3회(30초) 동안 해결되지 않으면 구체적 사유와 함께 Captive Portal 오픈
        if (_reconnectFailCount >= 3) {
            _reconnectFailCount = 0;
            if (WiFi.status() != WL_CONNECTED) {
                startCaptivePortal("Wi-Fi 공유기 접속 실패 (SSID / 비밀번호 확인 필요)");
            } else if (!_isMqttConnected) {
                startCaptivePortal("MQTT 서버 접속 실패 (Server Host / Port 확인 필요)");
            }
        }
    }
}

void ConnectManager::setServerConnected(bool isConnected) {
    _isMqttConnected = isConnected;
}

bool ConnectManager::isConfigSaved() const {
    return _configSaved;
}

ConnectionState ConnectManager::getState() const {
    return _state;
}

IPAddress ConnectManager::getLocalIP() const {
    return WiFi.localIP();
}