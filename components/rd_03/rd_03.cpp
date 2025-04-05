#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/file.h>
#include "soc/soc_caps.h"
#include "hal/gpio_types.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "rd_03.h"

static const int RX_BUF_SIZE = 1024;
static const char *RX_TASK_TAG = "RX_TASK";
static const char *TAG = "RD03";

uint8_t commandFirmware[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
uint8_t commandModeOpen[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
uint8_t commandModeClose[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};
uint8_t commandDebugMode[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
uint8_t commandReportMode[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
uint8_t commandRunMode[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};

uint8_t resp_head[4] =  {0xFD, 0xFC, 0xFB, 0xFA};
uint8_t resp_data[64];

typedef struct{
char header[4];
uint16_t if_length;
uint16_t command;
uint16_t status;
uint16_t if_buffer_size;
char footer[4]; 
}response_t;

typedef 
  struct{
    char data[16];
}running_t;

typedef 
  struct{
    char header[4];
    uint16_t length;
    uint8_t result;
    uint16_t range;
    uint16_t gate[16];
    char footer[4];

}reporting_t;

namespace esphome {
namespace rd03 {

void RD03::setup() {
  ESP_LOGI(TAG, "Initialising RD03 peripheral...");
    
  gpio_set_pull_mode(tx_pin_, GPIO_PULLUP_ONLY);
  gpio_set_direction(tx_pin_, GPIO_MODE_OUTPUT);    
  gpio_set_pull_mode(rx_pin_, GPIO_PULLUP_ONLY);  
  gpio_set_direction(rx_pin_, GPIO_MODE_INPUT);    
  
  const uart_port_t uart_num = UART_NUM_2;
  uart_config_t uart_config = {
    //.baud_rate = 256000,
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
    .rx_flow_ctrl_thresh = 122,
  };
  esp_err_t err = uart_param_config(uart_num, &uart_config);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "Failed to set up UART config");
    return;
  }
  err = uart_set_pin(UART_NUM_2, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "Failed to set up UART pins");
    return;
  }
  const int uart_buffer_size = 640;
  QueueHandle_t uart_queue;
  // Install UART driver using an event queue here
  err = uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 10, &uart_queue, 0);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "Failed to install UART driver");
    return;
  }

  // ESP_LOGI(TAG, "Sending Firmware command");
  // err = config_command("firmware");
  // if (err == ESP_OK){
  //   ESP_LOGI(TAG, "firmware command sent success");
  // }else{
  //   ESP_LOGE(TAG, "firmware command failed");
  // }

  err = config_command("open");
  if (err == ESP_OK){
    ESP_LOGI(TAG, "open command sent success");
  }else{
    ESP_LOGE(TAG, "open command failed");
  }

  // err = config_command("reportmode");
  // if (err == ESP_OK){
  //   ESP_LOGI(TAG, "reportmode command sent success");

  // }else{
  //   ESP_LOGE(TAG, "reportmode command failed");
  // }
  err = config_command("runmode");
  if (err == ESP_OK){
    ESP_LOGI(TAG, "runmode command sent success");

  }else{
    ESP_LOGE(TAG, "runmode command failed");
  }

  err = config_command("close");
  if (err == ESP_OK){
    ESP_LOGI(TAG, "close command sent success");

  }else{
    ESP_LOGE(TAG, "close command failed");
  }

  data_ = (char*) malloc(RX_BUF_SIZE + 1);
  //xTaskCreate(rx_task, RX_TASK_TAG , 1024 * 3, this, uxTaskPriorityGet(NULL) + 1, NULL);

  return;
}

void RD03::loop() {
  size_t length;
  int num_readings;
  int rxBytes;
  char range[5];

  uart_get_buffered_data_len(UART_NUM_2, (size_t*)&length);
  if (length < 14){
    uart_flush(UART_NUM_2);
    return;
  }
  //ESP_LOGI(TAG, "There are %d bytes in the queue. Mode: %d", length, mode_);

  switch (mode_) {
    case 0: //run mode
      rxBytes = uart_read_bytes(UART_NUM_2, data_, length, 50);
      //ESP_LOGI(TAG, "%.14s", data_);
      if (data_[1] == 78){ // Is second character an N
        set_motion_value(true);
      }else{
        set_motion_value(false);
        set_range_value(0);
        return;
      }

      for (int i = 10; i < length; i++){
        range[i-10] = data_[i];
        range[i-9] = 0;
      }
      set_range_value(atoi(range));
      //ESP_LOGI(TAG, "range: %d", atoi(range));
      //uart_flush(UART_NUM_2);
      break;
    case 1: // report mode
    case 2: // debug mode
    default:
      uart_flush(UART_NUM_2);
      ESP_LOGW(TAG, "Mode not implemented");
    break;  
  }
}

