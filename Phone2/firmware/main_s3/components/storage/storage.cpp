#include "storage.h"

#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plog.h"
#include "s3_config.h"

namespace storage {

static bool s_mounted = false;

static bool mount(bool formatIfFailed) {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = FS_BASE_PATH;
  conf.partition_label = FS_PARTITION_LABEL;
  conf.format_if_mount_failed = formatIfFailed;
  conf.dont_mount = false;
  return esp_vfs_littlefs_register(&conf) == ESP_OK;
}

bool begin() {
  s_mounted = mount(true);
  if (!s_mounted) {
    LOG("FS", "LittleFS mount failed - data will not be saved (check partitions.csv)");
    return false;
  }
  size_t used, total;
  usage(&used, &total);
  LOG("FS", "LittleFS mounted: %u/%u KB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
  return true;
}

bool mounted() { return s_mounted; }

void usage(size_t* used, size_t* total) {
  *used = *total = 0;
  if (s_mounted) esp_littlefs_info(FS_PARTITION_LABEL, total, used);
}

bool formatAll() {
  esp_vfs_littlefs_unregister(FS_PARTITION_LABEL);
  bool ok = esp_littlefs_format(FS_PARTITION_LABEL) == ESP_OK;
  s_mounted = mount(true);
  return ok && s_mounted;
}

namespace {

void fullPath(const char* path, char* out, size_t cap) { snprintf(out, cap, "%s%s", FS_BASE_PATH, path); }

bool exists(const char* full) {
  struct stat st;
  return stat(full, &st) == 0;
}

class FileSink : public Sink {
 public:
  explicit FileSink(FILE* f) : f_(f) {}
  bool write(const void* data, size_t n) override { return fwrite(data, 1, n, f_) == n; }

 private:
  FILE* f_;
};

class FileSource : public Source {
 public:
  explicit FileSource(FILE* f) : f_(f) {}
  size_t read(void* data, size_t n) override { return fread(data, 1, n, f_); }

 private:
  FILE* f_;
};

}  // namespace

bool saveFile(const char* path, WriterFn writer, void* ctx) {
  if (!s_mounted) return false;
  char full[64], tmp[70];
  fullPath(path, full, sizeof full);
  snprintf(tmp, sizeof tmp, "%s.tmp", full);
  FILE* f = fopen(tmp, "wb");
  if (!f) {
    LOG("FS", "cannot write %s", tmp);
    return false;
  }
  FileSink sink(f);
  bool ok = writer(sink, ctx);
  ok = fflush(f) == 0 && ok;
  fclose(f);
  if (!ok) {
    unlink(tmp);
    LOG("FS", "write of %s failed (disk full?)", path);
    return false;
  }
  unlink(full);
  if (rename(tmp, full) != 0) {
    LOG("FS", "rename %s failed", path);
    return false;
  }
  return true;
}

bool loadFile(const char* path, ReaderFn reader, void* ctx) {
  if (!s_mounted) return false;
  char full[64], tmp[70];
  fullPath(path, full, sizeof full);
  snprintf(tmp, sizeof tmp, "%s.tmp", full);
  if (!exists(full) && exists(tmp)) rename(tmp, full);  // power was lost between unlink and rename
  FILE* f = fopen(full, "rb");
  if (!f) return false;
  struct stat st;
  size_t size = stat(full, &st) == 0 ? (size_t)st.st_size : 0;
  FileSource src(f);
  bool ok = reader(src, size, ctx);
  fclose(f);
  if (!ok) LOG("FS", "%s is damaged or from an older version - ignored", path);
  return ok;
}

bool removeFile(const char* path) {
  if (!s_mounted) return false;
  char full[64];
  fullPath(path, full, sizeof full);
  return unlink(full) == 0;
}

void* allocLarge(size_t bytes) {
  void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = calloc(1, bytes);
  return p;
}

bool hasPsram() { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0; }

void Deferred::touch() {
  dirty = true;
  changedMs = plogMillis();
}

bool Deferred::due(uint32_t nowMs, bool allowWrite) const {
  return dirty && allowWrite && nowMs - changedMs >= STORAGE_SAVE_DELAY_MS;
}

}  // namespace storage
