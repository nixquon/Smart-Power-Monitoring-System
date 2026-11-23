#include "network/MqttHandler.h"

MqttHandler::MqttHandler(ConfigManager& configMgr, ConnectManager& connectMgr)
    : _configMgr(configMgr),
      _connectMgr(connectMgr),
      _mqttClient(_wifiClient),
      _powerCmdCallback(nullptr),
      _lastReconnectAttempt(0) {
}

MqttHandler::~MqttHandler() {
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }
}

void MqttHandler::begin() {
    buildTopics();
    updateServerConfig();

    // 수신 콜백 바인딩
    _mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->handleMessage(topic, payload, length);
    });

    // JSON 페이로드 처리를 위해 기본 패킷 버퍼 크기 확장 (기본 256 -> 512 bytes)
    _mqttClient.setBufferSize(512);
}

void MqttHandler::updateServerConfig() {
    String host = _configMgr.getServerHost();
    uint16_t port = _configMgr.getServerPort();

    if (host.length() > 0) {
        _mqttClient.setServer(host.c_str(), port);
        Serial.printf("[MqttHandler] 브로커 설정 적용: %s:%d\n", host.c_str(), port);
    }
}

void MqttHandler::buildTopics() {
    String deviceId = _configMgr.getDeviceId();
    String zone   = _configMgr.getZone();

    _topicTelemetry = "sensors/" + deviceId + "/telemetry";
    _topicStatus    = "devices/" + deviceId + "/status";
    _topicConfigReq = "devices/" + deviceId + "/config_request";

    _topicDeviceCmd = "devices/" + deviceId + "/command";
    if (zone.length() > 0) {
        _topicZoneCmd = "zones/" + zone + "/command";
    } else {
        _topicZoneCmd = "";
    }
}

void MqttHandler::loop() {
    ConnectionState netState = _connectMgr.getState();

    // Wi-Fi 자체가 연결되지 않은 상태면 MQTT 처리 중단 및 상태 동기화
    if (netState != ConnectionState::WIFI_CONNECTED && netState != ConnectionState::SERVER_CONNECTED) {
        _connectMgr.setServerConnected(false);
        return;
    }

    // 브로커 연결이 끊어진 경우 5초 주기로 비동기 재접속 시도
    if (!_mqttClient.connected()) {
        _connectMgr.setServerConnected(false);

        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            if (connectToBroker()) {
                _lastReconnectAttempt = 0;
            }
        }
    } else {
        // 연결 유지 중이면 수신 패킷 및 Keep-Alive 처리
        _mqttClient.loop();
    }
}

bool MqttHandler::connectToBroker() {
    String serverHost = _configMgr.getServerHost();
    if (serverHost.length() == 0) {
        return false;
    }

    String deviceId = _configMgr.getDeviceId();
    buildTopics();

    // LWT(유언장) JSON 페이로드 구성 (Retain = true로 등록)
    StaticJsonDocument<512> lwtDoc;
    lwtDoc["device_id"] = deviceId;
    lwtDoc["status"]    = "OFFLINE";
    lwtDoc["action"]    = "DISCONNECT";

    String lwtPayload;
    serializeJson(lwtDoc, lwtPayload);

    Serial.printf("[MqttHandler] 브로커 접속 시도 중... (%s)\n", deviceId.c_str());

    // connect(clientId, willTopic, willQos, willRetain, willMessage)
    bool connected = _mqttClient.connect(
        deviceId.c_str(),
        _topicStatus.c_str(),
        1,
        true,
        lwtPayload.c_str()
    );

    if (connected) {
        Serial.println("[MqttHandler] MQTT 브로커 연결 성공!");
        _connectMgr.setServerConnected(true);

        // 1. 개별 장치 제어 토픽 구독
        _mqttClient.subscribe(_topicDeviceCmd.c_str());
        Serial.println("[MqttHandler] 구독 등록: " + _topicDeviceCmd);

        // 2. 구역(Zone) 일괄 제어 토픽 구독 (설정된 경우)
        if (_topicZoneCmd.length() > 0) {
            _mqttClient.subscribe(_topicZoneCmd.c_str());
            Serial.println("[MqttHandler] 구역 구독 등록: " + _topicZoneCmd);
        }

        // 3. ONLINE 상태 발행 (Retain: true) 및 초기 설정 요청 발행
        publishOnlineStatus();
        publishConfigRequest();

        return true;
    } else {
        Serial.printf("[MqttHandler] 브로커 연결 실패 (rc=%d)\n", _mqttClient.state());
        return false;
    }
}

void MqttHandler::publishOnlineStatus() {
    if (!_mqttClient.connected()) return;

    StaticJsonDocument<512> doc;
    doc["device_id"] = _configMgr.getDeviceId();
    doc["status"]    = "ONLINE";
    doc["action"]    = "CONNECT";

    String payload;
    serializeJson(doc, payload);

    // LWT와 덮어쓰기 호환을 위해 Retain = true로 발행
    _mqttClient.publish(_topicStatus.c_str(), payload.c_str(), true);
}

bool MqttHandler::publishTelemetry(float currentA, float powerW, float voltageV, bool isPowerOn) {
    if (!_mqttClient.connected()) return false;

    StaticJsonDocument<512> doc;
    doc["device_id"]   = _configMgr.getDeviceId();
    doc["current_a"]   = round(currentA * 100.0) / 100.0; // 소수점 둘째 자리 반올림
    doc["power_w"]     = round(powerW * 10.0) / 10.0;     // 소수점 첫째 자리 반올림
    doc["voltage_v"]   = round(voltageV * 10.0) / 10.0;
    doc["power_state"] = isPowerOn ? "ON" : "OFF";

    String payload;
    serializeJson(doc, payload);

    return _mqttClient.publish(_topicTelemetry.c_str(), payload.c_str(), false);
}

