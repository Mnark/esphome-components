#pragma once
#ifndef SDMMC_H
#define SDMMC_H
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include "esphome/core/defines.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/sdmmc_types.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

namespace esphome {
namespace sdmmc {

enum class State {
  UNKNOWN,
  UNAVAILABLE,
  IDLE,
  BUSY,
};

class SDMMC : public Component, public EntityBase  { // ,
 public:
  /* public API (derivated) */
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void set_command_pin(int pin);
  void set_clock_pin(int pin);
  void set_data_pin(int pin);
  void set_data_pins(std::array<int, 4> pins);
  void set_mount_point(std::string);
  std::string get_mount_point(void);
  State get_state(void);
  esp_err_t get_stat(const char *_uri, struct stat * status);
  esp_err_t write_file(const char *path, uint32_t len, void *data);
  esp_err_t write_file(const char *path, uint32_t len, std::vector<uint8_t> &data);
  uint64_t get_total_capacity(void);
  uint64_t get_used_capacity(void);
  uint64_t get_free_capacity(void);
  std::string card_status;
  char *info;

 protected:
  void set_state_(State state);
  sdmmc_card_t *card;
  gpio_num_t command_pin;
  gpio_num_t clock_pin;
  gpio_num_t data_pins[4];
  std::string mount_point_;
  State state_{State::UNKNOWN};
};

template<typename... Ts> class SDMMCWriteAction : public Action<Ts...> {
 public:
  SDMMCWriteAction(SDMMC *sdmmc) : sdmmc_(sdmmc) {}
  
  TEMPLATABLE_VALUE(uint32_t, length)
  TEMPLATABLE_VALUE(std::string, filename)

  void set_data_static(const std::vector<uint8_t> &data) {
    this->data_static_ = data;
    this->static_ = true;
  }

  void set_data_template_int(const std::function<uint8_t*(Ts...)> func) {
    this->data_func_int_ = func;
    this->static_ = false;
  }

  void play(Ts... x) override {
    this->path_ = this->filename_.value(x...).c_str();
    if (this->static_) {
      this->sdmmc_->write_file(this->path_, this->length_.value(x...), this->data_static_);
    } else {
      auto val = this->data_func_int_(x...);
      this->sdmmc_->write_file(this->path_, this->length_.value(x...), val);
      }
  }

 protected:
  SDMMC *sdmmc_;

  const char *path_;
  bool static_{false};
  std::function<uint8_t*(Ts...)> data_func_int_{};
  std::vector<uint8_t> data_static_{};
};

}  // namespace sdmmc
}  // namespace esphome
#endif
