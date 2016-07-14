// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/v8-tracing.h"
#include "src/tracing/trace-event.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace platform {
namespace tracing {

const char* test_trace_config_str =
    "{\"record_mode\":\"record-until-full\",\"enable_sampling\":1,\"enable_"
    "systrace\":0,\"enable_argument_filter\":1,\"included_categories\":[\"v8."
    "cpu_profile\",\"v8.cpu_profile.hires\"],\"excluded_categories\":[\"v8."
    "runtime\"]}";

TEST(TestTraceConfigConstructor) {
  LocalContext env;
  TraceConfig* trace_config_from_JSON = TraceConfig::CreateTraceConfigFromJSON(
      env->GetIsolate(), test_trace_config_str);
  CHECK_EQ(trace_config_from_JSON->IsSamplingEnabled(), true);
  CHECK_EQ(trace_config_from_JSON->IsSystraceEnabled(), false);
  CHECK_EQ(trace_config_from_JSON->IsArgumentFilterEnabled(), true);
  CHECK_EQ(trace_config_from_JSON->IsCategoryGroupEnabled("v8"), false);
  CHECK_EQ(trace_config_from_JSON->IsCategoryGroupEnabled("v8.cpu_profile"),
           true);
  CHECK_EQ(
      trace_config_from_JSON->IsCategoryGroupEnabled("v8.cpu_profile.hires"),
      true);
  CHECK_EQ(trace_config_from_JSON->IsCategoryGroupEnabled("v8.runtime"), false);
  delete trace_config_from_JSON;

  TraceConfig* trace_config_default = TraceConfig::CreateDefaultTraceConfig();
  CHECK_EQ(trace_config_default->IsSamplingEnabled(), false);
  CHECK_EQ(trace_config_default->IsSystraceEnabled(), false);
  CHECK_EQ(trace_config_default->IsArgumentFilterEnabled(), false);
  CHECK_EQ(trace_config_default->IsCategoryGroupEnabled("v8"), true);
  CHECK_EQ(trace_config_default->IsCategoryGroupEnabled("v8.cpu_profile"),
           false);
  CHECK_EQ(trace_config_default->IsCategoryGroupEnabled("v8.cpu_profile.hires"),
           false);
  CHECK_EQ(trace_config_default->IsCategoryGroupEnabled("v8.runtime"), false);
  delete trace_config_default;
}

TEST(TestTraceObject) {
  TraceObject trace_object;
  trace_object.Initialize('X', "Test.Trace", "v8-cat", 42, 123, 0, 0);
  CHECK_EQ('X', trace_object.phase());
  CHECK_EQ("Test.Trace", trace_object.name());
  CHECK_EQ("v8-cat", trace_object.category_group());
  CHECK_EQ(0, trace_object.duration());
  CHECK_EQ(0, trace_object.cpu_duration());
}

class MockTraceWriter : public TraceWriter {
 public:
  void AppendTraceEvent(TraceObject* trace_event) override {
    events_.push_back(trace_event->name());
  }

  void Flush() override {}

  std::vector<std::string> events() { return events_; }

