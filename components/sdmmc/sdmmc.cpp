
// #ifndef SOC_SDMMC_USE_GPIO_MATRIX
// #define SOC_SDMMC_USE_GPIO_MATRIX 1
// #endif
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/file.h>
#include "soc/soc_caps.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esphome/core/log.h"
#include "esp_timer.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "sdmmc.h"

std::map<std::string, current_file_t> active_avi_processes;

uint64_t padding = 0;

typedef struct {
  char *fullpath;
  uint32_t len;
  void *data;
}write_file_args_t;

write_file_args_t args;

typedef struct {
  uint32_t length;
  void *data;
} frame_image_t;

namespace esphome {
namespace sdmmc {

static const char *TAG = "SDMMC";

esp_err_t get_dimensions(void * image_v, int len, uint16_t *width, uint16_t *height){
  char * image = (char *) image_v;
  int iPos = 0;

  for(int i=0; i<len; i++) {
    if((image[i]==0xFF) && (image[i+1]==0xC0) )
    {
        iPos=i;         
        break;
    }       
  }   

  if(iPos == 0){
    return ESP_FAIL;
  }
  iPos = iPos + 5;
  *height = image[iPos]<<8|image[iPos+1];
  *width = image[iPos+2]<<8|image[iPos+3];
  return ESP_OK;
}

const char *SDMMC::sdmmc_state_to_string(State state) {
  switch (state) {
    case State::UNKNOWN:
      return "Unknown"; 
    case State::UNAVAILABLE:
      return "Unavailable";
    case State::IDLE:
      return "Idle";
    case State::BUSY:
      return "Busy";
    default:
      return "Unknown";
  }
};

void SDMMC::setup() {
  ESP_LOGI(TAG, "Initialising SDMMC peripheral...");
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_1;
  host.flags = this->num_data_pins_ == 4 ? SDMMC_HOST_FLAG_4BIT:SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
  slot_config.clk = (gpio_num_t)clock_pin;
  slot_config.cmd = command_pin;
  #endif
  if (this->num_data_pins_ == 1){
  slot_config.width = 1;
  #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
  slot_config.d0 = data_pins[0];
  #endif
  }else{
  slot_config.width = 4;
  #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
  slot_config.d0 = data_pins[0];
  slot_config.d1 = data_pins[1];
  slot_config.d2 = data_pins[2];
  slot_config.d3 = data_pins[3];
  #endif
  }
  
  gpio_set_pull_mode(command_pin, GPIO_PULLUP_ONLY);      // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode(clock_pin, GPIO_PULLUP_ONLY);  // D3, needed in 4- and 1-line modes
  gpio_set_pull_mode(data_pins[0], GPIO_PULLUP_ONLY);     // D0, needed in 4- and 1-line modes
  if (this->num_data_pins_ == 4){
    gpio_set_pull_mode(data_pins[1], GPIO_PULLUP_ONLY);  
    gpio_set_pull_mode(data_pins[2], GPIO_PULLUP_ONLY); 
    gpio_set_pull_mode(data_pins[3], GPIO_PULLUP_ONLY); 
  }
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = true, 
      .max_files = 5, 
      .allocation_unit_size = 16 * 1024};
  
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
    this->set_state(State::UNAVAILABLE);
    return;
  }

  this->set_state(State::IDLE);
  return;
}

void  SDMMC::update() {
  if (state_ != last_state_){
    card_sensor_->publish_state(sdmmc_state_to_string(state_));    
    last_state_ = state_;
  }
}

