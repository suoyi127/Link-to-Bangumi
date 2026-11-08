#include "anime_vault/services/MpvProgressMonitor.hpp"
#include "anime_vault/services/MpvPlaybackSamples.hpp"
#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

namespace anime_vault {
using Json = nlohmann::json;

MpvPlaybackSamples::MpvPlaybackSamples(std::string expectedPath)
    : matchedPath(expectedPath.empty()), expectedPath_(std::move(expectedPath)) {
    std::replace(expectedPath_.begin(), expectedPath_.end(), '\\', '/');
}

void MpvPlaybackSamples::event(std::string_view line, double wallSeconds) {
    const auto msg = Json::parse(line, nullptr, false);
    if (!msg.is_object() || finished) return;
    const auto type = msg.find("event");
    if (type == msg.end()) return;
    if (*type == "end-file" || *type == "shutdown") {
        finished = flushRequested = true;
        return;
    }
    if (*type == "seek" || *type == "playback-restart") continuous_ = false;
    if (*type != "property-change") return;
    const auto name = msg.find("name"), data = msg.find("data");
    if (name == msg.end() || data == msg.end()) return;
    if (*name == "path" && data->is_string()) {
        auto path = data->get<std::string>();
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path != expectedPath_) finished = true;
        else matchedPath = true;
        return;
    }
    if (*name == "pause" && data->is_boolean()) {
        paused_ = data->get<bool>();
        continuous_ = false;
        flushRequested = true;
    }
    if (!data->is_number()) return;
    const double value = data->get<double>();
    if (!std::isfinite(value) || value < 0) return;
    if (*name == "duration" && value > 0) {
        duration = value;
        dirty = true;
    }
    if (*name == "speed" && value > 0) speed_ = value;
    if (*name != "time-pos") return;
    const double elapsed = wallSeconds - lastTime_;
    const double advanced = value - position;
    // 拖动、倒退及长时间断采只更新续播点，不补算跳过的区间。
    if (continuous_ && elapsed >= 0 && elapsed < 5 && advanced > 0 &&
        advanced <= elapsed * speed_ + 0.5) {
        if (!ranges.empty() && ranges.back().second == position)
            ranges.back().second = value;
        else ranges.emplace_back(position, value);
    }
    position = value;
    lastTime_ = wallSeconds;
    continuous_ = !paused_;
    dirty = true;
}

#ifdef _WIN32
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Pipe {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~Pipe() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

std::string newEndpoint() {
    GUID id{};
    wchar_t name[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, name, 40))
        return {};
    std::string endpoint = R"(\\.\pipe\anime-vault-)";
    for (auto ch = name; *ch; ++ch) endpoint.push_back(static_cast<char>(*ch)); // GUID text is ASCII.
    return endpoint;
}

void track(std::stop_token stop, WatchProgressStore& store, WatchSession session,
           std::int64_t mediaId, const std::string& endpoint, const std::string& mediaPath) {
    Pipe pipe;
    const auto deadline = Clock::now() + 20s;
    while (!stop.stop_requested() && Clock::now() < deadline) {
        pipe.handle = CreateFileA(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe.handle != INVALID_HANDLE_VALUE) break;
        std::this_thread::sleep_for(75ms);
    }
    if (pipe.handle == INVALID_HANDLE_VALUE) return;
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(pipe.handle, &mode, nullptr, nullptr)) return;

    std::string commands;
    int id = 0;
    for (const auto* name : {"path", "duration", "pause", "speed", "time-pos"}) {
        commands += Json{{"command", {"observe_property", ++id, name}}}.dump() + "\n";
    }
    DWORD written{};
    if (!WriteFile(pipe.handle, commands.data(), static_cast<DWORD>(commands.size()), &written, nullptr)
        || written != commands.size()) return;

    MpvPlaybackSamples samples(mediaPath);
    auto lastWrite = Clock::now();
    auto save = [&] {
        if (samples.matchedPath && samples.dirty && samples.position >= 0) {
            store.sample(mediaId, session.id, samples.position, samples.duration, samples.ranges);
            samples.ranges.clear();
            samples.dirty = false;
        }
        samples.flushRequested = false;
        lastWrite = Clock::now();
    };
    std::string pending;
    while (!stop.stop_requested() && !samples.finished) {
        DWORD available{};
        if (!PeekNamedPipe(pipe.handle, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            char buf[8192];
            DWORD read{};
            if (!ReadFile(pipe.handle, buf, std::min<DWORD>(sizeof(buf), available), &read, nullptr) || !read) break;
            pending.append(buf, read);
            for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                const auto line = pending.substr(0, end);
                pending.erase(0, end + 1);
                const double wall = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
                samples.event(line, wall);
                if (samples.finished) break;
            }
            if (pending.size() > 65536) break;
        } else {
            std::this_thread::sleep_for(75ms);
        }
        if (samples.flushRequested || Clock::now() - lastWrite >= 2s) save();
    }
    save();
}
} // namespace
#endif

MpvProgressMonitor::~MpvProgressMonitor() {
    for (auto& worker : workers_) worker.thread.request_stop();
}

bool MpvProgressMonitor::launch(ProcessLauncher& launcher, const std::filesystem::path& executable,
                               const std::string& mediaPath, std::int64_t mediaId, bool fromStart) {
#ifdef _WIN32
    std::lock_guard lock(mutex_);
    std::erase_if(workers_, [](const Worker& worker) { return worker.done->load(); });
    const auto endpoint = newEndpoint();
    const auto start = fromStart ? std::optional<double>{0} : store_.resumePosition(mediaId);
    std::vector<std::string> options;
    if (start) options = {"--no-resume-playback", "--start=" + std::to_string(*start)};
    else {
        // 首次采集沿用旧版 mpv 的 watch-later 断点，随后以应用记录为准。
        options = {"--resume-playback=yes", "--save-position-on-quit"};
    }
    if (!endpoint.empty()) options.push_back("--input-ipc-server=" + endpoint);
    options.insert(options.end(), {"--", mediaPath});
    if (!launcher.launch(executable, options)) return false;
    if (endpoint.empty()) return true;
    const auto session = store_.begin(mediaId, fromStart);
    auto done = std::make_shared<std::atomic_bool>(false);
    workers_.push_back({done, std::jthread(
        [this, done, session, mediaId, endpoint, mediaPath](std::stop_token stop) {
            try {
                track(stop, store_, session, mediaId, endpoint, mediaPath);
            } catch (const std::exception&) {
                // 媒体可能已被移除；采集结束不影响播放器进程。
            }
            done->store(true);
        })});
    return true;
#else
    (void)mediaId;
    (void)fromStart;
    return launcher.launch(executable, {"--save-position-on-quit", "--", mediaPath});
#endif
}

} // namespace anime_vault
