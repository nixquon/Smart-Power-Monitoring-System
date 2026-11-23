#ifndef NETWORK_MQTT_HANDLER_H
#define NETWORK_MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include "config/ConfigManager.h"
#include "network/ConnectManager.h"

// 서버로부터 수신된 원격 제어 명령을 상위(main.cpp / PowerControl)로 전달하기 위한 콜백 타입
// powerOn: true면 POWER_ON, false면 POWER_OFF
using PowerCommandCallback = std::function<void(bool powerOn)>;

class MqttHandler {
public:
    // ConfigManager(NVS 설정) 및 ConnectManager(네트워크/서버 상태 동기화) 참조 주입
    MqttHandler(ConfigManager& configMgr, ConnectManager& connectMgr);
    ~MqttHandler();

    // MQTT 클라이언트 초기화 및 브로커 정보, 수신 콜백 등록
    void begin();

    // 메인 loop()에서 상시 호출:
    // Wi-Fi 연결 상태 확인, 비동기 브로커 재접속, 수신 메시지 처리(_mqttClient.loop()) 수행
    void loop();

    // 브로커 연결 정보(Host, Port)가 Captive Portal 등으로 변경되었을 때 갱신
    void updateServerConfig();

    // 현재 MQTT 브로커 연결 상태 반환
    bool isConnected();

    // --- [발행(Publish) API] ---

    // 1. 실시간 전력 데이터 발행 -> sensors/{device_id}/telemetry (Retain: false)
    bool publishTelemetry(float currentA, float powerW, float voltageV, bool isPowerOn);

    // 2. 원격 명령 처리 결과 응답 발행 -> devices/{device_id}/status (Retain: false)
    // action: "ACK" 또는 "NACK", command: "POWER_ON", "POWER_OFF", "CONFIG_UPDATED" 등
    bool publishCommandAck(const String& action, const String& command, bool isPowerOn);

    // 3. 현장 수동 버튼 조작 이벤트 발행 -> devices/{device_id}/status (Retain: false)
    bool publishManualPowerEvent(bool isPowerOn);

    // 4. 부팅/재접속 직후 초기 설정 요청 발행 -> devices/{device_id}/config_request (Retain: false)
    bool publishConfigRequest();

    // --- [콜백 등록 API] ---
    // POWER_ON / POWER_OFF 명령 수신 시 실행할 제어 함수 등록
    void onPowerCommand(PowerCommandCallback callback);

private:
    ConfigManager& _configMgr;
    ConnectManager& _connectMgr;

    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    PowerCommandCallback _powerCmdCallback;

    // 동적 토픽 문자열 캐싱
    String _topicTelemetry;
    String _topicStatus;
    String _topicConfigReq;
    String _topicDeviceCmd;
    String _topicZoneCmd;
    String _subscribedZone;

    // Non-blocking 재연결 타이머 (5초 간격)
    unsigned long _lastReconnectAttempt;

    // device_id 및 zone 기반 토픽 문자열 구성
    void buildTopics();

    // 비동기 MQTT 브로커 접속 및 LWT(유언장) 등록, 토픽 구독 수행
    bool connectToBroker();

    // 브로커 접속 성공 직후 ONLINE 상태 발행 (Retain: true)
    void publishOnlineStatus();

    // 구역(Zone) 변경 시 기존 zone 토픽 구독 해제 및 새 zone 토픽 구독
    void updateZoneSubscription(const String& newZone);

    // PubSubClient 내부 메시지 수신 정적/멤버 핸들러
    void handleMessage(char* topic, byte* payload, unsigned int length);
};

#endif // NETWORK_MQTT_HANDLER_H