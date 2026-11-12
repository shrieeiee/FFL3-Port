#include "bugreport.h"
#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ffl3 {

void BugLog::frame(uint16_t keys) {
    if (keys_.empty() || keys_.back().second != keys) keys_.push_back({frame_, keys});
    frame_++;
    while (keys_.size() > 1 && frame_ - keys_[1].first > kMaxFrames) keys_.pop_front();
}

void BugLog::event(const std::string& line) {
    char b[32];
    std::snprintf(b, sizeof b, "%8llu  ", static_cast<unsigned long long>(frame_));
    lines_.push_back(b + line);
    while (lines_.size() > kMaxLines) lines_.pop_front();
}

bool BugLog::write(const std::string& dir, const std::string& info) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    FILE* f = std::fopen((fs::path(dir) / "inputs.txt").string().c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# frame  keys (A B S=start U D L R M=menu), held from that frame on\n");
    static const char kNames[] = "ABSUDLRM";
    for (const auto& [fr, k] : keys_) {
        char s[9];
        for (int i = 0; i < 8; i++) s[i] = (k >> i) & 1 ? kNames[i] : '.';
        s[8] = 0;
        std::fprintf(f, "%8llu  %s\n", static_cast<unsigned long long>(fr), s);
    }
    std::fprintf(f, "%8llu  (now)\n", static_cast<unsigned long long>(frame_));
    std::fclose(f);
    f = std::fopen((fs::path(dir) / "log.txt").string().c_str(), "w");
    if (!f) return false;
    for (const std::string& l : lines_) std::fprintf(f, "%s\n", l.c_str());
    std::fclose(f);
    f = std::fopen((fs::path(dir) / "info.txt").string().c_str(), "w");
    if (!f) return false;
    std::fputs(info.c_str(), f);
    std::fclose(f);
    return true;
}

std::string newReportDir(const std::string& base) {
    namespace fs = std::filesystem;
    const std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
    fs::path p = fs::path(base) / "bug_reports" / stamp;
    for (int n = 2; fs::exists(p); n++) p = fs::path(base) / "bug_reports" / (std::string(stamp) + "-" + std::to_string(n));
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

namespace {
std::function<void()> g_onCrash;
volatile std::sig_atomic_t g_crashed = 0;
void crashOnce() {
    if (g_crashed) return;
    g_crashed = 1;
    if (g_onCrash) g_onCrash();
}
void onSignal(int sig) {
    crashOnce();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#ifdef _WIN32
LONG WINAPI onSeh(EXCEPTION_POINTERS*) {
    crashOnce();
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
} // namespace

void installCrashHandler(std::function<void()> onCrash) {
    g_onCrash = std::move(onCrash);
    std::signal(SIGSEGV, onSignal);
    std::signal(SIGABRT, onSignal);
    std::signal(SIGFPE, onSignal);
    std::signal(SIGILL, onSignal);
    std::set_terminate([] { crashOnce(); std::abort(); });
#ifdef _WIN32
    SetUnhandledExceptionFilter(onSeh);
#endif
}

} // namespace ffl3