esp_err_t RD03::config_command( const char* command ){
  uint8_t * cmd;
  bool valid = false;
  size_t len;
  size_t length;

  if (strncmp(command, "open", 4) == 0) {
    cmd = commandModeOpen;
    len = sizeof(commandModeOpen);
    valid = true;
  }

  if (strncmp(command, "close", 5) == 0) {
      cmd = commandModeClose;
      len = sizeof(commandModeClose);
      valid = true;
  }
  
  if (strncmp(command, "firmware", 8) == 0) {
        cmd = commandFirmware;
        len = sizeof(commandFirmware);
        valid = true;
  }

  if (strncmp(command, "runmode", 7) == 0) {
    cmd = commandRunMode;
    len = sizeof(commandRunMode);
    mode_ = 0;
    valid = true;
  }
  
  if (strncmp(command, "debugmode", 79) == 0) {
    cmd = commandDebugMode;
    len = sizeof(commandDebugMode);
    mode_ = 2;
    valid = true;
  }

  if (strncmp(command, "reportmode", 10) == 0) {
    cmd = commandReportMode;
    len = sizeof(commandReportMode);
    mode_ = 1;
    valid = true;
  }

  if (!valid){
    ESP_LOGE(TAG, "Config command \"%s\" not found", command);
    return ESP_FAIL;
  }

  int txBytes = uart_write_bytes(UART_NUM_2, cmd, len);
  ESP_LOGI(TAG, "Wrote %d bytes", txBytes);

  if (cmd == commandModeOpen){
    vTaskDelay(100);
    uart_flush(UART_NUM_2);
    int txBytes = uart_write_bytes(UART_NUM_2, cmd, len);
    ESP_LOGI(TAG, "Wrote %d bytes AGAIN", txBytes);   
  }
  response_t cmd_response;
  char* data = (char*) malloc(RX_BUF_SIZE + 1);
  vTaskDelay(100);
  uart_get_buffered_data_len(UART_NUM_2, (size_t*)&length);
  ESP_LOGI(TAG, "Response of %d bytes waiting", length);
  int rxBytes = uart_read_bytes(UART_NUM_2, &cmd_response, length, 100 / portTICK_PERIOD_MS);

  if (rxBytes < 8){
    ESP_LOGE(TAG, "No response to command \"%s\"", command);
    return ESP_FAIL;
  }

  if (cmd_response.status != 0){
    return ESP_FAIL;
  }

  return ESP_OK;
}

bool RD03::get_motion_value(){
  return motion_;
}

void RD03::set_motion_value(bool motion){
  motion_ = motion;
  if (motion_ != last_motion_){
    motion_sensor_->publish_state(motion_);
    last_motion_ = motion_;
  }
}

int RD03::get_range_value(){
  return range_;
}

void RD03::set_range_value(int range ){
  range_ = range;
  if (range_ != last_range_){
    range_sensor_->publish_state((float)range_);
    last_range_ = range_;
  }
}

uint8_t RD03::get_mode(){
  return mode_;
}

void RD03::dump_config() {
  ESP_LOGCONFIG(TAG, "RD03:");
  ESP_LOGCONFIG(TAG, " TX Pin: %d", tx_pin_);
  ESP_LOGCONFIG(TAG, " RX Pin: %d", rx_pin_);
}

float RD03::get_setup_priority() const { return setup_priority::PROCESSOR; }

void RD03::set_tx_pin(int pin) {
  tx_pin_ = (gpio_num_t) pin;
};

void RD03::set_rx_pin(int pin) {
  rx_pin_ = (gpio_num_t) pin;
};

}  // namespace rd03
}  // namespace esphome

