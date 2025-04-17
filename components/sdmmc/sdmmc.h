#pragma once
#ifndef SDMMC_H
#define SDMMC_H
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include "esphome/core/defines.h"

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
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
#include "ff.h"

//#define SDMMC_AVI_HAS_INDEX 

typedef struct {
char fourcc [4];
} FOURCC;


typedef struct{
  char ckID[4];
  uint32_t ckSize;
  char ckData[4];
} AVIChunk;

typedef struct tagRECT {
  uint16_t left;
  uint16_t top;
  uint16_t right;
  uint16_t bottom;
} RECT, *PRECT, *NPRECT, *LPRECT;

typedef  struct {
    FOURCC dwChunkId;
    DWORD dwFlags;
    DWORD dwOffset;
    DWORD dwSize;
  } _avioldindex_entry;

typedef struct _avioldindex {
  FOURCC             fcc;
  DWORD              cb;
} AVIOLDINDEX;

typedef struct {
  FOURCC fccType;
  char fccHandler[4];
  DWORD  dwFlags;
  WORD   wPriority;
  WORD   wLanguage;
  DWORD  dwInitialFrames;
  DWORD  dwScale;
  DWORD  dwRate;
  DWORD  dwStart;
  DWORD  dwLength;
  DWORD  dwSuggestedBufferSize;
  DWORD  dwQuality;
  DWORD  dwSampleSize;
  RECT   rcFrame;
} AVIStreamHeader;

typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  long  biWidth;
  long  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  FOURCC biCompression;
  DWORD biSizeImage;
  long  biXPelsPerMeter;
  long  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct {
  char dwRIFF[4];
  DWORD dwFileSize;
  DWORD daAVI;
  FOURCC fccList;
  DWORD dwSize;
  FOURCC fccHdrl;
  DWORD dwAvih;
  DWORD dwAvihSize;
  DWORD dwMicroSecPerFrame;
  DWORD dwMaxBytesPerSec;
  DWORD dwPaddingGranularity;

  DWORD dwFlags;
  DWORD dwTotalFrames;
  DWORD dwInitialFrames;
  DWORD dwStreams;
  DWORD dwSuggestedBufferSize;

  DWORD dwWidth;
  DWORD dwHeight;

  DWORD dwReserved[4];
  FOURCC dwStrlList;
  DWORD dwStrlSize;
  FOURCC dwStrl;
  
  FOURCC fccStrh;
  DWORD dwStrhSize;
  AVIStreamHeader dwStrh;

  FOURCC fccStrf;
  DWORD dwStrfSize;
  BITMAPINFOHEADER dwStrf;

  DWORD dwMoviList;
  DWORD dwMoviSize;
  char dwMoviID[4];
} AVIHeader;

typedef struct {
//  char dwFourCC [4];
  char dwMoviType[4];
  DWORD dwSize;
} MOVIHeader;

typedef struct {
  std::string key;
  std::string filename;
  bool initialised = false;
  bool async;
  int size;
  int frame_count;
  int width;
  int height;
  int chunksize;
  int64_t start;
  AVIHeader header;
  AVIOLDINDEX index;
  bool has_index = false;
  std::vector<_avioldindex_entry> entries;
  FILE *handle;
  QueueHandle_t queue = NULL;
  int buffer_len;
  void * buffer;
  TaskHandle_t task;
}current_file_t;

const AVIHeader default_header = {
    .dwRIFF = {'R','I','F','F'},
    .daAVI = 541677121,
    .fccList = {'L','I','S','T'},
    .dwSize = 68 + sizeof(AVIStreamHeader) + sizeof(BITMAPINFOHEADER) + 28,
    .fccHdrl = {'h','d','r','l'},
    .dwAvih = 1751742049,
    .dwAvihSize = 56,
  #ifdef SDMMC_AVI_HAS_INDEX
    .dwFlags = 16, 
  #else
    .dwFlags = 0,
  #endif
    .dwInitialFrames = 1,
    .dwStreams = 1,
    .dwStrlList= {'L','I','S','T'},
    .dwStrlSize = sizeof(AVIStreamHeader) + sizeof(BITMAPINFOHEADER) + 20,
    .dwStrl = {'s','t','r','l'},
    .fccStrh = {'s','t','r','h'},
    .dwStrhSize = sizeof(AVIStreamHeader),
    .dwStrh = {
      .fccType ={'v','i','d','s'},
      .fccHandler = {'M','J','P','G'},
      .dwScale =30,
      .dwRate = 1000000, 
      .dwQuality = 10000,
    },  
    .fccStrf = {'s','t','r','f'},
    .dwStrfSize = sizeof(BITMAPINFOHEADER),
    .dwStrf = {
      .biSize = 40,
      .biPlanes = 1,
      .biBitCount = 24,
      .biCompression = {'M','J','P','G'},
    },
    .dwMoviList = 1414744396,
    .dwMoviID = {'m','o','v','i'}
};

namespace esphome {
namespace sdmmc {

enum class State {
  UNKNOWN,
  UNAVAILABLE,
  IDLE,
  BUSY,
};

class SDMMC : public PollingComponent {//public Component, public EntityBase  { // ,
 public:
  /* public API (derivated) */
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void set_command_pin(int pin);
  void set_clock_pin(int pin);
  void set_data_pin(int pin);
  void set_data_pins(int pin1, int pin2, int pin3, int pin4);
  void set_mount_point(std::string);
  std::string get_mount_point(void);
  esp_err_t path_to_uri(const char *, char *, bool);
  State get_state(void);
  esp_err_t write_file(const char *path, uint32_t len, void *data);
  esp_err_t write_file(const char *path, uint32_t len, std::vector<uint8_t> &data);
  esp_err_t write_avi(const char*, uint32_t, void*);
  uint64_t get_total_capacity(void);
  uint64_t get_used_capacity(void);
  uint64_t get_free_capacity(void);
  void set_card_sensor(text_sensor::TextSensor *card_sensor) { this->card_sensor_ = card_sensor; }
  //FILE*create_file(char * , const char *, bool );

  char *info;

 protected:
  void set_state(State state);
  static const char *sdmmc_state_to_string(State state);
  esp_err_t initialise_avi_process(const char *fullpath, uint32_t len, void *data, bool );
  static esp_err_t finalise_avi_process(current_file_t *current_file);

  static void write_file_async(void *arg);
  static void avi_process(void *arg);
  //esp_err_t get_dimensions(void * image_v, int len, uint16_t *width, uint16_t *height);
  sdmmc_card_t *card;
  gpio_num_t command_pin;
  gpio_num_t clock_pin;
  gpio_num_t data_pins[4];
  int num_data_pins_;
  std::string mount_point_;
  State state_{State::UNKNOWN};
  State last_state_{State::UNKNOWN};
  text_sensor::TextSensor *card_sensor_{nullptr};
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

template<typename... Ts> class SDMMCAppendAction : public Action<Ts...> {
 public:
  SDMMCAppendAction(SDMMC *sdmmc) : sdmmc_(sdmmc) {}
  
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
      ESP_LOGI("TEMP", "this is static");
      this->sdmmc_->write_avi(this->path_, this->length_.value(x...), this->data_static_);
    } else {
      ESP_LOGI("TEMP", "this is NOT static");
      auto val = this->data_func_int_(x...);
      this->sdmmc_->write_avi(this->path_, this->length_.value(x...), val);
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
