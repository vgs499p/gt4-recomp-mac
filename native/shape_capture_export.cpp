#include "shape_capture_export.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace gt4::observation {
namespace {
void hex(std::ostream &output, std::uint64_t value, int width) {
  output << '"' << std::hex << std::setw(width) << std::setfill('0') << value << '"' << std::dec;
}

void metadata(std::ostream &output, const Metadata &sample) {
  output << "{\"valid_fields\":";
  hex(output, sample.valid_fields, 8);
  output << ",\"enabled\":";
  hex(output, sample.enabled, 8);
  output << ",\"cached_bits\":";
  hex(output, sample.cached_bits, 8);
  output << ",\"cursor\":";
  hex(output, sample.cursor, 8);
  output << ",\"shape_bits\":";
  hex(output, sample.shape_bits, 8);
  output << ",\"descriptor_count\":";
  hex(output, sample.descriptor_count, 4);
  output << ",\"flag\":";
  hex(output, sample.flag, 2);
  output << '}';
}

const char *stop_reason(CaptureStopReason reason) {
  switch (reason) {
  case CaptureStopReason::manual: return "manual";
  case CaptureStopReason::observer_stopped: return "observer_stopped";
  case CaptureStopReason::shutdown: return "shutdown";
  }
  throw std::invalid_argument("Unknown capture stop reason");
}
} // namespace

std::string serialize_capture(const ShapeObserver &observer, CaptureStopReason reason) {
  if (observer.enabled())
    throw std::logic_error("Stop observation before exporting records");
  const auto reason_text = stop_reason(reason);
  const auto records = observer.records();
  if (records.size() > ShapeObserver::record_limit)
    throw std::length_error("Capture record count exceeds the observer bound");
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"schema_version\":1,\"scope\":\"shape_call_metadata_only\",\"call_pc\":";
  hex(output, shape_call_pc, 8);
  output << ",\"target_pc\":";
  hex(output, shape_target_pc, 8);
  output << ",\"return_pc\":";
  hex(output, shape_return_pc, 8);
  output << ",\"stop_reason\":\"" << reason_text << "\",\"record_count\":" << records.size();
  const auto &counters = observer.counters();
  const std::array<std::pair<const char *, std::uint64_t>, 10> values{{
      {"started", counters.started}, {"completed", counters.completed}, {"aborted", counters.aborted},
      {"handler_skipped", counters.handler_skipped}, {"orphan", counters.orphan},
      {"rejected_return", counters.rejected_return}, {"ambiguous_stack", counters.ambiguous_stack},
      {"capacity_rejected", counters.capacity_rejected}, {"record_dropped", counters.record_dropped},
      {"incomplete_samples", counters.incomplete_samples}}};
  output << ",\"counters\":{";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index) output << ',';
    output << '"' << values[index].first << "\":";
    hex(output, values[index].second, 16);
  }
  output << "},\"records\":[";
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (index) output << ',';
    const auto &record = records[index];
    output << "{\"epoch\":";
    hex(output, record.epoch, 16);
    output << ",\"incarnation\":";
    hex(output, record.incarnation, 16);
    output << ",\"sequence\":";
    hex(output, record.sequence, 16);
    output << ",\"owner_id\":";
    hex(output, record.owner_id, 8);
    output << ",\"before\":{\"argument_bits\":[";
    for (std::size_t argument = 0; argument < record.before.argument_bits.size(); ++argument) {
      if (argument) output << ',';
      output << '[';
      for (std::size_t lane = 0; lane < record.before.argument_bits[argument].size(); ++lane) {
        if (lane) output << ',';
        hex(output, record.before.argument_bits[argument][lane], 8);
      }
      output << ']';
    }
    output << "],\"fpr12_bits\":";
    hex(output, record.before.fpr12_bits, 8);
    output << ",\"guest_sp\":";
    hex(output, record.before.guest_sp, 8);
    output << ",\"metadata\":";
    metadata(output, record.before.metadata);
    output << "},\"after\":";
    metadata(output, record.after);
    output << '}';
  }
  output << "]}\n";
  auto result = output.str();
  if (result.size() > capture_json_limit)
    throw std::length_error("Capture JSON exceeds the fixed output bound");
  return result;
}

void export_capture(const std::filesystem::path &destination,
                    const ShapeObserver &observer, CaptureStopReason reason) {
  const auto json = serialize_capture(observer, reason);
#ifndef _WIN32
  // POSIX: O_EXCL matches CREATE_NEW; on failure the newly created file is removed by path.
  const int file = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (file < 0)
    throw std::system_error(errno, std::system_category(), "Create capture");
  int error = 0;
  std::size_t written = 0;
  while (written < json.size()) {
    const ssize_t count = ::write(file, json.data() + written, json.size() - written);
    if (count < 0) {
      if (errno == EINTR) continue;
      error = errno;
      break;
    }
    if (count == 0) { error = EIO; break; }
    written += static_cast<std::size_t>(count);
  }
  if (!error && ::fsync(file) != 0) error = errno;
  if (error) {
    ::close(file);
    const bool removed = ::unlink(destination.c_str()) == 0;
    throw std::system_error(error, std::system_category(),
                            removed ? "Write capture" : "Write capture; partial file cleanup failed");
  }
  if (::close(file) != 0)
    throw std::system_error(errno, std::system_category(), "Close capture");
#else
  const auto file = CreateFileW(destination.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Create capture");
  DWORD error = ERROR_SUCCESS;
  std::size_t written = 0;
  while (written < json.size()) {
    DWORD count = 0;
    if (!WriteFile(file, json.data() + written, static_cast<DWORD>(json.size() - written), &count, nullptr)) {
      error = GetLastError();
      break;
    }
    if (count == 0) { error = ERROR_WRITE_FAULT; break; }
    written += count;
  }
  if (!error && !FlushFileBuffers(file)) error = GetLastError();
  if (error) {
    // Remove only the newly created file by its still-exclusive handle.
    FILE_DISPOSITION_INFO disposition{TRUE};
    const auto removed = SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    CloseHandle(file);
    throw std::system_error(static_cast<int>(error), std::system_category(),
                            removed ? "Write capture" : "Write capture; partial file cleanup failed");
  }
  if (!CloseHandle(file))
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Close capture");
#endif
}

} // namespace gt4::observation
