#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <DMComm.h>

using namespace DMComm;

/* * StringPrinter class:
 * A custom implementation of the Arduino Print interface.
 * It captures output characters/buffers and appends them directly to an Arduino String.
 */
class StringPrinter : public Print {
public:
  StringPrinter(String& target)
    : _target(target) {}

  virtual size_t write(uint8_t c) {
    _target += (char)c;
    return 1;
  }

  virtual size_t write(const uint8_t* buffer, size_t size) {
    _target.reserve(_target.length() + size);
    for (size_t i = 0; i < size; i++) {
      _target += (char)buffer[i];
    }
    return size;
  }

private:
  String& _target;
};

// Hardware configuration and pin definitions
#define OUTPUT_PIN 25
#define INPUT_PIN 33


// Global variable to store incoming BLE commands
String BLECOMM;

// Initialize DMComm hardware interfaces
DComOutput output = DComOutput(OUTPUT_PIN, DMCOMM_NO_PIN);
//AnalogProngInput input = AnalogProngInput(INPUT_PIN, 3300, 10);
DigitalProngInput input = DigitalProngInput(INPUT_PIN);

// Initialize communicators and controller for DMComm
ClassicCommunicator classic_comm = ClassicCommunicator(output, input);
ColorCommunicator color_comm = ColorCommunicator(output, input);
Controller controller = Controller();

// Pointer to store the currently active DigiROM
BaseDigiROM* current_digirom = nullptr;

/*
 * createDigiROM:
 * Parses a string to determine the type of DigiROM signal and creates 
 * the appropriate DigiROM object dynamically.
 */
BaseDigiROM* createDigiROM(const char* digirom_data) {
  DigiROMType rom_type = digiROMType(digirom_data);
  switch (rom_type.signal_type) {
    case kSignalTypeV:
    case kSignalTypeX:
    case kSignalTypeY:
      return new ClassicDigiROM(digirom_data);
    case kSignalTypeC:
      return new WordsDigiROM(digirom_data);
    default:
      return nullptr;
  }
}

// Nordic UART Service (NUS) UUIDs (128-bit) for BLE communication
static const char* NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_CHAR_UUID_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // Client writes to this
static const char* NUS_CHAR_UUID_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // Server notifies client from this

// BLE Global objects
static NimBLEServer* pServer;
static NimBLECharacteristic* pRxCharacteristic;
static NimBLECharacteristic* pTxCharacteristic;

/*
 * ServerCallbacks:
 * Handles BLE server connection and disconnection events.
 */
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.printf("Client %s connected (handle %u)\n",
                  connInfo.getAddress().toString().c_str(),
                  connInfo.getConnHandle());
    // Update connection parameters for better stability/performance
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("Client disconnected, advertising again");
    // Restart advertising automatically when a client disconnects
    NimBLEDevice::startAdvertising();
  }
};

/*
 * RxCallbacks:
 * Handles incoming data when a BLE client writes to the RX characteristic.
 */
class RxCallbacks : public NimBLECharacteristicCallbacks {
private:
  std::string rxBuffer = "";

public:
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
    std::string val = pCharacteristic->getValue();
    rxBuffer += val;

    if (rxBuffer.find('\n') != std::string::npos || rxBuffer.find('\r') != std::string::npos) {
      if (pTxCharacteristic && pServer->getConnectedCount() > 0) {
        BLECOMM = String(rxBuffer.c_str());
        BLECOMM.trim();
        Serial.printf("Received %u bytes: %s\n", BLECOMM.length(), BLECOMM.c_str());
      }
      rxBuffer = "";
    }
  }
};


void setup() {

  // Register communicators to the controller
  controller.add(classic_comm);
  controller.add(color_comm);

  // Configure ADC for the input prong
  analogReadResolution(10);
  analogSetAttenuation(ADC_11db);

  Serial.begin(9600);
  delay(1000);

  // Start WiFi temporarily just to generate a unique MAC address for the BLE name
  WiFi.mode(WIFI_STA);
  delay(300);

  // Generate a unique Bluetooth name using the last 4 characters of the MAC address
  String deviceMac = WiFi.macAddress();
  deviceMac.replace(":", "");
  String btname = "BT-COM-" + deviceMac.substring(deviceMac.length() - 4);

  Serial.println("Starting BLE UART server");

  // Initialize BLE Device
  NimBLEDevice::init(btname.c_str());      // Set advertised device name
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Set to maximum transmit power

  // Create BLE Server and assign callbacks
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create Nordic UART Service
  NimBLEService* nus = pServer->createService(NUS_SERVICE_UUID);

  // Create RX Characteristic (Client -> Server)
  pRxCharacteristic = nus->createCharacteristic(
    NUS_CHAR_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR  // Allow write with or without response
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  // Create TX Characteristic (Server -> Client)
  pTxCharacteristic = nus->createCharacteristic(
    NUS_CHAR_UUID_TX,
    NIMBLE_PROPERTY::NOTIFY  // Enable notifications
  );

  // Start the service
  nus->start();

  // Print diagnostics: show service and characteristic UUIDs in Serial Monitor
  Serial.println("Service and characteristics created:");
  Serial.printf("  Service UUID: %s\n", nus->getUUID().toString().c_str());
  if (pRxCharacteristic) Serial.printf("  RX Char UUID: %s\n", pRxCharacteristic->getUUID().toString().c_str());
  if (pTxCharacteristic) Serial.printf("  TX Char UUID: %s\n", pTxCharacteristic->getUUID().toString().c_str());

  // Configure advertising
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(btname.c_str());
  adv->addServiceUUID(nus->getUUID());  // Add the primary service UUID to advertising packet

  // Start advertising
  NimBLEDevice::startAdvertising();

  Serial.println("Advertising started");
}

void loop() {
  // Check if a new command was received via BLE
  if (BLECOMM.length() > 0) {
    current_digirom = createDigiROM(BLECOMM.c_str());  // Create new DigiROM object based on the string
    BLECOMM = "";                                      // Clear the command buffer
  }

  // If a valid DigiROM is loaded, execute it
  if (current_digirom != nullptr) {
    controller.execute(*current_digirom, 3000);  // Run execution with a 3000ms timeout
    delay(10);

    // Capture the execution result into a String
    String resultString = "";
    StringPrinter stringPrinter(resultString);
    current_digirom->printResult(stringPrinter);
    Serial.println(resultString);
    // Send the result back to the client via BLE notification
    if (pTxCharacteristic && pServer && pServer->getConnectedCount() > 0) {
      String outStr = resultString + "\n";
      pTxCharacteristic->setValue(std::string(outStr.c_str(), outStr.length()));
      pTxCharacteristic->notify();
    }

    // Add a delay between turns if required by the DigiROM
    if (current_digirom->turn() == 1) {
      delay(3000);
    }
  } else {
    // Idle delay if no active DigiROM
    delay(300);
  }
}