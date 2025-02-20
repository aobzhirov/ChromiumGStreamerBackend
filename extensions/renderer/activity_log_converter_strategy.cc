// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/activity_log_converter_strategy.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "v8/include/v8.h"

namespace extensions {

namespace {

// Summarize a V8 value. This performs a shallow conversion in all cases, and
// returns only a string with a description of the value (e.g.,
// "[HTMLElement]").
scoped_ptr<base::Value> SummarizeV8Value(v8::Isolate* isolate,
                                         v8::Local<v8::Object> object) {
  v8::TryCatch try_catch(isolate);
  v8::Isolate::DisallowJavascriptExecutionScope scope(
      isolate, v8::Isolate::DisallowJavascriptExecutionScope::THROW_ON_FAILURE);
  v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, "[");
  if (object->IsFunction()) {
    name =
        v8::String::Concat(name, v8::String::NewFromUtf8(isolate, "Function"));
    v8::Local<v8::Value> fname =
        v8::Local<v8::Function>::Cast(object)->GetName();
    if (fname->IsString() && v8::Local<v8::String>::Cast(fname)->Length()) {
      name = v8::String::Concat(name, v8::String::NewFromUtf8(isolate, " "));
      name = v8::String::Concat(name, v8::Local<v8::String>::Cast(fname));
      name = v8::String::Concat(name, v8::String::NewFromUtf8(isolate, "()"));
    }
  } else {
    name = v8::String::Concat(name, object->GetConstructorName());
  }
  name = v8::String::Concat(name, v8::String::NewFromUtf8(isolate, "]"));

  if (try_catch.HasCaught()) {
    return scoped_ptr<base::Value>(
        new base::StringValue("[JS Execution Exception]"));
  }

  return scoped_ptr<base::Value>(
      new base::StringValue(std::string(*v8::String::Utf8Value(name))));
}

}  // namespace

ActivityLogConverterStrategy::ActivityLogConverterStrategy() {}

ActivityLogConverterStrategy::~ActivityLogConverterStrategy() {}

bool ActivityLogConverterStrategy::FromV8Object(
    v8::Local<v8::Object> value,
    base::Value** out,
    v8::Isolate* isolate,
    const FromV8ValueCallback& callback) const {
  return FromV8Internal(value, out, isolate, callback);
}

bool ActivityLogConverterStrategy::FromV8Array(
    v8::Local<v8::Array> value,
    base::Value** out,
    v8::Isolate* isolate,
    const FromV8ValueCallback& callback) const {
  return FromV8Internal(value, out, isolate, callback);
}

bool ActivityLogConverterStrategy::FromV8Internal(
    v8::Local<v8::Object> value,
    base::Value** out,
    v8::Isolate* isolate,
    const FromV8ValueCallback& callback) const {
  scoped_ptr<base::Value> parsed_value;
  parsed_value = SummarizeV8Value(isolate, value);
  *out = parsed_value.release();

  return true;
}

}  // namespace extensions
