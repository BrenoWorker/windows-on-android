#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace wd {

enum class ArchiveState { Running, Done, Failed, NeedPassword, WrongPassword, Cancelled };

struct ArchiveInfo {
    std::string format;
    int files = 0;
    long long size = 0;
    bool encrypted = false;
    bool encryptedNames = false;
    std::string error;
};

class ArchiveJob {
public:
    std::atomic<ArchiveState> state{ArchiveState::Running};
    std::atomic<long long> done{0};
    std::atomic<long long> total{0};
    std::atomic<int> files{0};
    std::atomic<bool> cancel{false};

    std::string error() {
        std::lock_guard<std::mutex> l(m_);
        return error_;
    }
    std::string current() {
        std::lock_guard<std::mutex> l(m_);
        return current_;
    }
    void setError(const std::string& e) {
        std::lock_guard<std::mutex> l(m_);
        error_ = e;
    }
    void setCurrent(const std::string& c) {
        std::lock_guard<std::mutex> l(m_);
        current_ = c;
    }

private:
    std::mutex m_;
    std::string error_;
    std::string current_;
};

bool isArchive(const std::string& path);
bool rarSupported();
bool archiveInfo(const std::string& path, const std::string& password, ArchiveInfo& out);
std::shared_ptr<ArchiveJob> extractArchive(const std::string& path, const std::string& dest, const std::string& password);
bool extractArchiveSync(const std::string& path, const std::string& dest, const std::string& password, ArchiveJob& job);

}
