#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOSConfig.h>

static NimBLEServer* pServer;

static TaskHandle_t motorTaskHandle = NULL;
static TaskHandle_t heaterTaskHandle = NULL;

static int targetHeartbeat = 80;
static double targetTemperature = 35;

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

void motorTask(void *parameters) {
    // Initialize pins
    ledcSetup(0, 5000, 8);
    ledcSetup(1, 50000, 8);
    ledcSetup(2, 50000, 8);
    ledcSetup(3, 50000, 8);

    ledcAttachPin(26, 0); // IN1 M1+
    ledcAttachPin(18, 1); // IN2 M1-
    ledcAttachPin(19, 2); // IN3 M2+
    ledcAttachPin(23, 3); // IN4 M2-

    ledcWrite(1, 0);
    ledcWrite(3, 0);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&xLastWakeTime, 60000 / targetHeartbeat);
        for (int i = 0; i <= 25; i++) {
            ledcWrite(0, i * 8);
            ledcWrite(2, i * 8);
            vTaskDelay(2);
        }
        for (int i = 25; i >= 0; i--) {
            ledcWrite(0, i * 8);
            ledcWrite(2, i * 8);
            vTaskDelay(2);
        }
        Serial.println(xTaskGetTickCount() - xLastWakeTime);
    }
}

void heaterTask(void *parameters) {
    analogReadResolution(12);
    pinMode(35, INPUT);

    ledcSetup(4, 5000, 8);
    ledcAttachPin(33, 4);

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, 500);
        double temperature = 1. * analogRead(35) * 330 / 4096;
        //double temperature = 0;
        if (targetTemperature - temperature < 1)
            ledcWrite(4, 0);
        else if (targetTemperature - temperature < 3)
            ledcWrite(4, 64);
        else if (targetTemperature - temperature < 5)
            ledcWrite(4, 128);
        else
            ledcWrite(4, 256);
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

    pBeatCharacteristic->setValue(80);
    pBeatCharacteristic->setCallbacks(new HeartbeatCallbacks());

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