#include "ConfigManager.h"
#include "Credentials.h"
#include "CredentialsSample.h"

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
    _prefs.end();
}

void ConfigManager::begin() {
    // 1. NVS 읽기 전용으로 열어서 기존 저장값 유무 확인 및 로드
    _prefs.begin(NVS_NAMESPACE, true);
    
    // NVS에 device_id 키가 존재하는지 확인
    bool hasDeviceId = _prefs.isKey("device_id");
    _prefs.end();

    // 2. device_id가 없으면 초기회
    if (!hasDeviceId) {
        init();
    } else {
        loadFromNVS();
    }
}

void ConfigManager::init() {
    // eFuse MAC 주소를 읽어 기본 device_id 생성 (예: ESP32_89BACD)
    uint64_t chipid = ESP.getEfuseMac();
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "ESP32_%06lX", (uint32_t)chipid & 0xFFFFFF);
    _deviceId = String(id_buf);

    // 기본값 초기화
    _wifiSsid = DEFAULT_WIFI_SSID;
    _wifiPw = DEFAULT_WIFI_PW;
    _serverHost = DEFAULT_SERVER_HOST;
    _serverPort = DEFAULT_SERVER_PORT;
    _zone = "default";
    _mode = ModeState::All;

    // NVS에 쓰기 모드로 개설 후 초기값 물리적 저장
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("device_id", _deviceId);
    _prefs.putString("wifi_ssid", _wifiSsid);
    _prefs.putString("wifi_pw", _wifiPw);
    _prefs.putString("server_host", _serverHost);
    _prefs.putUShort("server_port", _serverPort);
    _prefs.putString("zone", _zone);
    _prefs.putString("mode", modeToString(_mode));
    _prefs.end();

    Serial.println("[ConfigManager] ID: " + _deviceId + " | 최초 부팅: Config Init 완료");
}

void ConfigManager::loadFromNVS() {
    _prefs.begin(NVS_NAMESPACE, true);

    _deviceId   = _prefs.getString("device_id", "");
    _wifiSsid   = _prefs.getString("wifi_ssid", "");
    _wifiPw     = _prefs.getString("wifi_pw", "");
    _serverHost = _prefs.getString("server_host", "");
    _serverPort = _prefs.getUShort("server_port", 1883);
    _zone       = _prefs.getString("zone", "default");
    _mode       = modeToEnum(_prefs.getString("mode", "All"));

    _prefs.end();

    Serial.println("[ConfigManager] ID: " + _deviceId + " | NVS 로드 완료");
}

ModeState ConfigManager::modeToEnum(const String& str) const{
    ModeState mode = _mode;
    if(str == "All"){
        mode = ModeState::All;
    }
    if(str == "RemoteOnly"){
        mode = ModeState::RemoteOnly;
    }
    return mode;
}

String ConfigManager::modeToString(ModeState mode) const{
    String str;
    if(mode == ModeState::All){
        str = "All";
    }
    if(mode == ModeState::RemoteOnly){
        str = "RemoteOnly";
    }
    return str;
}


// Getter

String ConfigManager::getDeviceId() const {
    return _deviceId;
}

String ConfigManager::getWifiSSID() const {
    return _wifiSsid;
}

String ConfigManager::getWifiPW() const {
    return _wifiPw;
}

String ConfigManager::getServerHost() const {
    return _serverHost;
}

uint16_t ConfigManager::getServerPort() const {
    return _serverPort;
}

String ConfigManager::getZone() const {
    return _zone;
}

ModeState ConfigManager::getMode() const {
    return _mode;
}

String ConfigManager::getModeByString() const {
    return modeToString(_mode);
}

// Setter

void ConfigManager::setWifiCredentials(const String& ssid, const String& pw) {
    _wifiSsid = ssid;
    _wifiPw = pw;

    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("wifi_ssid", _wifiSsid);
    _prefs.putString("wifi_pw", _wifiPw);
    _prefs.end();
}

void ConfigManager::setServerInfo(const String& host, uint16_t port) {
    _serverHost = host;
    _serverPort = port;

    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("server_host", _serverHost);
    _prefs.putUShort("server_port", _serverPort);
    _prefs.end();
}

void ConfigManager::setZone(const String& newZone) {
    _zone = newZone;

    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("zone", _zone);
    _prefs.end();
}

void ConfigManager::setMode(const String& newMode) {
    _mode = modeToEnum(newMode);

    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("mode", modeToString(_mode));
    _prefs.end();
}

void ConfigManager::setMode(const ModeState newMode) {
    _mode = newMode;

    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("mode", modeToString(_mode));
    _prefs.end();
}

void ConfigManager::resetToFactory() {
    Serial.println("[ConfigManager] ID: " + _deviceId + " | 공장 초기화...");
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear(); // 해당 네임스페이스 내 모든 Key 삭제
    _prefs.end();

    // 변수 초기화 후 init() 재호출
    init();
}