#include "network/ConnectManager.h"

static constexpr byte DNS_PORT = 53;
static constexpr unsigned long PORTAL_TIMEOUT_MS = 300000; // 5분

ConnectManager::ConnectManager(ConfigManager& configMgr)
    : _configMgr(configMgr),
      _webServer(80),
      _state(ConnectionState::DISCONNECTED),
      _configSaved(false),
      _isMqttConnected(false),
      _lastReconnectAttempt(0),
      _portalStartTime(0),
      _reconnectFailCount(0) {
}

ConnectManager::~ConnectManager() {
    stopCaptivePortal();
}

bool ConnectManager::begin() {
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
        startCaptivePortal("저장된 Wi-Fi 설정 정보가 없습니다. 네트워크 및 서버 정보를 입력해 주세요.");
    }

    return true;
}

void ConnectManager::loop() {
    unsigned long now = millis();

    // 상태 최신화
    // 1. Captive Portal 활성 상태
    // DNS/HTTP 요청 처리 (Non-blocking) 및 타임아웃 체크 후 넘김
    if (_state == ConnectionState::CAPTIVE_PORTAL_OPEN) {
        processCaptivePortal();

        // 포털 타임아웃(300초) 초과 시 자동 종료 후 재연결 대기 상태로 복귀
        if (now - _portalStartTime > PORTAL_TIMEOUT_MS) {
            Serial.println("[ConnectManager] Captive Portal 타임아웃 -> 종료 및 재연결 시도");
            stopCaptivePortal();
            _state = ConnectionState::DISCONNECTED;
            _lastReconnectAttempt = now;
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        // 2. Wi-Fi 및 MQTT 서버까지 모두 정상 연결된 상태
        // 상태 최신화만 하고 넘김
        if (_isMqttConnected) {
            if (_state != ConnectionState::SERVER_CONNECTED) {
                _state = ConnectionState::SERVER_CONNECTED;
                Serial.println("[ConnectManager] Wi-Fi & MQTT 서버 연결됨");
            }
            _reconnectFailCount = 0;
            return;
        }
        // 3. Wi-Fi는 연결되었으나 MQTT 서버 연결이 되지 않은 경우
        else {
            if (_state != ConnectionState::WIFI_CONNECTED) {
                _state = ConnectionState::WIFI_CONNECTED;
                _reconnectFailCount = 0;
                _lastReconnectAttempt = now;
                Serial.println("[ConnectManager] Wi-Fi 연결됨 (MQTT 서버 접속 대기 중...)");
            }
        }
    }
    // 4. Wi-Fi 자체가 끊어진 상태
    else {
        if (_state >= ConnectionState::WIFI_CONNECTED) {
            _state = ConnectionState::DISCONNECTED;
            _isMqttConnected = false;
            Serial.println("[ConnectManager] Wi-Fi 연결 끊김");
        }
    }

    // wifi 및 MQTT 연결
    // 10초 주기 비동기 재연결 및 실패 카운트 (Wi-Fi 미연결 또는 MQTT 미연결 공통 처리)
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
            Serial.printf("[ConnectManager] MQTT 서버 응답 대기 중 (%d/3회)...\n", _reconnectFailCount);
        }

        // 연속 3회(30초) 동안 해결되지 않으면 구체적 사유와 함께 Captive Portal 오픈
        if (_reconnectFailCount >= 3) {
            _reconnectFailCount = 0;
            if (WiFi.status() != WL_CONNECTED) {
                startCaptivePortal("Wi-Fi 공유기 접속 실패 (SSID / 비밀번호 확인)");
            } else if (!_isMqttConnected) {
                startCaptivePortal("MQTT 서버 접속 실패 (Server Host / Port 확인)");
            }
        }
    }
}

