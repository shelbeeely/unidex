// Xteink X4: the SD card through the FreeInk SDK's SDCardManager (SdFat over the panel's SPI bus), behind Arduino's
// fs::FS interface, so storage.cpp and the apps keep using fs::File as on the Waveshare board.
#include "board.h"
#if UNIDEX_X4
#include "sdkcard.h"
#include <FSImpl.h>
#include <SDCardManager.h>

namespace {

class CardFile : public fs::FileImpl {
 public:
  CardFile(FsFile f, const String &path) : f_(std::move(f)), path_(path) {
    const int slash = path_.lastIndexOf('/');
    name_ = slash >= 0 ? path_.substring(slash + 1) : path_;
  }
  ~CardFile() override { close(); }

  size_t write(const uint8_t *buf, size_t size) override { return f_ ? f_.write(buf, size) : 0; }
  size_t read(uint8_t *buf, size_t size) override {
    if (!f_) return 0;
    const int n = f_.read(buf, size);
    return n > 0 ? n : 0;
  }
  void flush() override {
    if (f_) f_.sync();
  }
  bool seek(uint32_t pos, fs::SeekMode mode) override {
    if (!f_) return false;
    if (mode == fs::SeekCur) return f_.seekCur(pos);
    if (mode == fs::SeekEnd) return f_.seekSet(f_.fileSize() - pos);
    return f_.seekSet(pos);
  }
  size_t position() const override { return f_ ? const_cast<FsFile &>(f_).curPosition() : 0; }
  size_t size() const override { return f_ ? const_cast<FsFile &>(f_).fileSize() : 0; }
  bool setBufferSize(size_t) override { return false; }
  void close() override {
    if (f_) f_.close();
  }
  time_t getLastWrite() override { return 0; }
  const char *path() const override { return path_.c_str(); }
  const char *name() const override { return name_.c_str(); }
  boolean isDirectory() override { return f_ && f_.isDir(); }
  fs::FileImplPtr openNextFile(const char *) override {
    if (!isDirectory()) return fs::FileImplPtr();
    FsFile next;
    if (!next.openNext(&f_, O_RDONLY)) return fs::FileImplPtr();
    char n[128];
    next.getName(n, sizeof n);
    return std::make_shared<CardFile>(std::move(next), childPath(n));
  }
  boolean seekDir(long position) override { return f_ && f_.seekSet(position); }
  String getNextFileName() override { return getNextFileName(nullptr); }
  String getNextFileName(bool *isDir) override {
    fs::FileImplPtr next = openNextFile("r");
    if (!next) return "";
    if (isDir) *isDir = next->isDirectory();
    return next->path();
  }
  void rewindDirectory() override {
    if (f_) f_.rewindDirectory();
  }
  operator bool() override { return f_.isOpen(); }

 private:
  String childPath(const char *n) const { return (path_ == "/" ? String("/") : path_ + "/") + n; }
  FsFile f_;
  String path_, name_;
};

class CardFs : public fs::FSImpl {
 public:
  fs::FileImplPtr open(const char *path, const char *mode, const bool create) override {
    oflag_t flags = O_RDONLY;
    if (mode && mode[0] == 'w') flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (mode && mode[0] == 'a') flags = O_RDWR | O_CREAT | O_AT_END | O_APPEND;
    else if (mode && strchr(mode, '+')) flags = O_RDWR | (create ? O_CREAT : 0);
    FsFile f = SdMan.open(path, flags);
    if (!f) return fs::FileImplPtr();
    return std::make_shared<CardFile>(std::move(f), path);
  }
  bool exists(const char *path) override { return SdMan.exists(path); }
  bool rename(const char *from, const char *to) override { return SdMan.rename(from, to); }
  bool remove(const char *path) override { return SdMan.remove(path); }
  bool mkdir(const char *path) override { return SdMan.mkdir(path); }
  bool rmdir(const char *path) override { return SdMan.rmdir(path); }
};

fs::FS card(std::make_shared<CardFs>());

}  // namespace

fs::FS &sdkCard() { return card; }
bool sdkCardBegin() { return SdMan.begin(); }
void sdkCardEnd() { SdMan.shutdown(); }
uint64_t sdkCardTotal() { return SdMan.sdTotalBytes(); }
uint64_t sdkCardUsed() { return SdMan.sdUsedBytes(); }
#endif
