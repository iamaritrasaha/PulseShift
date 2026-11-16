#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>

#ifndef SeekSet
#define SeekSet 0
#endif
#ifndef SeekCur
#define SeekCur 1
#endif
#ifndef SeekEnd
#define SeekEnd 2
#endif

namespace fs {

struct MockFileData {
  std::vector<uint8_t> data;
};

inline std::map<std::string, std::shared_ptr<MockFileData>>& mockFsRegistry() {
  static std::map<std::string, std::shared_ptr<MockFileData>> registry;
  return registry;
}

inline void setMockFile(const std::string& path, const std::vector<uint8_t>& content) {
  auto file = std::make_shared<MockFileData>();
  file->data = content;
  mockFsRegistry()[path] = file;
}

inline void clearMockFiles() {
  mockFsRegistry().clear();
}

class File {
 public:
  File() : data_(nullptr), pos_(0), open_(false) {}
  File(std::shared_ptr<MockFileData> data, const char* mode)
      : data_(data), pos_(0), open_(true), mode_(mode ? mode : "r") {
    if (!data_) {
      open_ = false;
    } else if (mode && std::strchr(mode, 'a')) {
      pos_ = data_->data.size();
    }
  }

  operator bool() const { return open_ && data_ != nullptr; }

  size_t write(const uint8_t* buf, size_t size) {
    if (!open_ || !data_ || !buf || size == 0) return 0;
    if (pos_ + size > data_->data.size()) {
      data_->data.resize(pos_ + size);
    }
    std::memcpy(&data_->data[pos_], buf, size);
    pos_ += size;
    return size;
  }

  int read() {
    if (!open_ || !data_ || pos_ >= data_->data.size()) return -1;
    return data_->data[pos_++];
  }

  size_t read(uint8_t* buf, size_t size) {
    if (!open_ || !data_ || !buf || size == 0 || pos_ >= data_->data.size()) return 0;
    size_t avail = data_->data.size() - pos_;
    size_t toRead = (size < avail) ? size : avail;
    std::memcpy(buf, &data_->data[pos_], toRead);
    pos_ += toRead;
    return toRead;
  }

  int available() {
    if (!open_ || !data_ || pos_ >= data_->data.size()) return 0;
    return static_cast<int>(data_->data.size() - pos_);
  }

  int readBytes(char* buf, size_t size) {
    return static_cast<int>(read(reinterpret_cast<uint8_t*>(buf), size));
  }

  bool seek(uint32_t offset, int mode = SeekSet) {
    if (!open_ || !data_) return false;
    if (mode == SeekSet) {
      if (offset > data_->data.size()) return false;
      pos_ = offset;
      return true;
    } else if (mode == SeekCur) {
      if (pos_ + offset > data_->data.size()) return false;
      pos_ += offset;
      return true;
    } else if (mode == SeekEnd) {
      if (offset > data_->data.size()) return false;
      pos_ = data_->data.size() - offset;
      return true;
    }
    return false;
  }

  size_t position() const { return pos_; }

  void flush() {}

  void close() {
    open_ = false;
    data_ = nullptr;
    pos_ = 0;
  }

  size_t size() const {
    return (open_ && data_) ? data_->data.size() : 0;
  }

 private:
  std::shared_ptr<MockFileData> data_;
  size_t pos_;
  bool open_;
  std::string mode_;
};

class FS {
 public:
  bool begin() { return true; }
  void end() {}

  File open(const char* path = nullptr, const char* mode = "r") {
    if (!path) return File();
    auto& reg = mockFsRegistry();
    auto it = reg.find(path);
    if (it != reg.end()) {
      if (mode && std::strchr(mode, 'w') && !std::strchr(mode, '+')) {
        it->second->data.clear();
      }
      return File(it->second, mode);
    }
    if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a'))) {
      auto file = std::make_shared<MockFileData>();
      reg[path] = file;
      return File(file, mode);
    }
    return File();
  }

  bool exists(const char* path = nullptr) {
    if (!path) return false;
    auto& reg = mockFsRegistry();
    return reg.find(path) != reg.end();
  }

  bool remove(const char* path = nullptr) {
    if (!path) return false;
    auto& reg = mockFsRegistry();
    return reg.erase(path) > 0;
  }

  bool rename(const char* from = nullptr, const char* to = nullptr) {
    if (!from || !to) return false;
    auto& reg = mockFsRegistry();
    auto it = reg.find(from);
    if (it == reg.end()) return false;
    reg[to] = it->second;
    reg.erase(it);
    return true;
  }

  size_t totalBytes() { return 1000000; }
  size_t usedBytes() { return 0; }
};

}  // namespace fs

using fs::FS;
using fs::File;
