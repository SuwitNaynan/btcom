# BTCOM ESP32 Firmware
Alternative connection methods for dmcomm.

This project provides firmware for the ESP32 to act as a Bluetooth Low Energy (BLE) bridge for communicating with Virtual Pet devices (such as Digimon) using the DMComm protocol. It is designed to be controlled via the Nordic UART Service (NUS).

## Connection Specifications

* **Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
* **RX Characteristic (Write):** `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
    * Used to send DigiROM commands to the ESP32.
    * Supports `WRITE` and `WRITE_NR` properties.
* **TX Characteristic (Notify):** `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
    * Used to receive results from the ESP32.

## Device Naming
The firmware generates a dynamic Bluetooth name based on the ESP32's MAC address to avoid naming conflicts:
* **Naming Format:** `BT-COM-XXXX` (where XXXX is the last 4 characters of the MAC address).
* **Example:** If the MAC address ends in `EE:FF`, the device name will be `BT-COM-EEFF`.

## Usage
1. The device begins advertising automatically upon startup.
2. Connect to the device using the Web Bluetooth API with the specified Service UUID.
3. Send the DigiROM command (String) followed by a newline character (`\n` or `\r`) to trigger execution.
4. Listen for notifications on the TX Characteristic to receive the execution result.
