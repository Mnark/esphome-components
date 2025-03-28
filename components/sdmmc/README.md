# ESPHome SDMMC component

This component allows you to mount an SD Card into the firmware as part of the file system.

## Configuration



```yaml
# Example configuration entry
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

## Configuration variables: 

* **command_pin** (Required, GPIO pin): Pin used for the command signal.
* **clock_pin** (Required, GPIO pin): Pin used for the clock signal.
* **data_pin** (Required, GPIO pin): Pin(s) used for the data signal. If opperating in 1 bit mode then a single GPIO pin should be specified. If operating in 4-bit mode, an array of 4 GPIO pins should be specified e.g. [GPIO13,GPIO12,GPIO04,GPIO02] **N.B. The ESP32-CAM board shares pin GPIO04 with the SD Card. This means the flash will activate whenever the card is read or written to!!!**

## Use

### Directly in Yaml

The component will be typically be used by other components to access the SDCard.

There is one action available directly from a yaml and this will save a binary source directly to the card, e.g. when used with the esp32_camera component, a button could be configured to save an image directly to the SD card.

#### Saving an Image example

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
            id(sdcard1).write_file(id(filename).c_str(), image.length, image.data);
            id(save_to_disk) = false;
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

#### Saving an Avi example - N.B. Limited frame rate

```yaml
globals:
  - id: save_video_to_disk
    type: bool
    restore_value: no
    initial_value: "false"
  - id: video_filename
    type: std::string
    restore_value: no
  - id: video_filecount
    type: int
    restore_value: yes
    initial_value: "0"
  - id: video_duration
    type: int
    initial_value: "20" #seconds
  - id: video_end_time
    type: int
    restore_value: yes

esp32_camera:
  id: camera1
    ...
  on_image:
    then:
      - lambda: |-
          if (id(save_to_disk)){
            id(sdcard1).write_file(id(filename).c_str(), image.length, image.data);
            id(save_to_disk) = false;
          }
          if (id(save_video_to_disk)){
            //id(sdcard1).append_file(id(video_filename).c_str(), 4, &id(picture_header));
            id(sdcard1).write_avi(id(video_filename).c_str(), image.length, image.data);
            if (id(sntp_time).now().timestamp > id(video_end_time)){
              id(sdcard1).write_avi(id(video_filename).c_str(), 0, NULL);
              id(save_video_to_disk) = false;
            }
          }  

sdmmc:
  id: sdcard1
  name: "SD Card"
  command_pin: GPIO15
  clock_pin: GPIO14
  data_pin: GPIO02

button:
  - platform: template
    name: Record Video
    icon: mdi:video
    on_press:
      then:
        - lambda: |-
            id(save_video_to_disk) = true;
            id(video_filecount)++;
            id(video_filename) = "vid" + std::to_string(id(video_filecount)) + ".avi";
            id(video_end_time) = id(sntp_time).now().timestamp + (id(video_duration) );
```