bool MqttHandler::publishCommandAck(const String& action, const String& command, bool isPowerOn) {
    if (!_mqttClient.connected()) return false;

    StaticJsonDocument<512> doc;
    doc["device_id"]   = _configMgr.getDeviceId();
    doc["status"]      = "ONLINE";
    doc["action"]      = action;      // "ACK" or "NACK"
    doc["command"]     = command;     // "POWER_ON", "POWER_OFF", "CONFIG_UPDATED"
    doc["power_state"] = isPowerOn ? "ON" : "OFF";

    String currentZone = _configMgr.getZone();
    if (currentZone.length() > 0) {
        doc["zone"] = currentZone;
    }

    String payload;
    serializeJson(doc, payload);

    return _mqttClient.publish(_topicStatus.c_str(), payload.c_str(), false);
}

bool MqttHandler::publishManualPowerEvent(bool isPowerOn) {
    if (!_mqttClient.connected()) return false;

    StaticJsonDocument<512> doc;
    doc["device_id"]   = _configMgr.getDeviceId();
    doc["status"]      = "ONLINE";
    doc["action"]      = "MANUAL";
    doc["power_state"] = isPowerOn ? "ON" : "OFF";

    String payload;
    serializeJson(doc, payload);

    return _mqttClient.publish(_topicStatus.c_str(), payload.c_str(), false);
}

bool MqttHandler::publishConfigRequest() {
    if (!_mqttClient.connected()) return false;

    StaticJsonDocument<512> doc;
    doc["device_id"] = _configMgr.getDeviceId();

    String payload;
    serializeJson(doc, payload);

    return _mqttClient.publish(_topicConfigReq.c_str(), payload.c_str(), false);
}

void MqttHandler::updateZoneSubscription(const String& newZone) {
    if (!_mqttClient.connected()) return;

    // 기존 구독 중인 구역 토픽이 있다면 구독 해제
    if (_subscribedZone.length() > 0) {
        String oldTopic = "zones/" + _subscribedZone + "/command";
        _mqttClient.unsubscribe(oldTopic.c_str());
        Serial.println("[MqttHandler] 기존 구역 구독 해제: " + oldTopic);
    }

    // 새로운 구역 토픽 구독 등록
    if (newZone.length() > 0) {
        _topicZoneCmd = "zones/" + newZone + "/command";
        _mqttClient.subscribe(_topicZoneCmd.c_str());
        _subscribedZone = newZone;
        Serial.println("[MqttHandler] 새 구역 구독 완료: " + _topicZoneCmd);
    } else {
        _topicZoneCmd = "";
        _subscribedZone = "";
    }
}

void MqttHandler::handleMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MqttHandler] 메시지 수신 [%s]: %s\n", topicStr.c_str(), message.c_str());

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
        Serial.printf("[MqttHandler] JSON 파싱 실패: %s\n", error.c_str());
        return;
    }

    String command = doc["command"] | "";

    // 1. 전원 제어 명령 처리 (POWER_ON / POWER_OFF)
    if (command == "POWER_ON") {
        if (_powerCmdCallback) {
            _powerCmdCallback(true);
        }
        publishCommandAck("ACK", "POWER_ON", true);
    } 
    else if (command == "POWER_OFF") {
        if (_powerCmdCallback) {
            _powerCmdCallback(false);
        }
        publishCommandAck("ACK", "POWER_OFF", false);
    }
    // 2. 설정 업데이트 명령 처리 (CONFIG_UPDATE)
    else if (command == "CONFIG_UPDATE") {
        String target = doc["target"] | "";
        if (target == "ZONE") {
            String oldZone = doc["old_zone"] | "";
            String newZone = doc["new_zone"] | "";
            String currentZone = _configMgr.getZone();

            // 기존 zone 일치 여부 검증 (초기 미할당 상태이거나 old_zone이 일치할 경우 허용)
            if (currentZone.length() == 0 || currentZone == oldZone) {
                _configMgr.setZone(newZone);
                updateZoneSubscription(newZone);

                // 응답 전용 JSON 발행 (현재 전원 상태는 별도 상태가 없으므로 기본/유지 처리)
                StaticJsonDocument<512> ackDoc;
                ackDoc["device_id"] = _configMgr.getDeviceId();
                ackDoc["status"]    = "ONLINE";
                ackDoc["action"]    = "ACK";
                ackDoc["command"]   = "CONFIG_UPDATED";
                ackDoc["zone"]      = newZone;

                String ackPayload;
                serializeJson(ackDoc, ackPayload);
                _mqttClient.publish(_topicStatus.c_str(), ackPayload.c_str(), false);
            } else {
                // old_zone 불일치 시 NACK 응답
                StaticJsonDocument<512> nackDoc;
                nackDoc["device_id"] = _configMgr.getDeviceId();
                nackDoc["status"]    = "ONLINE";
                nackDoc["action"]    = "NACK";
                nackDoc["command"]   = "CONFIG_UPDATED";
                nackDoc["zone"]      = currentZone;

                String nackPayload;
                serializeJson(nackDoc, nackPayload);
                _mqttClient.publish(_topicStatus.c_str(), nackPayload.c_str(), false);
            }
        }
    }
}

void MqttHandler::onPowerCommand(PowerCommandCallback callback) {
    _powerCmdCallback = callback;
}

bool MqttHandler::isConnected() {
    return _mqttClient.connected();
}