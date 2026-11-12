#pragma once
#include "FS.h"

class LittleFSFS : public fs::FS {
 public:
  bool begin(bool formatOnFail = false, const char* basePath = "/littlefs", uint8_t maxOpenFiles = 10, const char* partitionLabel = "spiffs") {
    (void)formatOnFail; (void)basePath; (void)maxOpenFiles; (void)partitionLabel;
    return true;
  }
  bool format() { return true; }
};

inline LittleFSFS LittleFS;
