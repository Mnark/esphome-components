
// #ifndef SOC_SDMMC_USE_GPIO_MATRIX
// #define SOC_SDMMC_USE_GPIO_MATRIX 1
// #endif
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "soc/soc_caps.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esphome/core/log.h"
#include "sdmmc.h"

namespace esphome {
namespace sdmmc {

static const char *TAG = "SDMMC";
//char mount_point[] = MOUNT_POINT;

static const LogString *sdmmc_state_to_string(State state) {
  switch (state) {
    case State::UNKNOWN:
      return LOG_STR("UNKNOWN"); 
    case State::UNAVAILABLE:
      return LOG_STR("UNAVAILABLE");
    case State::IDLE:
      return LOG_STR("IDLE");
    case State::BUSY:
      return LOG_STR("BUSY");
    default:
      return LOG_STR("UNKNOWN");
  }
};

void SDMMC::setup() {
  ESP_LOGI(TAG, "Initialising SDMMC peripheral...");
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_1;
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_PROBING;
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
  slot_config.clk = (gpio_num_t)clock_pin;
  slot_config.cmd = command_pin;
  slot_config.d0 = data_pins[0];
  #endif
  
  gpio_set_pull_mode(command_pin, GPIO_PULLUP_ONLY);      // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode(data_pins[0], GPIO_PULLUP_ONLY);     // D0, needed in 4- and 1-line modes
  gpio_set_pull_mode(clock_pin, GPIO_PULLUP_ONLY);  // D3, needed in 4- and 1-line modes

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = true, 
      .max_files = 5, 
      .allocation_unit_size = 4 * 1024};
  
  esp_err_t ret;

