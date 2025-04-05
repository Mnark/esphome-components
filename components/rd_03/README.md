# ESPHome Rd-03 component

This component allows you to use the Rd-03 Radar detectetion Module. (Only tested with the Rd-03. Not any of the variations Rd-03E etc.)

## Configuration

```yaml
# Example configuration entry
external_components:
  - source: github://mnark/esphome-components
    components: [rd_03]

rd_03:
  id: radar
  tx_pin: GPIO05
  rx_pin: GPIO06
```

## Configuration variables

* **id** (Required): Id of the component.
* **tx_pin** (Required, GPIO pin): Transmit pin on the ESP32. Connect to the RX pin on the module
* **rx_pin** (Required, GPIO pin): Recieve pin on the ESP32. Connect to the OT1 pin on the module
