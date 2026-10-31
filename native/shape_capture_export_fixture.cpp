#include "shape_capture_export.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "posix_compat.hpp"
#endif

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <system_error>

using namespace gt4::observation;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

Inputs known_input() {
  Inputs input;
  for (std::size_t argument = 0; argument < 4; ++argument)
    for (std::size_t lane = 0; lane < 4; ++lane)
      input.argument_bits[argument][lane] = 0xFFFFFF00u + static_cast<std::uint32_t>(argument * 4 + lane);
  input.argument_bits[3][3] = 0xFFFFFFFF;
  input.fpr12_bits = 0x7FC12345;
  input.guest_sp = 0x01FFF000;
  input.metadata = {all_fields_valid, 0xFFFFFFFF, 0x80000000, 0x70002340, 0xFFFFFFFF, 0xFFFF, 0xFF};
  return input;
}

std::string contents(const std::filesystem::path &path) {
  std::ifstream source(path, std::ios::binary);
  require(source.is_open(), "read fixture export");
  return {std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
  const auto folder = std::filesystem::temp_directory_path() /
      ("GT4CaptureControl-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
  try {
    require(std::filesystem::create_directory(folder), "exclusive fixture directory");
    ShapeObserver observer;
    std::cout << serialize_capture(observer, CaptureStopReason::shutdown);
    observer.start();
    bool active_rejected = false;
    try { export_capture(folder / "active.json", observer, CaptureStopReason::manual); }
    catch (const std::logic_error &) { active_rejected = true; }
    require(active_rejected && !std::filesystem::exists(folder / "active.json"), "active export creates no file");
    const auto input = known_input();
    observer.begin(0xFFFFFFFF, false, input);
    auto after = input.metadata;
    after.valid_fields = cursor_valid;
    after.cursor = 0x700025A0;
    observer.complete(0xFFFFFFFF, false, shape_return_pc, input.guest_sp, after);
    observer.begin(0xFFFFFFFF, false, input);
    observer.stop();
    const auto one = serialize_capture(observer, CaptureStopReason::manual);
    const auto destination = folder / "capture.json";
    export_capture(destination, observer, CaptureStopReason::manual);
    require(contents(destination) == one, "file bytes equal deterministic metadata");
    require(serialize_capture(observer, CaptureStopReason::manual) == one, "export preserves observer state");
    bool overwrite_rejected = false;
    try { export_capture(destination, observer, CaptureStopReason::shutdown); }
    catch (const std::system_error &) { overwrite_rejected = true; }
    require(overwrite_rejected && contents(destination) == one, "existing file remains unchanged");
    bool missing_parent_rejected = false;
    try { export_capture(folder / "missing/capture.json", observer, CaptureStopReason::manual); }
    catch (const std::system_error &) { missing_parent_rejected = true; }
    require(missing_parent_rejected && !std::filesystem::exists(folder / "missing"), "export never creates parent directories");
    bool invalid_reason_rejected = false;
    try { export_capture(folder / "invalid.json", observer, static_cast<CaptureStopReason>(99)); }
    catch (const std::invalid_argument &) { invalid_reason_rejected = true; }
    require(invalid_reason_rejected && !std::filesystem::exists(folder / "invalid.json"), "invalid reason creates no file");
    std::cout << one;
    observer.start();
    for (std::size_t index = 0; index <= ShapeObserver::record_limit; ++index) {
      observer.begin(0xFFFFFFFF, false, input);
      observer.complete(0xFFFFFFFF, false, shape_return_pc, input.guest_sp, after);
    }
    require(!observer.enabled() && observer.records().size() == 256 && observer.counters().record_dropped == 1,
            "257th completion preserves the fixed record capacity");
    const auto full = serialize_capture(observer, CaptureStopReason::observer_stopped);
    require(full.size() <= capture_json_limit, "maximum encoded capture is bounded");
    export_capture(folder / "full.json", observer, CaptureStopReason::observer_stopped);
    require(contents(folder / "full.json") == full, "full export bytes agree");
    std::cout << full;
    std::filesystem::remove(destination);
    std::filesystem::remove(folder / "full.json");
    std::filesystem::remove(folder);
    std::fputs("PASS: bounded capture JSON and exclusive file controls\n", stderr);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
