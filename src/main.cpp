#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOSConfig.h>

static NimBLEServer* pServer;

static TaskHandle_t motorTaskHandle = NULL;
static TaskHandle_t heaterTaskHandle = NULL;

static int targetHeartbeat = 80;
static double targetTemperature = 35;
static bool heatingEnabled = false;
static bool vibrationEnabled = false;

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("Client connected");
    };
    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("Client disconnected - start advertising");
        NimBLEDevice::startAdvertising();
    };
    void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) {
        Serial.printf("MTU updated: %u for connection ID: %u\n", MTU, desc->conn_handle);
    };
};

class HeartbeatCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        uint8_t value = pCharacteristic->getValue<uint8_t>();
        if (value != 0) {
            targetHeartbeat = value;
            Serial.println(targetHeartbeat);
        } else {
            Serial.println("value was 0");
            Serial.println(("value: " + pCharacteristic->getValue()).c_str());
        }
    }
};

class EnableCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        uint8_t value = pCharacteristic->getValue<uint8_t>();
        vibrationEnabled = value & 1;
        heatingEnabled = value & 2;
        Serial.println(value);
    }
};

void motorTask(void *parameters) {
    // Initialize pins
    ledcSetup(0, 5000, 8);
    ledcSetup(1, 50000, 8);
    ledcSetup(2, 50000, 8);
    ledcSetup(3, 50000, 8);

    ledcAttachPin(12, 0); // IN1 M1+
    ledcAttachPin(32, 1); // IN2 M1-
    ledcAttachPin(25, 2); // IN3 M2+
    ledcAttachPin(27, 3); // IN4 M2-

    ledcWrite(1, 0);
    ledcWrite(3, 0);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&xLastWakeTime, 60000 / targetHeartbeat);
        if (!vibrationEnabled)
            continue;
        for (int i = 0; i <= 25; i++) {
            ledcWrite(0, i * 4);
            ledcWrite(2, i * 4);
            vTaskDelay(2);
        }
        for (int i = 25; i >= 0; i--) {
            ledcWrite(0, i * 4);
            ledcWrite(2, i * 4);
            vTaskDelay(2);
        }
        Serial.println(xTaskGetTickCount() - xLastWakeTime);
    }
}

void heaterTask(void *parameters) {
    pinMode(35, INPUT);
    analogReadResolution(12);

    ledcSetup(4, 5000, 8);
    ledcAttachPin(33, 4);

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, 500);
        if(!heatingEnabled)
            continue;
        uint16_t sensor = analogRead(35);
        double temperature = 1. * sensor * 330 / 4096 + 10;
        Serial.println(temperature);
        //double temperature = 0;
        if (targetTemperature - temperature < 1)
            ledcWrite(4, 0);
        else if (targetTemperature - temperature < 3)
            ledcWrite(4, 20);
        else if (targetTemperature - temperature < 5)
            ledcWrite(4, 40);
        else
            ledcWrite(4, 80);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println(LED_BUILTIN);

    // Initialize BLE
    NimBLEDevice::init("PillowPartner-Pillow");
    //NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService("61535c46-202a-4859-a213-520ef987c606");
    NimBLECharacteristic* pBeatCharacteristic = pService->createCharacteristic("69e01dc5-b098-417a-9e2e-be69bc86c2ae", NIMBLE_PROPERTY::WRITE);
    NimBLECharacteristic* pEnableCharacteristic = pService->createCharacteristic("c2abad98-a402-42a8-8981-edf54dd7d6ef", NIMBLE_PROPERTY::WRITE);

    pBeatCharacteristic->setValue(80);
    pBeatCharacteristic->setCallbacks(new HeartbeatCallbacks());

    pEnableCharacteristic->setValue(0);
    pEnableCharacteristic->setCallbacks(new EnableCallbacks());

    pService->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    /** Add the services to the advertisment data **/
    pAdvertising->addServiceUUID(pService->getUUID());
    /** If your device is battery powered you may consider setting scan response
     *  to false as it will extend battery life at the expense of less data sent.
     */
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.println("Advertising Started");

    xTaskCreateUniversal(motorTask, "motorTask", 2048, NULL, 0, &motorTaskHandle, 1);
    Serial.println("Motor task started");

    xTaskCreateUniversal(heaterTask, "heaterTask", 2048, NULL, 0, &heaterTaskHandle, 1);
    Serial.println("Heater task started");
}


void loop() {
    vTaskDelete(NULL); // Everything is done in tasks
}