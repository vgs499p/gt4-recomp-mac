// SPDX-License-Identifier: MIT
// gs_stream_record: a GS stream consumer for tests (TASK-035). It runs no renderer.
//
//   gs_stream_record --ipc <session name> --parent <producer pid> [--output <file.gscap>] [--frames first:last]
//                    [--config <ini>]   (accepted and ignored, like the GS process settings file)
//
// The game starts it through GT4_GS_PROCESS. It opens the session, validates the control block (magic, protocol
// major version, struct sizes, region layout, a non-empty serial), posts READY and then consumes every command record.
// With an output file (--output, or GT4_GS_RECORD_OUTPUT when --output is absent) it writes the records to a .gscap
// version 2 file, byte for byte as they travel in the ring; without one it is a null consumer that discards them.
// READ_FIFO requests are answered with zeros (this tool has no VRAM model): READY carries
// kReadyFlagReadbacksZeroFilled, every reply carries kReadbackZeroFilled and STATS counts them.
// It posts STATS every 500 VSYNCs and at shutdown, and exits on CONTROL shutdown (code 0), on the death of the
// producer (code 4) or on a malformed stream (code 3).
#include "gs_stream_file.hpp"
#include "gs_stream_ipc.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using namespace gs_stream;
using namespace gs_stream::ipc;

constexpr std::uint64_t kStatsEvery = 500;
constexpr const char *kVersion = "gs_stream_record 1.0 (TASK-035)";

std::uint64_t now_us() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void copy_text(char *destination, std::size_t size, const std::string &text) {
  const std::size_t count = std::min(size - 1, text.size());
  std::memcpy(destination, text.data(), count);
  destination[count] = '\0';
}

struct Recorder {
  std::unique_ptr<Session> session;
  FileWriter output;
  bool recording = false;
  std::uint64_t first_frame = 0, last_frame = ~std::uint64_t(0);
  std::uint64_t frame = 0;  // VSYNCs seen so far = frame number of the records being received
  StatsPayload stats{};
  std::uint64_t bad_records = 0;

  void post_error(ErrorCode code, bool fatal, const std::string &text) {
    ErrorPayload error{};
    error.code = static_cast<std::uint32_t>(code);
    error.fatal = fatal ? 1u : 0u;
    copy_text(error.text, sizeof error.text, text);
    std::fprintf(stderr, "[gs-record] error %u: %s\n", error.code, text.c_str());
    session->post_event(RecordType::error, &error, sizeof error);
  }

  void post_stats() {
    stats.wait_us = session->wait_totals().data_wait_us;
    session->post_event(RecordType::stats, &stats, sizeof stats);
  }

  bool valid_payload(const RecordView &view) {
    const RecordHeader &h = *view.header;
    switch (static_cast<RecordType>(h.type)) {
      case RecordType::gif: {
        if (h.payload_bytes < sizeof(GifPayload)) return false;
        GifPayload gif;
        std::memcpy(&gif, view.payload, sizeof gif);
        return gif.path >= 1 && gif.path <= 3 && gif.bytes % 16 == 0 && h.payload_bytes == sizeof gif + gif.bytes;
      }
      case RecordType::priv: {
        if (h.payload_bytes != sizeof(PrivPayload)) return false;
        PrivPayload priv;
        std::memcpy(&priv, view.payload, sizeof priv);
        return priv.address >= kPrivilegedBase && priv.address < kPrivilegedBase + kPrivilegedBytes && priv.address % 8 == 0;
      }
      case RecordType::vsync: return h.payload_bytes == sizeof(VsyncPayload);
      case RecordType::read_fifo: return h.payload_bytes == sizeof(ReadFifoPayload);
      case RecordType::reset: return h.payload_bytes == sizeof(ResetPayload);
      case RecordType::control: return h.payload_bytes == sizeof(ControlPayload);
      default: return true;
    }
  }

  // Returns false when the session ends (shutdown).
  bool handle(const RecordView &view) {
    const RecordHeader &h = *view.header;
    const auto type = static_cast<RecordType>(h.type);
    ++stats.records;
    stats.bytes += view.total_bytes;
    if (recording && frame >= first_frame && frame <= last_frame) output.write_raw(view.header, view.total_bytes);
    if (!valid_payload(view)) {
      ++bad_records;
      post_error(ErrorCode::bad_record, false, "malformed payload in record " + std::to_string(h.sequence));
      session->release_command(view);
      return true;
    }
    switch (type) {
      case RecordType::vsync: {
        session->release_command(view);
        ++stats.vsyncs;
        ++frame;
        session->complete_vsync();
        if (recording && frame > last_frame) {
          output.close();
          recording = false;
          std::fprintf(stderr, "[gs-record] recording closed after frame %llu\n",
                       static_cast<unsigned long long>(last_frame));
        }
        if (stats.vsyncs % kStatsEvery == 0) post_stats();
        return true;
      }
      case RecordType::read_fifo: {
        ReadFifoPayload request;
        std::memcpy(&request, view.payload, sizeof request);
        session->release_command(view);
        const std::uint64_t bytes = std::min<std::uint64_t>(std::uint64_t(request.qwords) * 16, session->response_capacity());
        std::memset(session->response_area(), 0, static_cast<std::size_t>(bytes));
        ++stats.readbacks;
        ++stats.readbacks_zero_filled;
        session->complete_readback(request.request_id, bytes, kReadbackZeroFilled);
        return true;
      }
      case RecordType::control: {
        ControlPayload control;
        std::memcpy(&control, view.payload, sizeof control);
        session->release_command(view);
        if (control.kind == static_cast<std::uint32_t>(ControlKind::shutdown)) return false;
        return true;
      }
      case RecordType::gif:
      case RecordType::priv:
      case RecordType::reset:
        session->release_command(view);
        return true;
      case RecordType::state_save:
      case RecordType::state_load:
      case RecordType::aux_frame_begin:
      case RecordType::aux_frame_end:
        session->release_command(view);
        post_error(ErrorCode::unsupported_record, false, "reserved record type " + std::to_string(h.type) + " ignored");
        return true;
      default:
        session->release_command(view);
        post_error(ErrorCode::bad_record, true, "unknown record type " + std::to_string(h.type));
        throw IpcError("unknown record type");
    }
  }
};

