# ESPHome SDMMC component

This component allows you to mount an SD Card into the firmware as part of the file system.

## Configuration

```yaml
external_components:
  - source: github://mnark/esphome-components
    components: [sdmmc]

sdmmc:
  id: sdcard1
  name: "SD Card"
  command_pin: GPIO15
  clock_pin: GPIO14
  data_pin: GPIO02
```

## Use

### Directly in Yaml

The component will be typically be used by other components to access the SDCard.

There is one action available directly in yaml and this will save a binary source directly to the card, e.g. when used with the esp32_camera component, a button could be configured to save an image directly to the SD card.

#### Example

```yaml
globals:
  - id: save_to_disk
    type: bool
    restore_value: no
    initial_value: "false"
  - id: filename
    type: std::string
    restore_value: no
    initial_value: '"img"'
  - id: filecount
    type: int
    restore_value: yes
    initial_value: "0"

esp32_camera:
  id: camera1
    ...
  on_image:
    then:
      - lambda: |-
        if (id(save_to_disk)){
          id(sdcard1).save(id(filename), image.length, image.data)
        } 

sdmmc:
  id: sdcard1
  name: "SD Card"
  command_pin: GPIO15
  clock_pin: GPIO14
  data_pin: GPIO02

button:
  - platform: template
    name: Save to Card
    on_press:
      then:
        - lambda: |-
            id(save_to_disk) = true;
            id(filecount)++;
            id(filename) = "file" + std::to_string(id(filecount)) + ".jpg";
```


