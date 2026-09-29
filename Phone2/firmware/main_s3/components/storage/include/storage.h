#pragma once
// LittleFS helpers: crash-safe file replacement and PSRAM-friendly allocation.
#include <stddef.h>
#include <stdint.h>

namespace storage {

bool begin();                 // mounts LittleFS (formats on first use)
bool mounted();
void usage(size_t* usedBytes, size_t* totalBytes);
bool formatAll();

// Writes `path` atomically: data goes to path.tmp, then replaces the original.
// `writer` is called with a sink that appends bytes; return false to abort.
class Sink {
 public:
  virtual bool write(const void* data, size_t n) = 0;
  virtual ~Sink() {}
};
class Source {
 public:
  virtual size_t read(void* data, size_t n) = 0;  // returns bytes read
  virtual ~Source() {}
};

using WriterFn = bool (*)(Sink& out, void* ctx);
using ReaderFn = bool (*)(Source& in, size_t fileSize, void* ctx);

bool saveFile(const char* path, WriterFn writer, void* ctx);
bool loadFile(const char* path, ReaderFn reader, void* ctx);  // also recovers path.tmp
bool removeFile(const char* path);

// Allocation that prefers PSRAM (falls back to internal RAM).
void* allocLarge(size_t bytes);
bool hasPsram();

// Common file header
struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recSize;
  uint32_t count;
};

// Tiny helper to defer flash writes until things are quiet.
struct Deferred {
  bool dirty = false;
  uint32_t changedMs = 0;
  void touch();
  // True when a save should happen now.
  bool due(uint32_t nowMs, bool allowWrite) const;
};

}  // namespace storage
