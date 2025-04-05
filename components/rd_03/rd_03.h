#pragma once
#ifndef RD03_H
#define RD03_H
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include "esphome/core/defines.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

namespace esphome {
namespace rd03 {

class RD03 : public sensor::Sensor, binary_sensor::BinarySensor, public Component {
    public:
  /* public API (derivated) */
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void set_tx_pin(int pin);
  void set_rx_pin(int pin);
  bool get_motion_value();
  void set_motion_value(bool);
  int get_range_value();
  void set_range_value(int);
  void set_range_sensor(sensor::Sensor *range_sensor) { range_sensor_ = range_sensor; }
  void set_motion_sensor(binary_sensor::BinarySensor *motion_sensor) { motion_sensor_ = motion_sensor; }
  uint8_t get_mode();
  
  esp_err_t config_command( const char* command );
  float distance = 0;


 protected:
  sensor::Sensor *range_sensor_;
  binary_sensor::BinarySensor *motion_sensor_;
  bool motion_ = false;
  bool last_motion_ = false;
  int range_ = 0;
  int last_range_ = 0;
  gpio_num_t tx_pin_;
  gpio_num_t rx_pin_;
  uint8_t mode_ = 0;
  char * data_;
};


}  // namespace rd03
}  // namespace esphome
#endif
