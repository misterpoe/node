#include "env-inl.h"
#include "tracing/agent.h"
#include "tracing/trace_config_parser.h"
#include "libplatform/libplatform.h"

namespace node {
namespace tracing {

Agent::Agent(Environment* env) : parent_env_(env) {}

void Agent::Start(v8::Platform* platform, const char* trace_config_file) {
  auto env = parent_env_;
  platform_ = platform;

  int err = uv_loop_init(&child_loop_);
  CHECK_EQ(err, 0);

  flush_signal_.data = this;
  err = uv_async_init(&child_loop_, &flush_signal_, FlushSignalCb);
  CHECK_EQ(err, 0);

  tracing_controller_ = new NodeTracingController();
  trace_writer_ = new NodeTraceWriter(&child_loop_);
  for (int i = 0; i < 2; ++i)
    trace_buffers_[i] = new NodeTraceBuffer(
        NodeTraceBuffer::kBufferChunks, trace_writer_, this);

  TraceConfig* trace_config = new TraceConfig();
  if (trace_config_file) {
    std::ifstream fin(trace_config_file);
    std::string str((std::istreambuf_iterator<char>(fin)),
                    std::istreambuf_iterator<char>());
    TraceConfigParser::FillTraceConfig(env->isolate(), trace_config,
                                       str.c_str());
  } else {
    trace_config->AddIncludedCategory("v8");
    trace_config->AddIncludedCategory("node");
  }
  tracing_controller_->Initialize(trace_buffers_[current_buf_]);
  tracing_controller_->StartTracing(trace_config);
  v8::platform::SetTracingController(platform, tracing_controller_);

  err = uv_thread_create(&thread_, ThreadCb, this);
  CHECK_EQ(err, 0);
}

void Agent::Stop() {
  if (!IsStarted()) {
    return;
  }
  // Perform final Flush on TraceBuffer. We don't want the tracing controller
  // to flush the buffer again on destruction of the V8::Platform.
  tracing_controller_->StopTracing();
  delete tracing_controller_;
  v8::platform::SetTracingController(platform_, nullptr);
  trace_writer_->WriteSuffix();
  delete trace_writer_;
  // // Remove extra TraceBuffer.
  delete trace_buffers_[1 - current_buf_];
  // NOTE: We might need to cleanup loop properly upon exit.
}

// static
void Agent::ThreadCb(void* agent) {
  static_cast<Agent*>(agent)->WorkerRun();
}

void Agent::WorkerRun() {
  uv_run(&child_loop_, UV_RUN_DEFAULT);
}

// static
void Agent::FlushSignalCb(uv_async_t* signal) {
  static_cast<Agent*>(signal->data)->AttemptFlush();
}

void Agent::AttemptFlush() {
  // If writer is ready now, we can perform a TraceBuffer->Flush() and swap
  // TraceBuffers immediately. Otherwise ignore this signal.
  if (!trace_writer_->IsReady()) {
    return;
  }
  current_buf_ = 1 - current_buf_;
  // Swap the trace buffers.
  tracing_controller_->SwapTraceBuffer(trace_buffers_[current_buf_]);
  trace_buffers_[1 - current_buf_]->Flush();
}

void Agent::SendFlushSignal() {
  int err = uv_async_send(&flush_signal_);
  CHECK_EQ(err, 0);
}

}  // namespace tracing
}  // namespace node
