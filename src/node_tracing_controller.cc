#include "node_tracing_controller.h"

#include <string.h>
#include <memory>
#include <string>

namespace node {

NodeTraceWriter::NodeTraceWriter() {
  write_req_.writer = this;
}

NodeTraceWriter::~NodeTraceWriter() {
  // Note that this is only done when the Node event loop stops.
  // If our final log file has traces, then end the file appropriately.
  // This means that if no trace events are recorded, then no trace file is
  // produced.
  if (total_traces_ > 0) {
    WriteToFile("]}\n");
  }
}

void NodeTraceWriter::MakeStreamBlocking() {
  // The following might not be safe.
  // It is advised to set blocking mode of a pipe immediately after creation.
  // So we might need to close the current pipe and start a new one here.
  uv_stream_set_blocking(reinterpret_cast<uv_stream_t*>(&trace_file_pipe_), 1);
}

void NodeTraceWriter::OpenNewFileForStreaming() {
  ++file_num_;
  uv_fs_t req;
  std::string log_file = "node_trace.log." + std::to_string(file_num_);
  int fd = uv_fs_open(uv_default_loop(), &req, log_file.c_str(),
      O_CREAT | O_WRONLY | O_TRUNC, 0644, NULL);
  uv_pipe_init(uv_default_loop(), &trace_file_pipe_, 0);
  uv_pipe_open(&trace_file_pipe_, fd);
  // Note that the following does not get flushed to file immediately.
  stream_ << "{\"traceEvents\":[";
}

void NodeTraceWriter::AppendTraceEvent(TraceObject* trace_event) {
  // If this is the first trace event, open a new file for streaming.
  if (total_traces_ == 0) {
    OpenNewFileForStreaming();
  } else {
    stream_ << ",\n";
  }
  ++total_traces_;
  if (trace_event->scope() == NULL) {
    stream_ << "{\"pid\":" << trace_event->pid()
            << ",\"tid\":" << trace_event->tid()
            << ",\"ts\":" << trace_event->ts()
            << ",\"tts\":" << trace_event->tts() << ",\"ph\":\""
            << trace_event->phase() << "\",\"cat\":\""
            << TracingController::GetCategoryGroupName(
                   trace_event->category_enabled_flag())
            << "\",\"name\":\"" << trace_event->name()
            << "\",\"args\":{},\"dur\":" << trace_event->duration()
            << ",\"tdur\":" << trace_event->cpu_duration() << "}";
  } else {
    stream_ << "{\"pid\":" << trace_event->pid()
            << ",\"tid\":" << trace_event->tid()
            << ",\"ts\":" << trace_event->ts()
            << ",\"tts\":" << trace_event->tts() << ",\"ph\":\""
            << trace_event->phase() << "\",\"cat\":\""
            << TracingController::GetCategoryGroupName(
                   trace_event->category_enabled_flag())
            << "\",\"name\":\"" << trace_event->name() << "\",\"scope\":\""
            << trace_event->scope()
            << "\",\"args\":{},\"dur\":" << trace_event->duration()
            << ",\"tdur\":" << trace_event->cpu_duration() << "}";
  }
}

void NodeTraceWriter::Flush() {
  // Set a writing_ flag here so that we only do a single Flush at any one
  // point in time. This is because we need the stream_ to be alive when we
  // are flushing.
  if (total_traces_ >= kTracesPerFile) {
    total_traces_ = 0;
    stream_ << "]}\n";
  }
  WriteToFile(stream_.str().c_str());
}

void NodeTraceWriter::WriteToFile(const char* str) {
  // Writes stream_ to file.
  is_writing_ = true;
  uv_buf_t uv_buf = uv_buf_init(const_cast<char*>(str), strlen(str));
  // We can reuse write_req_ here because we are assured that no other
  // uv_write using write_req_ is ongoing. In the case of synchronous writes
  // where the callback has not been fired, it is unclear whether it is OK
  // to reuse write_req_.
  uv_write(reinterpret_cast<uv_write_t*>(&write_req_),
           reinterpret_cast<uv_stream_t*>(&trace_file_pipe_),
           &uv_buf, 1, OnWrite);
}

void NodeTraceWriter::OnWrite(uv_write_t* req, int status) {
  NodeTraceWriter* writer = reinterpret_cast<WriteRequest*>(req)->writer;
  writer->stream_.str("");
  writer->is_writing_ = false;
}

const double TraceBufferStreamingBuffer::kFlushThreshold = 0.75;

TraceBufferStreamingBuffer::TraceBufferStreamingBuffer(size_t max_chunks,
    TraceWriter* trace_writer) : max_chunks_(max_chunks) {
  uv_mutex_init(&mutex_);
  trace_writer_.reset(trace_writer);
  chunks_.resize(max_chunks);
}

TraceBufferStreamingBuffer::~TraceBufferStreamingBuffer() {
  uv_mutex_destroy(&mutex_);
}

TraceObject* TraceBufferStreamingBuffer::AddTraceEvent(uint64_t* handle) {
  // If the buffer usage exceeds kFlushThreshold, attempt to perform a flush.
  // This number should be customizable.
  // Because there is no lock here, it is entirely possible that there are
  // useless flushes due to two threads accessing this at the same time.
  if (total_chunks_ >= max_chunks_ * kFlushThreshold) {
    Flush();
  }
  uv_mutex_lock(&mutex_);
  // Create new chunk if last chunk is full or there is no chunk.
  if (total_chunks_ == 0 || chunks_[total_chunks_ - 1]->IsFull()) {
    if (total_chunks_ == max_chunks_) {
      // There is no more space to store more trace events.
      *handle = MakeHandle(0, 0, 0);
      uv_mutex_unlock(&mutex_);
      return NULL;
    }
    auto& chunk = chunks_[total_chunks_++];
    if (chunk) {
      chunk->Reset(current_chunk_seq_++);
    } else {
      chunk.reset(new TraceBufferChunk(current_chunk_seq_++));
    }
  }
  auto& chunk = chunks_[total_chunks_ - 1];
  size_t event_index;
  TraceObject* trace_object = chunk->AddTraceEvent(&event_index);
  *handle = MakeHandle(total_chunks_ - 1, chunk->seq(), event_index);
  uv_mutex_unlock(&mutex_);
  return trace_object;
}

TraceObject* TraceBufferStreamingBuffer::GetEventByHandle(uint64_t handle) {
  size_t chunk_index, event_index;
  uint32_t chunk_seq;
  ExtractHandle(handle, &chunk_index, &chunk_seq, &event_index);
  uv_mutex_lock(&mutex_);
  if (chunk_index >= total_chunks_) {
    uv_mutex_unlock(&mutex_);
    return NULL;
  }
  auto& chunk = chunks_[chunk_index];
  if (chunk->seq() != chunk_seq) {
    uv_mutex_unlock(&mutex_);
    return NULL;
  }
  uv_mutex_unlock(&mutex_);
  return chunk->GetEventAt(event_index);
}

bool TraceBufferStreamingBuffer::Flush() {
  uv_mutex_lock(&mutex_);
  if (!static_cast<NodeTraceWriter*>(trace_writer_.get())->IsReady()) {
    uv_mutex_unlock(&mutex_);
    return false;
  }
  for (size_t i = 0; i < total_chunks_; ++i) {
    auto& chunk = chunks_[i];
    for (size_t j = 0; j < chunk->size(); ++j) {
      trace_writer_->AppendTraceEvent(chunk->GetEventAt(j));
    }
  }
  trace_writer_->Flush();
  total_chunks_ = 0;
  uv_mutex_unlock(&mutex_);
  return true;
}

uint64_t TraceBufferStreamingBuffer::MakeHandle(
    size_t chunk_index, uint32_t chunk_seq, size_t event_index) const {
  return static_cast<uint64_t>(chunk_seq) * Capacity() +
         chunk_index * TraceBufferChunk::kChunkSize + event_index;
}

void TraceBufferStreamingBuffer::ExtractHandle(
    uint64_t handle, size_t* chunk_index,
    uint32_t* chunk_seq, size_t* event_index) const {
  *chunk_seq = static_cast<uint32_t>(handle / Capacity());
  size_t indices = handle % Capacity();
  *chunk_index = indices / TraceBufferChunk::kChunkSize;
  *event_index = indices % TraceBufferChunk::kChunkSize;
}

}  // namespace node
