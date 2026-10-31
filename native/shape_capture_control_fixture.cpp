#include "shape_capture_control.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "posix_compat.hpp"
#endif

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace gt4::observation;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

std::string contents(const std::filesystem::path &path) {
  std::ifstream source(path, std::ios::binary);
  require(source.is_open(), "read control export");
  return {std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
}

void complete_one(ShapeObserver &observer) {
  Inputs input;
  input.guest_sp = 0x01FFF000;
  observer.begin(1, false, input);
  observer.complete(1, false, shape_return_pc, input.guest_sp, {});
}

void manual(const std::filesystem::path &path) {
  ShapeObserver observer;
  CaptureControl control(observer, path);
  require(control.phase() == CapturePhase::idle && !observer.enabled(), "initial control is inactive");
  control.request_toggle(true);
  control.service();
  require(!observer.enabled(), "repeat ignored");
  control.request_toggle();
  control.request_toggle();
  control.service();
  require(!observer.enabled(), "two unserviced commands coalesce by parity");
  control.request_toggle();
  require(!observer.enabled() && !std::filesystem::exists(path), "message only latches, including audio-pump path");
  control.service();
  require(observer.enabled() && control.phase() == CapturePhase::capturing, "boundary starts capture");
  complete_one(observer);
  control.request_toggle(true);
  control.service();
  require(observer.enabled(), "repeat cannot stop");
  control.request_toggle();
  require(observer.enabled(), "stop also deferred");
  control.service();
  require(!observer.enabled() && control.phase() == CapturePhase::exported, "manual stop exports");
  const auto saved = contents(path);
  control.request_toggle();
  control.service();
  control.finish();
  require(!observer.enabled() && contents(path) == saved && observer.records().size() == 1,
          "one capture terminal state never clears or overwrites records");
  std::cout << saved;
}

void automatic(const std::filesystem::path &path, bool ambiguous) {
  ShapeObserver observer;
  CaptureControl control(observer, path);
  control.request_toggle();
  control.service();
  if (ambiguous) {
    Inputs input;
    observer.begin(1, false, input);
    observer.begin(1, false, input);
  } else {
    for (std::size_t index = 0; index <= ShapeObserver::record_limit; ++index) complete_one(observer);
  }
  require(!observer.enabled() && !std::filesystem::exists(path), "automatic stop waits for host boundary");
  control.service();
  require(control.phase() == CapturePhase::exported, "any observer stop exports with generic reason");
  std::cout << contents(path);
}

void shutdown(const std::filesystem::path &path, bool pending_only) {
  ShapeObserver observer;
  CaptureControl control(observer, path);
  control.request_toggle();
  if (!pending_only) {
    control.service();
    complete_one(observer);
  }
  control.finish();
  control.finish();
  if (pending_only) {
    require(control.phase() == CapturePhase::idle && !observer.enabled() && !std::filesystem::exists(path),
            "closing must not start a latched idle capture");
  } else {
    require(control.phase() == CapturePhase::exported && !observer.enabled(), "off-execution shutdown exports once");
    std::cout << contents(path);
  }
}

void retry(const std::filesystem::path &path, bool at_shutdown) {
  ShapeObserver observer;
  CaptureControl control(observer, path);
  control.request_toggle();
  control.service();
  complete_one(observer);
  { std::ofstream existing(path); existing << "sentinel"; }
  control.request_toggle();
  control.service();
  require(control.phase() == CapturePhase::ready && contents(path) == "sentinel", "failed export preserves existing file");
  std::filesystem::remove(path);
  control.service();
  require(!std::filesystem::exists(path), "ready does not retry every frame");
  if (at_shutdown) {
    control.finish();
  } else {
    control.request_toggle();
    require(!std::filesystem::exists(path), "retry is latched");
    control.service();
  }
  require(control.phase() == CapturePhase::exported && observer.records().size() == 1, "explicit retry preserves capture");
  std::cout << contents(path);
}
} // namespace

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("GT4CaptureLatch-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
  try {
    require(std::filesystem::create_directory(directory), "exclusive control directory");
    manual(directory / "manual.json");
    automatic(directory / "capacity.json", false);
    automatic(directory / "ambiguity.json", true);
    shutdown(directory / "shutdown.json", false);
    shutdown(directory / "idle.json", true);
    retry(directory / "retry.json", false);
    retry(directory / "shutdown-retry.json", true);
    ShapeObserver observer;
    for (const auto &path : {std::filesystem::path("relative.json"), directory / "missing/out.json",
                             directory / "manual.json", directory / "file:stream"}) {
      bool rejected = false;
      try { CaptureControl invalid(observer, path); }
      catch (const std::invalid_argument &) { rejected = true; }
      require(rejected && !observer.enabled(), "invalid destination rejected without starting capture");
    }
    observer.start();
    complete_one(observer);
    observer.stop();
    bool used_rejected = false;
    try { CaptureControl used(observer, directory / "unused.json"); }
    catch (const std::logic_error &) { used_rejected = true; }
    require(used_rejected && observer.records().size() == 1, "initialization cannot adopt and clear old records");
    for (const auto &name : {"manual.json", "capacity.json", "ambiguity.json", "shutdown.json", "retry.json", "shutdown-retry.json"})
      std::filesystem::remove(directory / name);
    std::filesystem::remove(directory);
    std::cout.flush();
    std::fputs("PASS: deferred manual capture, limits, shutdown and retry controls\n", stderr);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
