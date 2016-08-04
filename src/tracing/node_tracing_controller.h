#ifndef SRC_NODE_TRACING_CONTROLLER_H_
#define SRC_NODE_TRACING_CONTROLLER_H_

#include "libplatform/v8-tracing.h"

namespace node {
namespace tracing {

using v8::platform::tracing::TraceBuffer;
using v8::platform::tracing::TracingController;

class NodeTracingController : public TracingController {
 public:
  TraceBuffer* SwapTraceBuffer(TraceBuffer* trace_buffer);
};

}  // namespace tracing
}  // namespace node

#endif  // SRC_NODE_TRACING_CONTROLLER_H_
