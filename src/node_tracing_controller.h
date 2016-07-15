#ifndef SRC_NODE_TRACING_CONTROLLER_H_
#define SRC_NODE_TRACING_CONTROLLER_H_

#include <sstream>

#include "uv.h"
#include "include/libplatform/v8-tracing.h"

namespace node {

using v8::platform::tracing::TraceBuffer;
using v8::platform::tracing::TraceBufferChunk;
using v8::platform::tracing::TraceConfig;
using v8::platform::tracing::TracingController;
using v8::platform::tracing::TraceObject;
using v8::platform::tracing::TraceWriter;

class NodeTracingController : public TracingController {
};

class TraceBufferStreamingBuffer : public TraceBuffer {
 public:
  TraceBufferStreamingBuffer(size_t max_chunks, TraceWriter* trace_writer);
  ~TraceBufferStreamingBuffer();

  TraceObject* AddTraceEvent(uint64_t* handle) override;
  TraceObject* GetEventByHandle(uint64_t handle) override;
  bool Flush() override;

  static const double kFlushThreshold;
  static const size_t kBufferChunks = 1024;

 private:
  uint64_t MakeHandle(size_t chunk_index, uint32_t chunk_seq,
                      size_t event_index) const;
  void ExtractHandle(uint64_t handle, size_t* chunk_index,
                     uint32_t* chunk_seq, size_t* event_index) const;
  size_t Capacity() const { return max_chunks_ * TraceBufferChunk::kChunkSize; }

  uv_mutex_t mutex_;
  size_t max_chunks_;
  std::unique_ptr<TraceWriter> trace_writer_;
  std::vector<std::unique_ptr<TraceBufferChunk>> chunks_;
  size_t total_chunks_ = 0;
  uint32_t current_chunk_seq_ = 1;
};

class NodeTraceWriter : public TraceWriter {
 public:
  NodeTraceWriter();
  ~NodeTraceWriter();

  bool IsReady() { return !is_writing_; }
  void AppendTraceEvent(TraceObject* trace_event) override;
  void Flush() override;
  void MakeStreamBlocking();

  static const int kTracesPerFile = 1 << 20;

 private:
  struct WriteRequest {
    uv_write_t req;
    NodeTraceWriter* writer;
  };

  static void OnWrite(uv_write_t* req, int status);
  void OpenNewFileForStreaming();
  void WriteToFile(const char* str);

  int total_traces_ = 0;
  int file_num_ = 0;
  WriteRequest write_req_;
  uv_pipe_t trace_file_pipe_;
  bool is_writing_ = false;
  std::ostringstream stream_;
};

}  // namespace node

#endif  // SRC_NODE_TRACING_CONTROLLER_H_
