// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_V8_TRACING_H_
#define V8_LIBPLATFORM_V8_TRACING_H_

#include <fstream>
#include <memory>

#include "include/v8.h"

namespace v8 {
namespace platform {
namespace tracing {

class TraceObject {
 public:
  void Initialize(char phase, const uint8_t* category_enabled_flag,
                  const char* name, const char* scope, uint64_t id,
                  uint64_t bind_id, int num_args, const char** arg_names,
                  const uint8_t* arg_types, const uint64_t* arg_values,
                  int flags);
  void UpdateDuration();
  void InitializeForTesting(char phase, const uint8_t* category_enabled_flag,
                            const char* name, const char* scope, uint64_t id,
                            uint64_t bind_id, int num_args,
                            const char** arg_names, const uint8_t* arg_types,
                            const uint64_t* arg_values, int flags, int pid,
                            int tid, int64_t ts, int64_t tts, uint64_t duration,
                            uint64_t cpu_duration);

  int pid() const { return pid_; }
  int tid() const { return tid_; }
  char phase() const { return phase_; }
  const uint8_t* category_enabled_flag() const {
    return category_enabled_flag_;
  }
  const char* name() const { return name_; }
  const char* scope() const { return scope_; }
  int64_t ts() { return ts_; }
  int64_t tts() { return tts_; }
  uint64_t duration() { return duration_; }
  uint64_t cpu_duration() { return cpu_duration_; }

 private:
  int pid_;
  int tid_;
  char phase_;
  const char* name_;
  const char* scope_;
  const uint8_t* category_enabled_flag_;
  uint64_t id_;
  uint64_t bind_id_;
  int num_args_;
  unsigned int flags_;
  int64_t ts_;
  int64_t tts_;
  uint64_t duration_;
  uint64_t cpu_duration_;
  // TODO(fmeawad): Add args support.
};

class TraceWriter {
 public:
  virtual ~TraceWriter() {}
  virtual void AppendTraceEvent(TraceObject* trace_event) = 0;
  virtual void Flush() = 0;

  static TraceWriter* CreateJSONTraceWriter(std::ostream& stream);
};

class TraceBufferChunk {
 public:
  explicit TraceBufferChunk(uint32_t seq);

  void Reset(uint32_t new_seq);
  bool IsFull() const { return next_free_ == kChunkSize; }
  TraceObject* AddTraceEvent(size_t* event_index);
  TraceObject* GetEventAt(size_t index) { return &chunk_[index]; }

  uint32_t seq() const { return seq_; }
  size_t size() const { return next_free_; }

  static const size_t kChunkSize = 64;

 private:
  size_t next_free_ = 0;
  TraceObject chunk_[kChunkSize];
  uint32_t seq_;
};

class TraceBuffer {
 public:
  virtual ~TraceBuffer() {}

  virtual TraceObject* AddTraceEvent(uint64_t* handle) = 0;
  virtual TraceObject* GetEventByHandle(uint64_t handle) = 0;
  virtual bool Flush() = 0;

  static const size_t kRingBufferChunks = 1024;

  static TraceBuffer* CreateTraceBufferRingBuffer(size_t max_chunks,
                                                  TraceWriter* trace_writer);
};

// Options determines how the trace buffer stores data.
enum TraceRecordMode {
  // Record until the trace buffer is full.
  RECORD_UNTIL_FULL,

  // Record until the user ends the trace. The trace buffer is a fixed size
  // and we use it as a ring buffer during recording.
  RECORD_CONTINUOUSLY,

  // Record until the trace buffer is full, but with a huge buffer size.
  RECORD_AS_MUCH_AS_POSSIBLE,

  // Echo to console. Events are discarded.
  ECHO_TO_CONSOLE,
};

class TraceConfig {
 public:
  typedef std::vector<std::string> StringList;

  static TraceConfig* CreateDefaultTraceConfig();
  // Create a trace config object using a given JSON format string.
  static TraceConfig* CreateTraceConfigFromJSON(v8::Isolate* isolate,
                                                const char* json_str);

  TraceRecordMode GetTraceRecordMode() const { return record_mode_; }
  bool IsSamplingEnabled() const { return enable_sampling_; }
  bool IsSystraceEnabled() const { return enable_systrace_; }
  bool IsArgumentFilterEnabled() const { return enable_argument_filter_; }

  void SetTraceRecordMode(TraceRecordMode mode) { record_mode_ = mode; }
  void EnableSampling() { enable_sampling_ = true; }
  void EnableSystrace() { enable_systrace_ = true; }
  void EnableArgumentFilter() { enable_argument_filter_ = true; }

  bool IsCategoryGroupEnabled(const char* category_group) const;

 private:
  friend class TraceConfigParser;

  TraceConfig()
      : enable_sampling_(false),
        enable_systrace_(false),
        enable_argument_filter_(false) {}

  TraceRecordMode record_mode_;
  bool enable_sampling_ : 1;
  bool enable_systrace_ : 1;
  bool enable_argument_filter_ : 1;
  StringList included_categories_;
  StringList excluded_categories_;
};

class TracingController {
 public:
  enum Mode { DISABLED = 0, RECORDING_MODE };

  // The pointer returned from GetCategoryGroupEnabledInternal() points to a
  // value with zero or more of the following bits. Used in this class only.
  // The TRACE_EVENT macros should only use the value as a bool.
  // These values must be in sync with macro values in TraceEvent.h in Blink.
  enum CategoryGroupEnabledFlags {
    // Category group enabled for the recording mode.
    ENABLED_FOR_RECORDING = 1 << 0,
    // Category group enabled by SetEventCallbackEnabled().
    ENABLED_FOR_EVENT_CALLBACK = 1 << 2,
    // Category group enabled to export events to ETW.
    ENABLED_FOR_ETW_EXPORT = 1 << 3
  };

  void Initialize(TraceBuffer* trace_buffer);
  const uint8_t* GetCategoryGroupEnabled(const char* category_group);
  static const char* GetCategoryGroupName(const uint8_t* category_enabled_flag);
  uint64_t AddTraceEvent(char phase, const uint8_t* category_enabled_flag,
                         const char* name, const char* scope, uint64_t id,
                         uint64_t bind_id, int32_t num_args,
                         const char** arg_names, const uint8_t* arg_types,
                         const uint64_t* arg_values, unsigned int flags);
  void UpdateTraceEventDuration(const uint8_t* category_enabled_flag,
                                const char* name, uint64_t handle);

  void StartTracing(TraceConfig* trace_config);
  void StopTracing();

 private:
  const uint8_t* GetCategoryGroupEnabledInternal(const char* category_group);
  void UpdateCategoryGroupEnabledFlag(size_t category_index);
  void UpdateCategoryGroupEnabledFlags();

  std::unique_ptr<TraceBuffer> trace_buffer_;
  std::unique_ptr<TraceConfig> trace_config_;
  Mode mode_ = DISABLED;
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_V8_TRACING_H_