void SDMMC::dump_config() {
  ESP_LOGCONFIG(TAG, "SDMMC:");
  ESP_LOGCONFIG(TAG, " Command Pin: %d", command_pin);
  ESP_LOGCONFIG(TAG, " Clock Pin: %d", clock_pin);
  ESP_LOGCONFIG(TAG, " Mode: %d pin", this->num_data_pins_);
  ESP_LOGCONFIG(TAG, " Data Pin: %d", data_pins[0]);
  ESP_LOGCONFIG(TAG, " Card Status: %s ", sdmmc_state_to_string(this->state_));
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

float SDMMC::get_setup_priority() const {
  return setup_priority::HARDWARE; 
}

void SDMMC::set_state(State state) {
  state_ = state;
  if (state_ != last_state_){
    card_sensor_->publish_state(sdmmc_state_to_string(state_));
    last_state_ = state_;
  }
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
    this->data_pins[0] = (gpio_num_t)pin;
    this->num_data_pins_ = 1;
}

void SDMMC::set_data_pins(int pin1, int pin2, int pin3, int pin4) {
    this->data_pins[0] = (gpio_num_t)pin1;
    this->data_pins[1] = (gpio_num_t)pin2;
    this->data_pins[2] = (gpio_num_t)pin3;
    this->data_pins[3] = (gpio_num_t)pin4;
    this->num_data_pins_ = 4;
}

void SDMMC::set_mount_point(std::string mount_point){
  mount_point_ = "/" + mount_point;
}

std::string  SDMMC::get_mount_point(void) {
  return mount_point_;
}

esp_err_t SDMMC::path_to_uri(const char *path, char *uri, bool create_dir){
  char temp[64];
  char * pch;
  
  strcpy(temp, path);

  strcpy(uri, mount_point_.c_str());
  if (strncmp(temp, "/", 1) != 0) {
    strcat(uri, "/");
  }

  pch = strtok (temp,"/");
  while (pch != NULL)
  {
    if (strcmp(pch, mount_point_.c_str()) == 0){
      strcat(uri, "/");
    } else {
      if (strncmp(path, pch, strlen(pch)) == 0){
        DIR* dir = opendir(uri);
        if (dir) {
          strcat(uri, pch);
          strcat(uri, "/");
          closedir(dir);
        } else if (ENOENT == errno) {
          if (create_dir){
            strcat(uri, pch);
            mkdir(uri, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            strcat(uri, "/");
          }else{
            return ESP_FAIL;
          }  
        } else {
            /* opendir() failed for some other reason. */
            return ESP_FAIL;
        }
      }else{
        strcat(uri, pch);
      }
    }
    pch = strtok (NULL, "/");
  }
  return ESP_OK;
}

FILE* create_file(char * fullpath, const char *permission, bool create_directory){
  const char s[2] = "/";
  char *token;
  char *partpath;
  char *startpath;
  DIR* dir;
  struct stat st = {};
  int status;
  
  int ret = stat(fullpath, &st);
  if (ret == 0){
    int version = 0;
    char *newpath = new char[64];
    strcat(fullpath, "\0");
    char *extPtr = strrchr(fullpath, '.');
    char startStr[64];
    strncpy(startStr, fullpath, strlen(fullpath) - strlen(extPtr));
    startStr[strlen(fullpath) - strlen(extPtr)] = 0;
    while (ret == 0){
      version++;
      sprintf (newpath, "%s(%d)%s", startStr, version, extPtr);
      ret = stat(newpath, &st);
    }
    strcpy(fullpath, newpath);
  }

  FILE *f = fopen(fullpath, permission);
  if (f != NULL) {
    return f;
  }
  if (create_directory == false){
    return NULL;
  }

  partpath = (char *)calloc(1, strlen(fullpath));
  startpath = (char *)calloc(1, strlen(fullpath));
  strcpy(startpath, fullpath);
  
  token = strtok(startpath, s);
  while( token != NULL ) {
    strcat(partpath, "/");
    strcat(partpath, token);
    if (strcmp(fullpath, partpath) == 0){
      break;
    }
    dir = opendir(partpath);
    if (dir) {
      closedir(dir);
    } else if (ENOENT == errno) {
      status = mkdir(partpath, 0777);
      if (status != 0){
        ESP_LOGE(TAG, "Failed to create directory: %s", partpath );
      } else {
        ESP_LOGI(TAG, "Created directory: %s", partpath );
      }

    } else {
      ESP_LOGE(TAG, "Failed to open directory: %s", partpath );
    }
    token = strtok(NULL, s);
  }

  free (partpath);
  free (startpath);

  f = fopen(fullpath, permission);
  if (f != NULL) {
    return f;
  }
  return NULL;
}

void SDMMC::write_file_async(void *arg){
  write_file_args_t *args = (write_file_args_t *)arg;
  FILE *f = create_file(args->fullpath, "wb", true);
  //FILE *f = fopen(args->fullpath, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file %s for writing", args->fullpath);
    return;
  }
  fwrite(args->data, 1, args->len, f);
  ESP_LOGI("async_write", "File %s saved", args->fullpath);
  fclose(f);
  free(args->data);
  vTaskDelete(NULL);
  return;
}

esp_err_t SDMMC::finalise_avi_process(current_file_t *current_file) {
  
  int64_t time_now = esp_timer_get_time();
  int64_t elapsed_time = time_now - current_file->start;
  ESP_LOGI(TAG,"Video time %llu", elapsed_time);
  
  #ifdef SDMMC_AVI_HAS_INDEX
      current_file->index.fcc = {'i','d','x','1'};
      current_file->index.cb = current_file->entries.size() * sizeof(_avioldindex_entry); 
      fwrite(&current_file->index, 1, sizeof(AVIOLDINDEX), current_file->handle);
      current_file->size += sizeof(AVIOLDINDEX);

      for(_avioldindex_entry& e : current_file->entries)
      {
        fwrite(&e, 1, sizeof(_avioldindex_entry), current_file->handle);
        current_file->size += sizeof(_avioldindex_entry);
      }
  #endif
    //pad the file so that it is a multiple of 8 bytes
    if (current_file->size % 8 > 0){
      int pad = 8 - (current_file->size % 8);
      fwrite(&padding, 1, pad, current_file->handle);
      current_file->size += pad;
    }
    
    fclose(current_file->handle);
    current_file->handle = fopen(current_file->filename.c_str(), "r+b");
    
    current_file->header.dwFileSize  = current_file->size  - 8 ;
  
 
    current_file->header.dwMicroSecPerFrame = elapsed_time / current_file->frame_count;

    current_file->header.dwMaxBytesPerSec = current_file->header.dwWidth * current_file->header.dwHeight * (1000000 / current_file->header.dwMicroSecPerFrame) * 1.5;
    current_file->header.dwTotalFrames = current_file->frame_count;
    current_file->header.dwStrh.dwSuggestedBufferSize = current_file->header.dwSuggestedBufferSize;
    current_file->header.dwStrh.dwRate = (uint32_t)((uint64_t)current_file->header.dwStrh.dwScale * current_file->frame_count * 1000000 / elapsed_time)  ;
    
     ESP_LOGI(TAG, "Scale: %d Rate: %d",current_file->header.dwStrh.dwScale,current_file->header.dwStrh.dwRate );
     current_file->header.dwStrh.dwLength =   current_file->header.dwStrh.dwRate * elapsed_time / (1000000 * current_file->header.dwStrh.dwScale) ;
    //current_file->header.dwStrh.dwLength =  (uint32_t)elapsed_time / current_file->header.dwStrh.dwScale;
   
    fwrite(&current_file->header, 1, sizeof(AVIHeader), current_file->handle);

    fclose(current_file->handle);
    ESP_LOGI(TAG, "File %s complete. Frame count: %d Time: %llu secs",
      current_file->key.c_str(),
      current_file->frame_count,
      elapsed_time / 1000000);
    if (current_file->async){
      vQueueDelete(current_file->queue);
    }
    if (current_file->buffer_len > 0){
      free(current_file->buffer);
    }
    active_avi_processes.erase(current_file->key);
    return ESP_OK;
}

void SDMMC::avi_process(void *arg){
  current_file_t *current_file = (current_file_t *)arg;
  frame_image_t  current_image;
  static const char *TAG = "SDMMC_AVI";
  
  while ( true ){
    if (xQueuePeek(current_file->queue, &current_image, 0) == pdPASS){
      if (current_image.length == 0){
        xQueueReceive(current_file->queue, &current_image, 0);
        break;
      }
      if (!current_file->initialised){
        uint16_t width, height;
        esp_err_t dim = get_dimensions(current_image.data, current_image.length, &width, &height);
        if (dim != ESP_OK){
          ESP_LOGE(TAG, "JPG dimensions Not Found");
        }
        current_file->header.dwWidth = width;
        current_file->header.dwHeight = height;
        current_file->header.dwStrh.rcFrame = {0, 0, width, height};
        current_file->header.dwStrf.biWidth = width;
        current_file->header.dwStrf.biHeight = height;
        current_file->header.dwStrf.biSizeImage = (uint32_t)width * height * current_file->header.dwStrf.biBitCount / 8;
      }
      
      MOVIHeader movi = { {'0','0','d','c'}, current_image.length};
      current_image.length += current_image.length % 2;
      fwrite(&movi, 1, sizeof(MOVIHeader), current_file->handle);
      fwrite(current_image.data, 1, current_image.length, current_file->handle);
      //fwrite(&padding, 1, pad, current_file->handle);

  #ifdef SDMMC_AVI_HAS_INDEX
        _avioldindex_entry item;
        item.dwChunkId = {'0', '0', 'd', 'c'} ;
        item.dwFlags = 16;
        item.dwOffset = current_file->size;
        item.dwSize = current_image.length;
        current_file->entries.push_back(item);
  #endif
      if (current_image.length  > current_file->header.dwSuggestedBufferSize)
        current_file->header.dwSuggestedBufferSize = current_image.length;
      current_file->frame_count ++;
      current_file->size += (current_image.length + sizeof(MOVIHeader));
      current_file->header.dwMoviSize += (current_image.length + sizeof(MOVIHeader));
      xQueueReceive(current_file->queue, &current_image, 10);
    }
    vTaskDelay(1);
  }
  esp_err_t err = finalise_avi_process(current_file);
  if (err != ESP_OK){
    ESP_LOGE(TAG, "Failed to finalise avi file");
  } 
  vTaskDelete(NULL);
}

esp_err_t SDMMC::write_file(const char *path, uint32_t len, void *data) {
  char *fullpath = new char[64];
  bool save_sync = false;
  struct stat st = {};
  
  esp_err_t err= path_to_uri(path, fullpath, true);
  if (err != ESP_OK){
    return err;
  }

  if (!save_sync){
    uint32_t* mem_image = (uint32_t*) malloc(len);
    
    if (mem_image == NULL){
      ESP_LOGW(TAG, "Failed to allocate memory for image. Saving Synchronusly");
      save_sync = true;
    } else {
      memcpy(mem_image, data, len);

      args = {
        .fullpath = fullpath,
        .len = len,
        .data = mem_image
      };

      BaseType_t xReturned = xTaskCreate( write_file_async, "write_file_async", 3072, (void *)&args, tskIDLE_PRIORITY + 1, NULL );
      if (xReturned != pdPASS){
        ESP_LOGW(TAG, "Failed to create async task. Saving Synchronusly");
        free(mem_image);
        save_sync = true;
      }
    }
  }

  
  if (save_sync){
    uint64_t start = esp_timer_get_time();
    //FILE *f = fopen(fullpath, "wb");
    FILE *f = create_file(fullpath, "wb", true);
    if (f == NULL) {
      ESP_LOGE(TAG, "Failed to open file %s for writing", path);
      return ESP_FAIL;
    }
    fwrite(data, 1, len, f);
    uint8_t *imagedata = (uint8_t *) data;
    fclose(f);
    uint64_t end = esp_timer_get_time();
    ESP_LOGI(TAG, "File %s saved. Time: %llu", fullpath, end - start);
  }

  return ESP_OK;
}

esp_err_t SDMMC::write_file(const char *path, uint32_t len, std::vector<uint8_t> &data) {
  void *void_data;
  void_data = static_cast<void *>(&data);
  write_file(path, len, void_data);
  return ESP_OK;
}

esp_err_t SDMMC::initialise_avi_process(const char *path, uint32_t len, void *data, bool write_async ) {
  struct stat st = {};
  char *fullpath = new char[64];
  current_file_t current_file;

  esp_err_t err= path_to_uri(path, fullpath, true);
  if (err != ESP_OK){
    return err;
  }

  current_file.async = write_async;
  current_file.handle  = create_file(fullpath, "wb", true);
  //current_file.handle = fopen(fullpath, "ab");
  if (current_file.handle == NULL) {
    ESP_LOGE(TAG, "Failed to open file %s for writing", fullpath);
    return ESP_FAIL;
  }
  set_state(State::BUSY);
  if(current_file.async){
    current_file.queue = xQueueCreate(1, sizeof(frame_image_t));
    if (current_file.queue == NULL){
      ESP_LOGE(TAG, "Failed to create Queue");
    }
  }

  current_file.filename = fullpath;
  current_file.key = path;
  current_file.buffer_len = 0;
  current_file.header = default_header;

  fwrite(&current_file.header, 1, sizeof(AVIHeader), current_file.handle);
  current_file.frame_count = 0;
  current_file.start = esp_timer_get_time();
  current_file.size = sizeof(AVIHeader);
  #ifdef SDMMC_AVI_HAS_INDEX
  current_file.has_index = true;
  #else
  current_file.has_index = false;
  #endif 
  
  const auto [it, success2] = active_avi_processes.insert({path, current_file});
  if(success2 != true){
    ESP_LOGE(TAG, "Failed to insert process into process map");
    return ESP_FAIL;
  }
  
  if(current_file.async){
    BaseType_t created =  xTaskCreate(avi_process, "Avi Task", 4 * 1024, &active_avi_processes[path], tskIDLE_PRIORITY + 6, &current_file.task);
    if (created != pdPASS){
      ESP_LOGE(TAG,"Failed to create .avi task");
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

esp_err_t SDMMC::write_avi(const char *path, uint32_t len, void *data) {
  current_file_t *current_file;
  BaseType_t sent;
  frame_image_t qi;
  //int64_t time_now = esp_timer_get_time();
  
  if (active_avi_processes.find(path) == active_avi_processes.end()){
    esp_err_t err = initialise_avi_process(path, len, data, true);
    if (err != ESP_OK){
      ESP_LOGE(TAG, "Failed to initialise avi process");
      return err;
    }
  }
  current_file = &active_avi_processes[path];
  // int64_t elapsed_time = esp_timer_get_time() - time_now;
  // ESP_LOGI(TAG,"Time to check existance %llu", elapsed_time);

  if (current_file->async){
    if (len == 0){
      qi = {0, NULL};
      sent = xQueueSend(current_file->queue, (void*) &qi , portMAX_DELAY);
      set_state(State::IDLE);
    } else {
      if (uxQueueSpacesAvailable(current_file->queue ) > 0){
        if (len > current_file->buffer_len){
          if (current_file->buffer_len == 0){
            current_file->buffer = heap_caps_malloc(len * 1.2, MALLOC_CAP_SPIRAM);
          }else{
            current_file->buffer = heap_caps_realloc(current_file->buffer, len * 1.2, MALLOC_CAP_SPIRAM);
          }
          if (current_file->buffer == NULL){
            ESP_LOGE(TAG, "Failed to allocate memory for avi buffer");
            current_file->buffer_len = 0;
            return ESP_FAIL;
          }else{
            current_file->buffer_len = len * 1.2;
          }
        }

        memcpy(current_file->buffer, data, len);
        qi = {len, current_file->buffer};
        sent = xQueueSend(current_file->queue, &qi, 0);
        if (sent != pdPASS){
          ESP_LOGE(TAG, "Failed to send item to queue!");
        } 
      } else {
        ESP_LOGW(TAG, "No space on Queue... Dropping frame");
      }
    }
  } else {
    if (len == 0){
      finalise_avi_process(current_file);
      set_state(State::IDLE);
    }else{
      if (!current_file->initialised){
        uint16_t width, height;
        esp_err_t dim = get_dimensions(data, len, &width, &height);
        if (dim != ESP_OK){
          ESP_LOGE(TAG, "JPG dimensions Not Found");
        }
        current_file->header.dwWidth = width;
        current_file->header.dwHeight = height;
        current_file->header.dwStrh.rcFrame = {0, 0, width, height};
        current_file->header.dwStrf.biWidth = width;
        current_file->header.dwStrf.biHeight = height;
        current_file->header.dwStrf.biSizeImage = (uint32_t)width * height * current_file->header.dwStrf.biBitCount / 8;
      }
      int pad = 2 - (len % 2);
      MOVIHeader movi = { {'0','0','d','c'}, len + pad};
      
      fwrite(&movi, 1, sizeof(MOVIHeader), current_file->handle);

      fwrite(data, 1, len, current_file->handle);

      fwrite(&padding, 1, pad, current_file->handle);

  #ifdef SDMMC_AVI_HAS_INDEX
      _avioldindex_entry item;
      item.dwChunkId = {'0', '0', 'd', 'c'} ;
      item.dwFlags = 16;
      item.dwOffset = current_file->size;
      item.dwSize = len;
      current_file->entries.push_back(item);
  #endif

      if (len + pad  > current_file->header.dwSuggestedBufferSize)
        current_file->header.dwSuggestedBufferSize = len + pad;
      current_file->frame_count++;
      current_file->size += (len + sizeof(MOVIHeader) + pad);
      current_file->header.dwMoviSize += (len + sizeof(MOVIHeader) + pad);
    }   
  }
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

