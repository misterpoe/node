#include "tracing/node_tracing_controller.h"

#include <memory>

namespace node {
namespace tracing {

TraceBuffer* NodeTracingController::SwapTraceBuffer(TraceBuffer* trace_buffer) {
  TraceBuffer* prev_trace_buffer = trace_buffer_.release();
  trace_buffer_.reset(trace_buffer);
  return prev_trace_buffer;
}

}  // namespace tracing
}  // namespace node