int usage() {
  std::fputs("usage: gs_stream_record --ipc <session name> --parent <producer pid> [--output <file>] [--frames first:last]\n",
             stderr);
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  std::string name, output_path, frames;
  unsigned long parent_pid = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--ipc" && i + 1 < argc) name = argv[++i];
    else if (arg == "--parent" && i + 1 < argc) parent_pid = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--output" && i + 1 < argc) output_path = argv[++i];
    else if (arg == "--frames" && i + 1 < argc) frames = argv[++i];
    else if (arg == "--config" && i + 1 < argc) ++i;  // a GS renderer's settings file (TASK-036); not used by the recorder
    else return usage();
  }
  if (name.empty() || !parent_pid) return usage();
  if (output_path.empty())
    if (const char *value = std::getenv("GT4_GS_RECORD_OUTPUT")) output_path = value;
  if (frames.empty())
    if (const char *value = std::getenv("GT4_GS_RECORD_FRAMES")) frames = value;

  Recorder recorder;
  try {
    recorder.session = Session::open_consumer(name);
  } catch (const IpcError &error) {
    std::fprintf(stderr, "[gs-record] cannot open session %s: %s\n", name.c_str(), error.what());
    return 3;
  }
  Session &session = *recorder.session;
  ControlBlock &control = session.control();
  if (control.producer_pid != parent_pid) {
    recorder.post_error(ErrorCode::layout_mismatch, true, "producer pid differs from --parent");
    return 3;
  }
  const std::string serial(control.serial, strnlen(control.serial, sizeof control.serial));
  if (serial.empty()) {
    recorder.post_error(ErrorCode::layout_mismatch, true, "control block has no game serial");
    return 3;
  }
  NativeHandle parent = open_process_for_wait(parent_pid);
  if (!parent) {
    recorder.post_error(ErrorCode::internal, true, "cannot open the producer process");
    return 3;
  }
  session.set_peer(parent);
  control.consumer_pid = current_process_id();
  std::atomic_ref<std::uint32_t>(control.state).fetch_or(kStateConsumerReady);

  if (!frames.empty()) {
    unsigned long long first = 0, last = 0;
    char extra = 0;
    if (std::sscanf(frames.c_str(), "%llu:%llu%c", &first, &last, &extra) != 2 || last < first) {
      recorder.post_error(ErrorCode::internal, true, "--frames is not first:last");
      return 2;
    }
    recorder.first_frame = first;
    recorder.last_frame = last;
  }
  if (!output_path.empty()) {
    std::string error;
    if (!recorder.output.open(output_path, serial, recorder.first_frame, error)) {
      recorder.post_error(ErrorCode::internal, true, error);
      return 3;
    }
    recorder.recording = true;
  }

  ReadyPayload ready{};
  ready.version_major = kVersionMajor;
  ready.version_minor = kVersionMinor;
  ready.consumer_pid = current_process_id();
  ready.flags = kReadyFlagReadbacksZeroFilled | (recorder.recording ? kReadyFlagRecords : 0u);
  copy_text(ready.consumer, sizeof ready.consumer, "gs_stream_record");
  copy_text(ready.renderer, sizeof ready.renderer, recorder.recording ? "none (recorder)" : "none (null consumer)");
  copy_text(ready.version, sizeof ready.version, kVersion);
  session.post_event(RecordType::ready, &ready, sizeof ready);
  std::fprintf(stderr, "[gs-record] session %s serial %s: %s%s\n", name.c_str(), serial.c_str(),
               recorder.recording ? "recording to " : "discarding (null consumer)", output_path.c_str());

  int exit_code = 0;
  try {
    RecordView view;
    for (;;) {
      session.next_command(view);
      const std::uint64_t start = now_us();
      const bool more = recorder.handle(view);
      recorder.stats.busy_us += now_us() - start;
      std::atomic_ref<std::uint64_t>(control.consumer_heartbeat).fetch_add(1);
      if (!more) break;
    }
  } catch (const PeerExited &error) {
    std::fprintf(stderr, "[gs-record] %s\n", error.what());
    exit_code = 4;
  } catch (const IpcError &error) {
    std::fprintf(stderr, "[gs-record] stream error: %s\n", error.what());
    exit_code = 3;
  }
  recorder.output.close();
  std::fprintf(stderr,
               "[gs-record] end: vsyncs %llu records %llu bytes %llu readbacks %llu (zero-filled) bad %llu; file %llu "
               "records\n",
               static_cast<unsigned long long>(recorder.stats.vsyncs), static_cast<unsigned long long>(recorder.stats.records),
               static_cast<unsigned long long>(recorder.stats.bytes), static_cast<unsigned long long>(recorder.stats.readbacks),
               static_cast<unsigned long long>(recorder.bad_records),
               static_cast<unsigned long long>(recorder.output.records()));
  std::fflush(stderr);
  if (exit_code == 0) {
    try {
      recorder.post_stats();
      const ExitingPayload exiting{0, 0, 0};
      std::atomic_ref<std::uint32_t>(control.state).fetch_or(kStateExiting);
      session.post_event(RecordType::exiting, &exiting, sizeof exiting);
    } catch (const IpcError &) {
    }
  }
  close_handle(parent);
  return exit_code;
}
