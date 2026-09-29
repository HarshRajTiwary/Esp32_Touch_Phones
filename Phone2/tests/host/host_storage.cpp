// PC implementation of storage.h (plain files in a temporary folder) so the real
// contacts / call log / message stores can be tested on the host.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "plog.h"
#include "s3_config.h"
#include "storage.h"

namespace storage {

static std::string base() {
  const char* t = getenv("TEMP");
  return std::string(t ? t : ".") + "/phone2_host_fs";
}

static std::string full(const char* path) { return base() + path; }

bool begin() {
#ifdef _WIN32
  mkdir(base().c_str());
#else
  mkdir(base().c_str(), 0755);
#endif
  return true;
}
bool mounted() { return true; }
void usage(size_t* used, size_t* total) { *used = 0, *total = 1 << 20; }
bool formatAll() { return true; }

namespace {
class FileSink : public Sink {
 public:
  explicit FileSink(FILE* f) : f_(f) {}
  bool write(const void* d, size_t n) override { return fwrite(d, 1, n, f_) == n; }
  FILE* f_;
};
class FileSource : public Source {
 public:
  explicit FileSource(FILE* f) : f_(f) {}
  size_t read(void* d, size_t n) override { return fread(d, 1, n, f_); }
  FILE* f_;
};
}  // namespace

bool saveFile(const char* path, WriterFn writer, void* ctx) {
  std::string tmp = full(path) + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  FileSink s(f);
  bool ok = writer(s, ctx);
  fclose(f);
  if (!ok) return false;
  remove(full(path).c_str());
  return rename(tmp.c_str(), full(path).c_str()) == 0;
}

bool loadFile(const char* path, ReaderFn reader, void* ctx) {
  FILE* f = fopen(full(path).c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  size_t size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  FileSource s(f);
  bool ok = reader(s, size, ctx);
  fclose(f);
  return ok;
}

bool removeFile(const char* path) { return remove(full(path).c_str()) == 0; }

void* allocLarge(size_t bytes) { return calloc(1, bytes); }
bool hasPsram() { return true; }

void Deferred::touch() {
  dirty = true;
  changedMs = plogMillis();
}

bool Deferred::due(uint32_t nowMs, bool allowWrite) const {
  return dirty && allowWrite && nowMs - changedMs >= STORAGE_SAVE_DELAY_MS;
}

}  // namespace storage

// Test helper: does the store file exist?
bool hostFileExists(const char* path) {
  struct stat st;
  return stat(storage::full(path).c_str(), &st) == 0;
}
void hostRemoveFile(const char* path) { remove(storage::full(path).c_str()); }