void ConnectManager::startCaptivePortal(const String& failureReason) {
    _failureReason = failureReason;

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

    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    delay(100);

    // AP 모드 변경 후 주변 Wi-Fi 초기 스캔
    WiFi.mode(WIFI_AP_STA);
    scanNetworks();

    // AP 개방
    IPAddress apIP = IPAddress(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(portalSSID.c_str());
    delay(100);
    
    // DNS 개방
    _dnsServer.start(DNS_PORT, "*", apIP);

    // 기본 페이지 라우팅
    _webServer.on("/", HTTP_GET, [this]() { this->handleRoot(); });
    _webServer.on("/scan", HTTP_GET, [this]() { this->handleScan(); });
    _webServer.on("/save", HTTP_POST, [this]() { this->handleSave(); });

    // OS별 연결 감지 URL 라우팅
    _webServer.on("/generate_204", HTTP_GET, [this]() { this->handleRoot(); });        // Android
    _webServer.on("/hotspot-detect.html", HTTP_GET, [this]() { this->handleRoot(); }); // iOS / macOS
    _webServer.on("/connecttest.txt", HTTP_GET, [this]() { this->handleRoot(); });     // Windows
    _webServer.onNotFound([this]() { this->handleRoot(); });

    _webServer.begin();

    _portalStartTime = millis();
    _state = ConnectionState::CAPTIVE_PORTAL_OPEN;
}

void ConnectManager::processCaptivePortal() {
    _dnsServer.processNextRequest();
    _webServer.handleClient();
}

void ConnectManager::stopCaptivePortal() {
    _webServer.stop();
    _dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

void ConnectManager::scanNetworks() {
    Serial.println("[ConnectManager] 주변 Wi-Fi 스캔 중...");
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();

    if (n <= 0) {
        _scannedNetworksHtml = "<div class='no-net'>주변에서 검색된 Wi-Fi가 없습니다.</div>";
        WiFi.scanDelete();
        return;
    }

    String listHtml;
    listHtml.reserve(n * 180);

    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // 숨겨진 네트워크 제외

        int32_t rssi = WiFi.RSSI(i);
        bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        String escapedSsid = htmlEscape(ssid);

        listHtml += "<div class='net-item' onclick=\"selectWifi('" + escapedSsid + "')\">";
        listHtml += "<span class='net-name'>" + escapedSsid + (isOpen ? "" : " 🔒") + "</span>";
        listHtml += "<span class='net-rssi'>" + String(rssi) + " dBm</span>";
        listHtml += "</div>";
    }

    _scannedNetworksHtml = listHtml;
    WiFi.scanDelete();
}

void ConnectManager::handleScan() {
    scanNetworks();
    _webServer.sendHeader("Location", "/", true);
    _webServer.send(302, "text/plain", "");
}

void ConnectManager::handleRoot() {
    _webServer.send(200, "text/html; charset=utf-8", buildPortalHtml());
}

void ConnectManager::handleSave() {
    String newSSID = _webServer.arg("ssid");
    String newPW   = _webServer.arg("pw");
    String newHost = _webServer.arg("server_host");
    uint16_t newPort = (uint16_t)_webServer.arg("server_port").toInt();

    newSSID.trim();
    newHost.trim();
    if (newPort == 0) newPort = 1883;

    if (newSSID.length() == 0 || newHost.length() == 0) {
        _webServer.send(400, "text/html; charset=utf-8",
            "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
            "<body style='font-family:sans-serif;text-align:center;padding:40px;'>"
            "<h3>입력 오류</h3><p>Wi-Fi 이름(SSID)과 MQTT 서버 주소는 필수입니다.</p>"
            "<a href='/'>돌아가기</a></body></html>");
        return;
    }

    _configMgr.setWifiCredentials(newSSID, newPW);
    _configMgr.setServerInfo(newHost, newPort);
    _configSaved = true;

    String doneHtml =
        "<!DOCTYPE html><html lang='ko'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:-apple-system,sans-serif;background:#f4f6f8;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
        ".card{background:#fff;padding:28px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.08);max-width:360px;text-align:center;}"
        "h2{color:#16a34a;margin-top:0;}p{color:#4b5563;line-height:1.5;font-size:14px;}</style></head>"
        "<body><div class='card'><h2>설정 저장 완료</h2>"
        "<p><b>" + htmlEscape(newSSID) + "</b> 네트워크와<br><b>" + htmlEscape(newHost) + ":" + String(newPort) + "</b> 서버로<br>연결을 시작합니다.</p>"
        "<p style='color:#9ca3af;font-size:12px;'>이 창을 닫으셔도 됩니다.</p></div></body></html>";

    _webServer.send(200, "text/html; charset=utf-8", doneHtml);
    delay(500); // 브라우저가 완료 응답을 완전히 수신할 수 있도록 잠시 대기

    Serial.println("[ConnectManager] 설정 업데이트 완료 -> 포털 종료 및 연결 시작");
    stopCaptivePortal();

    _reconnectFailCount = 0;
    _isMqttConnected = false;
    _state = ConnectionState::CONNECTING;
    _lastReconnectAttempt = millis();

    WiFi.begin(newSSID.c_str(), newPW.c_str());
}

String ConnectManager::htmlEscape(const String& str) const {
    String escaped = str;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&#39;");
    return escaped;
}

String ConnectManager::buildPortalHtml() const {
    String currentSSID = _configMgr.getWifiSSID();
    String currentHost = _configMgr.getServerHost();
    String currentPort = String(_configMgr.getServerPort());
    String deviceId    = _configMgr.getDeviceId();

    String html;
    html.reserve(4200);

    html += F("<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'>"
              "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
              "<title>SmartPlug 네트워크 설정</title>"
              "<style>"
              "*{box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}"
              "body{margin:0;padding:16px;background:#f1f5f9;color:#1e293b;display:flex;justify-content:center;}"
              ".container{width:100%;max-width:420px;background:#fff;border-radius:14px;padding:22px;box-shadow:0 4px 16px rgba(0,0,0,0.08);}"
              "h1{font-size:20px;margin:0 0 16px 0;color:#0f172a;text-align:center;}"
              ".status-box{background:#fef2f2;border:1px solid #fecaca;color:#991b1b;padding:12px 14px;border-radius:8px;font-size:13px;line-height:1.6;margin-bottom:18px;}"
              ".status-box.info{background:#eff6ff;border-color:#bfdbfe;color:#1e40af;}"
              ".status-title{font-weight:700;margin-bottom:4px;font-size:14px;}"
              ".reason{margin-top:8px;padding-top:8px;border-top:1px solid rgba(0,0,0,0.1);font-weight:600;}"
              ".section-title{font-size:13px;font-weight:700;color:#64748b;margin:16px 0 8px 0;text-transform:uppercase;letter-spacing:0.5px;}"
              ".scan-header{display:flex;justify-content:space-between;align-items:center;margin:16px 0 6px 0;}"
              ".scan-btn{font-size:12px;font-weight:600;color:#2563eb;text-decoration:none;background:#eff6ff;padding:4px 10px;border-radius:6px;border:1px solid #bfdbfe;}"
              ".scan-btn:active{background:#dbeafe;}"
              ".net-list{max-height:160px;overflow-y:auto;border:1px solid #cbd5e1;border-radius:8px;margin-bottom:12px;background:#f8fafc;}"
              ".net-item{display:flex;justify-content:space-between;align-items:center;padding:10px 12px;border-bottom:1px solid #e2e8f0;cursor:pointer;font-size:14px;transition:background 0.15s;}"
              ".net-item:last-child{border-bottom:none;}"
              ".net-item:hover,.net-item:active{background:#e0f2fe;}"
              ".net-name{font-weight:600;color:#0f172a;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding-right:8px;}"
              ".net-rssi{font-size:12px;color:#64748b;white-space:nowrap;}"
              ".no-net{padding:14px;text-align:center;font-size:13px;color:#64748b;}"
              "label{display:block;font-size:13px;font-weight:600;margin-bottom:5px;color:#334155;}"
              "input{width:100%;padding:10px 12px;border:1px solid #cbd5e1;border-radius:8px;font-size:15px;margin-bottom:12px;outline:none;transition:border-color 0.2s;}"
              "input:focus{border-color:#2563eb;}"
              "button{width:100%;padding:12px;background:#2563eb;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:700;cursor:pointer;margin-top:6px;}"
              "button:active{background:#1d4ed8;}"
              "</style></head><body><div class='container'>");

    html += F("<h1>SmartPlug 설정</h1>");

    // 상단 장치 정보 및 장애 사유 카드
    html += (_failureReason.length() > 0) ? F("<div class='status-box'>") : F("<div class='status-box info'>");
    html += F("<div class='status-title'>장치 정보 및 현재 설정</div>");
    html += "<div>• Device ID: <b>" + htmlEscape(deviceId) + "</b></div>";
    html += "<div>• 기존 Wi-Fi: <b>" + (currentSSID.length() > 0 ? htmlEscape(currentSSID) : String("설정 없음")) + "</b></div>";
    html += "<div>• 기존 서버: <b>" + htmlEscape(currentHost) + ":" + htmlEscape(currentPort) + "</b></div>";

    if (_failureReason.length() > 0) {
        html += "<div class='reason'>⚠️ " + htmlEscape(_failureReason) + "</div>";
    }
    html += F("</div>");

    // 주변 Wi-Fi 스캔 목록 영역
    html += F("<div class='scan-header'>"
              "<span class='section-title' style='margin:0;'>주변 Wi-Fi 선택</span>"
              "<a href='/scan' class='scan-btn'>🔄 새로고침</a>"
              "</div>");
    html += "<div class='net-list'>" + _scannedNetworksHtml + "</div>";

    // 설정 입력 폼
    html += F("<form action='/save' method='POST'>");
    html += F("<label for='ssid'>Wi-Fi 이름 (SSID)</label>");
    html += "<input type='text' id='ssid' name='ssid' value='" + htmlEscape(currentSSID) + "' placeholder='위 목록에서 선택하거나 직접 입력' required>";

    html += F("<label for='pw'>Wi-Fi 비밀번호</label>");
    html += F("<input type='password' id='pw' name='pw' placeholder='비밀번호 입력 (개방형은 비워둠)'>");

    html += F("<div class='section-title'>MQTT 브로커 서버 설정</div>");
    html += F("<label for='server_host'>서버 호스트 / IP</label>");
    html += "<input type='text' id='server_host' name='server_host' value='" + htmlEscape(currentHost) + "' placeholder='예: broker.hivemq.com 또는 192.168.0.10' required>";

    html += F("<label for='server_port'>서버 포트 (Port)</label>");
    html += "<input type='number' id='server_port' name='server_port' value='" + htmlEscape(currentPort) + "' min='1' max='65535' required>";

    html += F("<button type='submit'>저장 및 연결</button>");
    html += F("</form>");

    // 목록 항목 클릭 시 SSID 입력란 자동 채움 및 비밀번호 입력란 포커스
    html += F("<script>"
              "function selectWifi(ssid){"
              "  var el=document.getElementById('ssid');"
              "  el.value=ssid;"
              "  document.getElementById('pw').focus();"
              "}"
              "</script>");

    html += F("</div></body></html>");

    return html;
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