/*
 * FatFS-backed filesystem for Node using ESP-IDF VFS (SDMMC 4-line mode).
 */

#include "FileSystem.h"

#include "Adapters/node/platform/gpio.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/I_File.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <sstream>
#include <unistd.h>

namespace {
constexpr const char *kMountPoint = "/sdcard";
sdmmc_card_t *g_card = nullptr;

std::string ResolvePath(const std::string &cwd, const char *path) {
  if (!path) {
    return cwd;
  }
  // Absolute path: if already under the mount point, return as-is. Otherwise, map to the SD mount.
  constexpr size_t kMountLen = 7;  // strlen("/sdcard")
  if (path[0] == '/') {
    if (strncmp(path, kMountPoint, kMountLen) == 0) {
      return std::string(path);
    }
    return std::string(kMountPoint) + path;
  }
  std::string full = cwd;
  if (!full.empty() && full.back() != '/') {
    full.push_back('/');
  }
  full += path;
  return full;
}

bool EnsureParentDirs(const std::string &full_path) {
  // Create parent directories for a file path if they don't exist.
  auto pos = full_path.find_last_of('/');
  if (pos == std::string::npos || pos == 0) {
    return true;  // root-level file
  }
  std::string dir = full_path.substr(0, pos);
  struct stat st {};
  if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    return true;
  }
  // Recursively create parent.
  if (!EnsureParentDirs(dir)) {
    return false;
  }
  int rc = mkdir(dir.c_str(), 0777);
  if (rc == 0 || errno == EEXIST) {
    return true;
  }
  // FatFS sometimes rejects the mount prefix with EINVAL; retry without it.
  if (errno == EINVAL && dir.rfind(kMountPoint, 0) == 0) {
    std::string alt = dir.substr(strlen(kMountPoint));
    if (alt.empty()) {
      alt = "/";
    }
    rc = mkdir(alt.c_str(), 0777);
    if (rc == 0 || errno == EEXIST) {
      return true;
    }
  }
  return false;
}

bool MountCard() {
  if (g_card != nullptr) {
    return true;
  }

  // Use SDMMC host (4-line mode) for Node
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz for high speed

  // Configure 4-bit SD bus
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;  // 4-line mode  // Set to 1 for now
  slot_config.clk = (gpio_num_t)SD_CLK_PIN;
  slot_config.cmd = (gpio_num_t)SD_CMD_PIN;
  slot_config.d0 = (gpio_num_t)SD_D0_PIN;
  slot_config.d1 = (gpio_num_t)SD_D1_PIN;
  slot_config.d2 = (gpio_num_t)SD_D2_PIN;
  slot_config.d3 = (gpio_num_t)SD_D3_PIN;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 8,
      .allocation_unit_size = 16 * 1024,
      .disk_status_check_enable = false,
      .use_one_fat = false,
  };

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot_config,
                                          &mount_config, &g_card);
  if (ret != ESP_OK) {
    Trace::Error("FILESYSTEM", "SD mount failed: %d (%s)", ret, esp_err_to_name(ret));
    g_card = nullptr;
    return false;
  }
  Trace::Log("FILESYSTEM", "Mounted SD at %s", kMountPoint);
  sdmmc_card_print_info(stdout, g_card);
  return true;
}
} // namespace

NodeFileSystem::NodeFileSystem() {
  std::lock_guard<std::mutex> lock(mutex_);
  cwd_ = kMountPoint;
  MountCard();
}

FileHandle NodeFileSystem::Open(const char *name, const char *mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!MountCard()) {
    return FileHandle();
  }
  std::string full = ResolvePath(cwd_, name);
  // If writing/creating, ensure parent directories exist.
  if (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))) {
    if (!EnsureParentDirs(full)) {
      Trace::Error("FILESYSTEM", "EnsureParentDirs failed: %s errno:%d (%s)", full.c_str(), errno, strerror(errno));
      return FileHandle();
    }
  }
  FILE *f = fopen(full.c_str(), mode);
  if (f == nullptr) {
    int err = errno;
    Trace::Error("FILESYSTEM", "Open failed: %s mode:%s errno:%d (%s)", full.c_str(),
                 mode ? mode : "", err, strerror(err));
    // If FATFS didn't like the mount prefix, retry without it.
    if (err == EINVAL) {
      const size_t kMountLen = strlen(kMountPoint);
      if (full.rfind(kMountPoint, 0) == 0) {
        std::string alt = full.substr(kMountLen);
        if (alt.empty()) {
          alt = "/";
        }
        f = fopen(alt.c_str(), mode);
        if (f != nullptr) {
          Trace::Log("FILESYSTEM", "Open succeeded without mount prefix: %s", alt.c_str());
          return MakeFileHandle(new VfsFile(f));
        }
      }
    }
    return FileHandle();
  }
  return MakeFileHandle(new VfsFile(f));
}

