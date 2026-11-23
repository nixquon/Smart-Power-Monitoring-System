#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

enum class ModeState {
    All,        // 원격/버튼 전원 컨트롤 허용
    RemoteOnly  // 원격으로만 전원 컨트롤 허용
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    // NVS 초기화(NVS에 데이터 없을 경우) 및 설정 로드
    void begin();

    // --- Getter ---

    // DeviceId
    String getDeviceId() const;
    // Wifi SSID
    String getWifiSSID() const;
    // Wifi Password
    String getWifiPW() const;
    // 서버 host
    String getServerHost() const;
    // 서버 Port
    uint16_t getServerPort() const;
    // 장치의 장소
    String getZone() const;
    // 장치의 모드(All|RemoteOnly)
    ModeState getMode() const;
    // 장치의 모드(All|RemoteOnly)
    String getModeByString() const;

    // --- Setter (NVS 비휘발성 저장 포함) ---
    
    // wifi의 ssid와 password를 config 설정
    void setWifiCredentials(const String& ssid, const String& pw);
    // 서버 host와 port를 config 설정
    void setServerInfo(const String& host, uint16_t port);
    // 장치의 장소를 설정
    void setZone(const String& newZone);
    // 장치의 모드(All|RemoteOnly) 설정(String)
    void setMode(const String& newMode);
    // 장치의 모드(All|RemoteOnly) 설정(Enum ModeState)
    void setMode(const ModeState newMode);

    // NVS 초기화 (공장 초기화용 - 버튼 5초 이상 길게 누름 시 호출)
    void resetToFactory();

private:

    // NVS에 device_id 등의 존재 유무 확인 후 없는 정보 초기화
    void init();

    // NVS에서 config 읽어오기
    void loadFromNVS();

    // modeState(Enum) to String
    ModeState modeToEnum(const String& str) const;
    // modeState(Enum) to String
    String modeToString(ModeState mode) const;

    String _deviceId;
    String _wifiSsid;
    String _wifiPw;
    String _serverHost;
    uint16_t _serverPort;
    String _zone;
    ModeState _mode;

    Preferences _prefs;
    //NVS_NAMESPACE : sys_config
    const char* NVS_NAMESPACE = "sys_config";
};

#endif // CONFIG_MANAGER_H