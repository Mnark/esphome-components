# ESPHome WebDav component

This component allows an SD Card to be shared via the WebDav protocol to other computers in the local network. It is dependant on the sdmmc component to mount the card onto the local file system. It provides a very basic level of security (which can be disabled).

| :warning: WARNING                                       |
|:--------------------------------------------------------|
| This component requires ESP-IDF version 5.2.0 or above. |
| At time of writing, esphome is at version 5.1.5.        |
| To compile this componet use the following in yaml      |

```yaml
 esp32:
  board: esp32dev
  framework:
    type: esp-idf
    version: 5.3.2
    release: "250210"
    platform_version: "https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip"

```

## Configuration variables

* **port** (Optional, Default 80): The port used to host the service.
* **sdmmc** (Required): The id of the sdmmc card to be shared.
* **share_name** (Optional, Default share): The name of the virtual directory to be shared. This does not have to actually be present of the card but will be used when creating the share. (e.g. when a share_name of "share" the network addres when connecting to the drive will be http://[ip-address]/share ).  
* **auth** (Optional, Default BASIC): The authentication method applied. Can be either BASIC (user/password required) or NONE.
* **user** (Required if auth is BASIC)
* **password** (Required if auth is BASIC)
* **enable_web** (Optional, Default False): Enable in built web site hosting. ** Still under development
* **web_directory** (Optional, Default www): Specify the directory on the SD card that contains the web pages.
* **home_page** (Optional): Enable the camera for the website.
* **camera** (Optional, camera id): Id of the camera if web hosting is enabled.
* **video_directory**
* **share_name**

## Configuration

```yaml
# Example configuration entry
external_components:
  - source: github://mnark/esphome-components
    components: [sdmmc, webdav]

sdmmc:
  id: sdcard1
  name: "SD Card"
  ...

webdav:
  port: 80
  sdmmc: sdcard1
  share_name: share
  web_directory: www
  camera: camera1
  auth: BASIC
  user: !secret webdav_user
  password: !secret webdav_password
```

## Usage

### Example

```yaml

esp32_camera:
  id: camera1
 ...

sdmmc:
  id: sdcard1
...

webdav:
  port: 80
  sdmmc: sdcard1
  share_name: share
  web_directory: www
  camera: camera1
  auth: BASIC
  user: !secret webdav_user
  password: !secret webdav_password

```