bool NodeFileSystem::chdir(const char *path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (path == nullptr) {
    return false;
  }
  std::string newPath = ResolvePath(cwd_, path);
  struct stat st {};
  if (stat(newPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    cwd_ = newPath;
    return true;
  }
  Trace::Error("FILESYSTEM", "chdir failed: %s errno:%d (%s)", newPath.c_str(), errno, strerror(errno));
  // If trying to chdir to root and SD card is mounted, set cwd to mount point.
  if (strcmp(path, "/") == 0) {
    std::string fallback = std::string(kMountPoint);
    if (stat(fallback.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      cwd_ = fallback;
      Trace::Log("FILESYSTEM", "Fallback cwd to %s", fallback.c_str());
      return true;
    }
  }
  return false;
}

void NodeFileSystem::RefreshDir(const char *filter, bool subDirOnly,
                                bool includeHidden) {
  entries_.clear();
  DIR *dir = opendir(cwd_.c_str());
  if (!dir) {
    return;
  }
  while (true) {
    dirent *ent = readdir(dir);
    if (!ent) {
      break;
    }
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    std::string name(ent->d_name);
    bool isDir = (ent->d_type == DT_DIR);
    if (subDirOnly && !isDir) {
      continue;
    }
    const bool isHidden = !name.empty() && name.front() == '.';
    if (!includeHidden && isHidden) {
      continue;
    }
    if (filter && *filter) {
      // Convert name to lowercase for case-insensitive match
      std::string lowerName = name;
      for (auto &c : lowerName) {
        c = tolower(c);
      }
      if (lowerName.find(filter) == std::string::npos) {
        continue;
      }
    }
    std::string full = cwd_ + "/" + name;
    struct stat st {};
    uint64_t sz = 0;
    if (stat(full.c_str(), &st) == 0) {
      sz = st.st_size;
      isDir = S_ISDIR(st.st_mode);
    }
    entries_.push_back({name, isDir, sz});
  }
  closedir(dir);
}

void NodeFileSystem::list(etl::ivector<int> *fileIndexes, const char *filter,
                          bool subDirOnly, bool includeHidden) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fileIndexes != nullptr) {
    fileIndexes->clear();
  }
  RefreshDir(filter, subDirOnly, includeHidden);
  if (fileIndexes != nullptr) {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (fileIndexes->full()) {
        break;
      }
      fileIndexes->push_back(static_cast<int>(i));
    }
  }
}

void NodeFileSystem::getFileName(int index, char *name, int length) {
  if (name == nullptr || length <= 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    name[0] = '\0';
    return;
  }
  strncpy(name, entries_[index].name.c_str(), length - 1);
  name[length - 1] = '\0';
}

PicoFileType NodeFileSystem::getFileType(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    return PFT_UNKNOWN;
  }
  return entries_[index].is_dir ? PFT_DIR : PFT_FILE;
}

bool NodeFileSystem::isParentRoot() { return cwd_ == kMountPoint; }
bool NodeFileSystem::isCurrentRoot() { return cwd_ == kMountPoint; }

bool NodeFileSystem::DeleteFile(const char *name) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string full = ResolvePath(cwd_, name);
  return unlink(full.c_str()) == 0;
}

bool NodeFileSystem::DeleteDir(const char *name) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string full = ResolvePath(cwd_, name);
  return rmdir(full.c_str()) == 0;
}

bool NodeFileSystem::exists(const char *path) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string full = ResolvePath(cwd_, path);
  struct stat st {};
  return stat(full.c_str(), &st) == 0;
}

bool NodeFileSystem::makeDir(const char *path, bool pFlag) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string full = ResolvePath(cwd_, path);
  if (!pFlag) {
    return mkdir(full.c_str(), 0755) == 0;
  }
  // Recursive create
  std::string accum;
  if (!full.empty() && full.front() == '/') {
    accum = "/";  // keep absolute paths rooted
  }
  std::stringstream ss(full);
  std::string segment;
  while (std::getline(ss, segment, '/')) {
    if (segment.empty()) {
      continue;
    }
    if (!accum.empty() && accum.back() != '/') {
      accum.push_back('/');
    }
    accum += segment;
    mkdir(accum.c_str(), 0755);
  }
  struct stat st {};
  return stat(full.c_str(), &st) == 0;
}

uint64_t NodeFileSystem::getFileSize(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
    return 0;
  }
  return entries_[index].size;
}

bool NodeFileSystem::CopyFile(const char *src, const char *dest) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string base = cwd_;
  if (!base.empty() && base.back() != '/') {
    base.push_back('/');
  }
  std::string s = base + src;
  std::string d = base + dest;
  FILE *in = fopen(s.c_str(), "rb");
  if (in == nullptr) {
    return false;
  }
  FILE *out = fopen(d.c_str(), "wb");
  if (out == nullptr) {
    fclose(in);
    return false;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return false;
    }
  }
  fclose(in);
  fclose(out);
  return true;
}

bool NodeFileSystem::MoveFile(const char *src, const char *dest) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string source = ResolvePath(cwd_, src);
  std::string target = ResolvePath(cwd_, dest);
  if (!EnsureParentDirs(target)) {
    return false;
  }
  return rename(source.c_str(), target.c_str()) == 0;
}

bool NodeFileSystem::isExFat() { return false; }

// -------- VfsFile -----------

VfsFile::VfsFile(FILE *f) : f_(f) {}
VfsFile::~VfsFile() { Close(); }

int VfsFile::Read(void *ptr, int size) { return static_cast<int>(fread(ptr, 1, size, f_)); }
int VfsFile::GetC() { return fgetc(f_); }
int VfsFile::Write(const void *ptr, int size, int nmemb) {
  return static_cast<int>(fwrite(ptr, size, nmemb, f_));
}
void VfsFile::Seek(long offset, int whence) { fseek(f_, offset, whence); }
long VfsFile::Tell() { return ftell(f_); }
bool VfsFile::Close() {
  if (f_) {
    fclose(f_);
    f_ = nullptr;
  }
  return true;
}
int VfsFile::Error() { return f_ ? ferror(f_) : -1; }
bool VfsFile::Sync() { return f_ ? (fflush(f_) == 0) : false; }
void VfsFile::Dispose() { delete this; }