  ret = esp_vfs_fat_sdmmc_mount(mount_point_.c_str(), &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount filesystem. ");
    } else {
      ESP_LOGE(TAG,
               "Failed to initialize the card (%s). "
               "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    card_status = "Not Detected";
    this->set_state_(State::UNAVAILABLE);
    return;
  }

  this->set_state_(State::IDLE);
  card_status = "Card Detected";
  return;
}

void SDMMC::loop(void) {}

void SDMMC::dump_config() {
  ESP_LOGCONFIG(TAG, "SDMMC:");
  ESP_LOGCONFIG(TAG, " Command Pin: %d", command_pin);
  ESP_LOGCONFIG(TAG, " Clock Pin: %d", clock_pin);
  ESP_LOGCONFIG(TAG, " Data Pin: %d", data_pins[0]);
  ESP_LOGCONFIG(TAG, " Card Status: %s ", LOG_STR_ARG(sdmmc_state_to_string(this->state_)));
  if (this->state_ == (State::IDLE)){
    ESP_LOGCONFIG(TAG, " Card Name: %s ", card->cid.name);
    if (card->real_freq_khz == 0) {
       ESP_LOGCONFIG(TAG, " Card Speed: N/A");
    } else {
        const char *freq_unit = card->real_freq_khz < 1000 ? "kHz" : "MHz";
        const float freq = card->real_freq_khz < 1000 ? card->real_freq_khz : card->real_freq_khz / 1000.0;
        const char *max_freq_unit = card->max_freq_khz < 1000 ? "kHz" : "MHz";
        const float max_freq = card->max_freq_khz < 1000 ? card->max_freq_khz : card->max_freq_khz / 1000.0;
        ESP_LOGCONFIG(TAG, " Card Speed:  %.2f %s (limit: %.2f %s)%s", freq, freq_unit, max_freq, max_freq_unit, card->is_ddr ? ", DDR" : "");
    }
    ESP_LOGCONFIG(TAG, " Card Size: %llu MB", (uint64_t) (get_total_capacity() / (1024 * 1024)));
    ESP_LOGCONFIG(TAG, " Free Space: %llu MB", ((uint64_t) get_free_capacity() / (1024 * 1024)));
  }
}

float SDMMC::get_setup_priority() const { return setup_priority::HARDWARE; }

void SDMMC::set_state_(State state) {
  State old_state = this->state_;
  this->state_ = state;
  ESP_LOGD(TAG, "State changed from %s to %s", LOG_STR_ARG(sdmmc_state_to_string(old_state)),
           LOG_STR_ARG(sdmmc_state_to_string(state)));
}

State SDMMC::get_state(void){
 return this->state_;
};

void SDMMC::set_command_pin(int pin) {
  command_pin = (gpio_num_t) pin;
};

void SDMMC::set_clock_pin(int pin) {
  clock_pin = (gpio_num_t) pin;
};

void SDMMC::set_data_pin(int pin) {
  data_pins[0] = (gpio_num_t) pin;
};

void SDMMC::set_data_pins(std::array<int, 4> pins) {
  ESP_LOGI(TAG, "Data Pin set to %d", pins[0]);
  data_pins[0] = (gpio_num_t) pins[0];
  data_pins[1] = (gpio_num_t) pins[1];
  data_pins[2] = (gpio_num_t) pins[2];
  data_pins[3] = (gpio_num_t) pins[3];
}

void SDMMC::set_mount_point(std::string mount_point){
  mount_point_ = "/" + mount_point;
}

std::string  SDMMC::get_mount_point(void) {
  return mount_point_;
}

esp_err_t SDMMC::get_stat(const char *_uri, struct stat * status){
  struct stat st = {};
  char *buf;
  size_t buf_len = 64;
  buf = (char *) malloc(buf_len);
  ESP_LOGI(TAG, "get_stat uri: '%s'",_uri);
   sprintf(buf, "%s%s", mount_point_, _uri);

  ESP_LOGI(TAG, "Checking Status of %s on file system", buf);

  if (stat(buf, &st) == -1) {
    ESP_LOGW(TAG, "%s doesn't exist", buf);
    free (buf);
    return ESP_FAIL;
  }
  if (st.st_size > 0){
    ESP_LOGI(TAG, "uri %s is file: ",_uri);
  }else{
     ESP_LOGI(TAG, "uri %s is directory: ",_uri);  
  }
  memcpy(status, &st, sizeof(struct stat));
  free (buf);
  return ESP_OK;
}

esp_err_t SDMMC::write_file(const char *path, uint32_t len, void *data) {
  char *fullpath = new char[64];
  strcpy(fullpath, mount_point_.c_str());
  if (strncmp(path, "/", 1) != 0) {
    strcat(fullpath, "/");
  }
  strcat(fullpath, path);
  ESP_LOGI(TAG, "Writing File %s Length %lu", fullpath, len);
  FILE *f = fopen(fullpath, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file %s for writing", path);
    return ESP_FAIL;
  }
  fwrite(data, len, 1, f);
  uint8_t *imagedata = (uint8_t *) data;
  fclose(f);
  ESP_LOGI(TAG, "File %s written", fullpath);
  return ESP_OK;
}

esp_err_t SDMMC::write_file(const char *path, uint32_t len, std::vector<uint8_t> &data) {
  void *void_data;
  void_data = static_cast<void *>(&data);
  write_file(path, len, void_data);
  return ESP_OK;
}

uint64_t SDMMC::get_total_capacity(){
    return (uint64_t)card->csd.capacity * card->csd.sector_size;
}
  
uint64_t SDMMC::get_used_capacity(){
  return get_total_capacity() - get_free_capacity();
}
  
uint64_t SDMMC:: get_free_capacity(){
  FATFS *fs;
  DWORD fre_clust;

  auto res = f_getfree(this->mount_point_.c_str(), &fre_clust, &fs);
  if (res) {
    ESP_LOGE(TAG, "Failed to read card information");
    return 0;
  }

  return ( uint64_t)fre_clust * fs->csize * FF_SS_SDCARD;
}

}  // namespace sdmmc
}  // namespace esphome

