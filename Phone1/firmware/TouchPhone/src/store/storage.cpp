#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "../config/app_config.h"
#include "../core/log.h"

namespace storage {

static bool s_mounted = false;

bool begin() {
  s_mounted = LittleFS.begin(true, FS_BASE_PATH, 10, FS_PARTITION_LABEL);
  if (!s_mounted) {
    LOG("FS", "LittleFS mount failed - data will not be saved (check the partition scheme)");
    return false;
  }
  size_t used, total;
  usage(&used, &total);
  LOG("FS", "LittleFS mounted: %u/%u KB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
  return true;
}

bool mounted() { return s_mounted; }

void usage(size_t* used, size_t* total) {
  *used = s_mounted ? LittleFS.usedBytes() : 0;
  *total = s_mounted ? LittleFS.totalBytes() : 0;
}

bool formatAll() {
  LittleFS.end();
  bool ok = LittleFS.format();
  s_mounted = LittleFS.begin(true, FS_BASE_PATH, 10, FS_PARTITION_LABEL);
  return ok && s_mounted;
}

namespace {
class FileSink : public Sink {
 public:
  explicit FileSink(File& f) : f_(f) {}
  bool write(const void* data, size_t n) override { return f_.write((const uint8_t*)data, n) == n; }

 private:
  File& f_;
};
class FileSource : public Source {
 public:
  explicit FileSource(File& f) : f_(f) {}
  size_t read(void* data, size_t n) override { return f_.read((uint8_t*)data, n); }

 private:
  File& f_;
};
}  // namespace

bool saveFile(const char* path, WriterFn writer, void* ctx) {
  if (!s_mounted) return false;
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    LOG("FS", "cannot write %s", tmp.c_str());
    return false;
  }
  FileSink sink(f);
  bool ok = writer(sink, ctx);
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    LOG("FS", "write of %s failed (disk full?)", path);
    return false;
  }
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    LOG("FS", "rename %s failed", path);
    return false;
  }
  return true;
}

bool loadFile(const char* path, ReaderFn reader, void* ctx) {
  if (!s_mounted) return false;
  String tmp = String(path) + ".tmp";
  if (!LittleFS.exists(path) && LittleFS.exists(tmp)) LittleFS.rename(tmp, path);  // power lost mid-save
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  FileSource src(f);
  bool ok = reader(src, f.size(), ctx);
  f.close();
  if (!ok) LOG("FS", "%s is damaged or from an older version - ignored", path);
  return ok;
}

bool removeFile(const char* path) {
  return s_mounted && LittleFS.remove(path);
}

void* allocLarge(size_t bytes) {
  void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = calloc(1, bytes);
  return p;
}

bool hasPsram() { return psramFound(); }

void Deferred::touch() {
  dirty = true;
  changedMs = millis();
}

bool Deferred::due(uint32_t nowMs, bool allowWrite) const {
  return dirty && allowWrite && nowMs - changedMs >= STORAGE_SAVE_DELAY_MS;
}

}  // namespace storage