 private:
  std::vector<std::string> events_;
};

TEST(TestTraceBufferRingBuffer) {
  MockTraceWriter* writer = new MockTraceWriter();
  TraceBuffer* ring_buffer =
      TraceBuffer::CreateTraceBufferRingBuffer(2, writer);

  // We should be able to add kChunkSize * 2 + 1 trace events.
  std::vector<uint64_t> handles(TraceBufferChunk::kChunkSize * 2 + 1);
  for (size_t i = 0; i < handles.size(); ++i) {
    TraceObject* trace_object = ring_buffer->AddTraceEvent(&handles[i]);
    CHECK_NOT_NULL(trace_object);
    std::string name = "Test.Trace" + std::to_string(i);
    trace_object->Initialize('X', name, "v8-cat", 42, 123, 0, 0);
    trace_object = ring_buffer->GetEventByHandle(handles[i]);
    CHECK_NOT_NULL(trace_object);
    CHECK_EQ('X', trace_object->phase());
    CHECK_EQ(name, trace_object->name());
    CHECK_EQ("v8-cat", trace_object->category_group());
  }

  // We should only be able to retrieve the last kChunkSize + 1.
  for (size_t i = 0; i < TraceBufferChunk::kChunkSize; ++i) {
    CHECK_NULL(ring_buffer->GetEventByHandle(handles[i]));
  }

  for (size_t i = TraceBufferChunk::kChunkSize; i < handles.size(); ++i) {
    TraceObject* trace_object = ring_buffer->GetEventByHandle(handles[i]);
    CHECK_NOT_NULL(trace_object);
    // The object properties should be correct.
    CHECK_EQ('X', trace_object->phase());
    CHECK_EQ("Test.Trace" + std::to_string(i), trace_object->name());
    CHECK_EQ("v8-cat", trace_object->category_group());
  }

  // Check Flush(), that the writer wrote the last kChunkSize  1 event names.
  ring_buffer->Flush();
  auto events = writer->events();
  CHECK_EQ(TraceBufferChunk::kChunkSize + 1, events.size());
  for (size_t i = TraceBufferChunk::kChunkSize; i < handles.size(); ++i) {
    CHECK_EQ("Test.Trace" + std::to_string(i),
             events[i - TraceBufferChunk::kChunkSize]);
  }
  delete ring_buffer;
}

TEST(TestJSONTraceWriter) {
  std::ostringstream stream;
  TraceWriter* writer = TraceWriter::CreateJSONTraceWriter(stream);
  TraceObject trace_object;
  trace_object.InitializeForTesting('X', "Test0", "v8-cat", 42, 123, 0, 0, 11,
                                    22, 100, 50, 33, 44);
  writer->AppendTraceEvent(&trace_object);
  trace_object.InitializeForTesting('Y', "Test1", "v8-cat", 43, 456, 0, 0, 55,
                                    66, 110, 55, 77, 88);
  writer->AppendTraceEvent(&trace_object);
  writer->Flush();
  delete writer;

  std::string trace_str = stream.str();
  std::string expected_trace_str =
      "{\"traceEvents\":[{\"pid\":11,\"tid\":22,\"ts\":100,\"tts\":50,"
      "\"ph\":\"X\",\"cat\":\"v8-cat\",\"name\":\"Test0\",\"args\":{},"
      "\"dur\":33,\"tdur\":44},{\"pid\":55,\"tid\":66,\"ts\":110,\"tts\":55,"
      "\"ph\":\"Y\",\"cat\":\"v8-cat\",\"name\":\"Test1\",\"args\":{},\"dur\":"
      "77,\"tdur\":88}]}";
  CHECK_EQ(expected_trace_str, trace_str);
}

TEST(TestTracingController) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  v8::Platform* default_platform = v8::platform::CreateDefaultPlatform();
  i::V8::SetPlatformForTesting(default_platform);

  TracingController tracing_controller;
  platform::SetTracingController(default_platform, &tracing_controller);

  MockTraceWriter* writer = new MockTraceWriter();
  TraceBuffer* ring_buffer =
      TraceBuffer::CreateTraceBufferRingBuffer(1, writer);
  tracing_controller.Initialize(ring_buffer);
  tracing_controller.StartTracing(TraceConfig::CreateDefaultTraceConfig());
  TRACE_EVENT0("v8", "v8.Test");
  // cat category is not included in default config
  TRACE_EVENT0("cat", "v8.Test2");
  TRACE_EVENT0("v8", "v8.Test3");
  tracing_controller.StopTracing();

  CHECK_EQ(2, writer->events().size());
  CHECK_EQ("v8.Test", writer->events()[0]);
  CHECK_EQ("v8.Test3", writer->events()[1]);

  i::V8::SetPlatformForTesting(old_platform);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
