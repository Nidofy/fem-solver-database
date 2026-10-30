#pragma once
#include <array>
#include <exception>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sdb::durability {
// StreamFlush exposes complete bytes to this OS; FileSync asks the OS to flush
// file contents; PublicationSync additionally synchronizes the parent entry.
// These describe successful OS calls, not storage-device power-loss guarantees.
enum class Level { StreamFlush, FileSync, PublicationSync };
#ifdef _WIN32
inline constexpr bool supports_parent_sync = false;
#else
inline constexpr bool supports_parent_sync = true;
#endif
inline void require_supported(Level level) {
    if (level != Level::StreamFlush && level != Level::FileSync && level != Level::PublicationSync)
        throw std::invalid_argument("unknown durability level");
    if (level == Level::PublicationSync && !supports_parent_sync)
        throw std::runtime_error("standalone parent-directory synchronization is unsupported on this platform");
}

// Explicit, per-thread test instrumentation. No environment variables activate
// faults. Worker threads must install their own hook; callbacks must not outlive
// their context. A callback may throw or terminate its dedicated test process.
enum class FaultPoint {
    AfterTempWrite, BeforeFileSync, AfterFileSync, BeforeReplace, AfterReplace,
    BeforeParentSync, AfterParentSync, ResultWrite, IndexCommit, LinkPublish,
    BeforeLinkPublish, AfterLinkPublish
};
struct Hook {
    void (*callback)(FaultPoint, const std::filesystem::path &, void *) = nullptr;
    void *context = nullptr;
};
inline thread_local Hook current_hook{};
class ScopedFaultHook {
    Hook previous_;
  public:
    explicit ScopedFaultHook(Hook hook) noexcept : previous_(current_hook) { current_hook = hook; }
    ScopedFaultHook(const ScopedFaultHook &) = delete;
    ScopedFaultHook &operator=(const ScopedFaultHook &) = delete;
    ~ScopedFaultHook() { current_hook = previous_; }
};
inline void fault(FaultPoint point, const std::filesystem::path &path) {
    if (current_hook.callback)
        current_hook.callback(point, path, current_hook.context);
}

inline void sync_file(const std::filesystem::path &path) {
    fault(FaultPoint::BeforeFileSync, path);
#ifdef _WIN32
    // Existing append streams may still be open. Share read/write/delete here;
    // this does not bypass an existing handle's more restrictive sharing mode.
    auto handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("file synchronization open failed");
    const bool synced = FlushFileBuffers(handle) != 0;
    const bool closed = CloseHandle(handle) != 0;
    if (!synced || !closed)
        throw std::runtime_error("file synchronization failed");
#else
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0)
        throw std::runtime_error("file synchronization open failed");
    const int status = ::fsync(fd);
    const int closed = ::close(fd);
    if (status || closed)
        throw std::runtime_error("file synchronization failed");
#endif
    fault(FaultPoint::AfterFileSync, path);
}

inline void sync_parent(const std::filesystem::path &path) {
    require_supported(Level::PublicationSync);
    fault(FaultPoint::BeforeParentSync, path);
#ifndef _WIN32
    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        throw std::runtime_error("parent-directory synchronization open failed");
    const int status = ::fsync(fd);
    const int closed = ::close(fd);
    if (status || closed)
        throw std::runtime_error("parent-directory synchronization failed");
#endif
    fault(FaultPoint::AfterParentSync, path);
}

// A replacement can become visible before a later synchronization fails.
// published() means replacement was confirmed, NOT that durability succeeded.
// A failed replacement system call is conservatively classified as uncertain.
class PublicationError : public std::runtime_error {
    bool published_ = false, uncertain_ = false;
    std::exception_ptr cause_;
    std::array<char, 1024> diagnostic_{};
  public:
    PublicationError()
        : std::runtime_error("atomic publication failed; inspect publication outcome and cause") {}
    // Own a bounded diagnostic; rethrow_exception may copy/allocate, so what()
    // must not return a pointer obtained from a temporary rethrown exception.
    const char *what() const noexcept override {
        return diagnostic_[0] ? diagnostic_.data() : std::runtime_error::what();
    }
    bool published() const noexcept { return published_; }
    bool replacement_may_have_happened() const noexcept { return uncertain_; }
    const std::exception_ptr &cause() const noexcept { return cause_; }
    void capture(bool published, bool uncertain) noexcept {
        published_ = published;
        uncertain_ = uncertain;
        cause_ = std::current_exception();
        if (cause_) {
            // capture is called from an active handler. Bare throw refers to
            // that same exception without copying it or allocating storage.
            try { throw; }
            catch (const std::exception &error) {
                const char *message = error.what();
                std::size_t i = 0;
                for (; i + 1 < diagnostic_.size() && message[i]; ++i) diagnostic_[i] = message[i];
                diagnostic_[i] = '\0';
            }
            catch (...) {}
        }
    }
};
} // namespace sdb::durability
