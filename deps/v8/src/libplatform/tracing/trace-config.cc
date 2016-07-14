// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "include/libplatform/v8-tracing.h"

namespace v8 {

class Isolate;

namespace platform {
namespace tracing {

// String options that can be used to initialize TraceOptions.
const char kRecordUntilFull[] = "record-until-full";
const char kRecordContinuously[] = "record-continuously";
const char kRecordAsMuchAsPossible[] = "record-as-much-as-possible";

const char kRecordModeParam[] = "record_mode";
const char kEnableSamplingParam[] = "enable_sampling";
const char kEnableSystraceParam[] = "enable_systrace";
const char kEnableArgumentFilterParam[] = "enable_argument_filter";
const char kIncludedCategoriesParam[] = "included_categories";
const char kExcludedCategoriesParam[] = "excluded_categories";

class TraceConfigParser {
 public:
  static void FillTraceConfig(v8::Isolate* isolate, TraceConfig* trace_config,
                              const char* json_str) {
    HandleScope outer_scope(isolate);
    Local<Context> context = Context::New(isolate);
    Context::Scope context_scope(context);
    HandleScope inner_scope(isolate);

    Local<String> source =
        String::NewFromUtf8(isolate, json_str, NewStringType::kNormal)
            .ToLocalChecked();
    Local<Value> result = JSON::Parse(context, source).ToLocalChecked();
    Local<v8::Object> trace_config_object = Local<v8::Object>::Cast(result);

    trace_config->record_mode_ =
        GetTraceRecordMode(isolate, context, trace_config_object);
    trace_config->enable_sampling_ =
        GetBoolean(isolate, context, trace_config_object, kEnableSamplingParam);
    trace_config->enable_systrace_ =
        GetBoolean(isolate, context, trace_config_object, kEnableSystraceParam);
    trace_config->enable_argument_filter_ = GetBoolean(
        isolate, context, trace_config_object, kEnableArgumentFilterParam);
    UpdateCategoriesList(isolate, context, trace_config_object,
                         kIncludedCategoriesParam,
                         trace_config->included_categories_);
    UpdateCategoriesList(isolate, context, trace_config_object,
                         kExcludedCategoriesParam,
                         trace_config->excluded_categories_);
  }

 private:
  static bool GetBoolean(v8::Isolate* isolate, Local<Context> context,
                         Local<v8::Object> object, const char* property) {
    Local<Value> value = GetValue(isolate, context, object, property);
    if (value->IsNumber()) {
      Local<Boolean> v8_boolean = value->ToBoolean(context).ToLocalChecked();
      return v8_boolean->Value();
    }
    return false;
  }

  static int UpdateCategoriesList(v8::Isolate* isolate, Local<Context> context,
                                  Local<v8::Object> object,
                                  const char* property,
                                  TraceConfig::StringList& categories_list) {
    Local<Value> value = GetValue(isolate, context, object, property);
    if (value->IsArray()) {
      Local<Array> v8_array = Local<Array>::Cast(value);
      for (int i = 0, length = v8_array->Length(); i < length; ++i) {
        Local<Value> v = v8_array->Get(context, i)
                             .ToLocalChecked()
                             ->ToString(context)
                             .ToLocalChecked();
        String::Utf8Value str(v->ToString(context).ToLocalChecked());
        categories_list.push_back(std::string(*str));
      }
      return v8_array->Length();
    }
    return 0;
  }

  static TraceRecordMode GetTraceRecordMode(v8::Isolate* isolate,
                                            Local<Context> context,
                                            Local<v8::Object> object) {
    Local<Value> value = GetValue(isolate, context, object, kRecordModeParam);
    if (value->IsString()) {
      Local<String> v8_string = value->ToString(context).ToLocalChecked();
      String::Utf8Value str(v8_string);
      if (strcmp(kRecordUntilFull, *str) == 0) {
        return TraceRecordMode::RECORD_UNTIL_FULL;
      } else if (strcmp(kRecordContinuously, *str) == 0) {
        return TraceRecordMode::RECORD_CONTINUOUSLY;
      } else if (strcmp(kRecordAsMuchAsPossible, *str) == 0) {
        return TraceRecordMode::RECORD_AS_MUCH_AS_POSSIBLE;
      }
    }
    return TraceRecordMode::RECORD_UNTIL_FULL;
  }

  static Local<Value> GetValue(v8::Isolate* isolate, Local<Context> context,
                               Local<v8::Object> object, const char* property) {
    Local<String> v8_str =
        String::NewFromUtf8(isolate, property, NewStringType::kNormal)
            .ToLocalChecked();
    return object->Get(context, v8_str).ToLocalChecked();
  }
};

TraceConfig* TraceConfig::CreateTraceConfigFromJSON(v8::Isolate* isolate,
                                                    const char* json_str) {
  TraceConfig* trace_config = new TraceConfig();
  TraceConfigParser::FillTraceConfig(isolate, trace_config, json_str);
  return trace_config;
}

TraceConfig* TraceConfig::CreateDefaultTraceConfig() {
  TraceConfig* trace_config = new TraceConfig();
  trace_config->included_categories_.push_back("v8");
  return trace_config;
}

bool TraceConfig::IsCategoryGroupEnabled(const char* category_group) const {
  for (auto included_category : included_categories_) {
    if (strcmp(included_category.data(), category_group) == 0) return true;
  }
  return false;
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
