// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_OBJECT_BACKED_NATIVE_HANDLER_H_
#define EXTENSIONS_RENDERER_OBJECT_BACKED_NATIVE_HANDLER_H_

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "extensions/renderer/native_handler.h"
#include "v8/include/v8-util.h"
#include "v8/include/v8.h"

namespace extensions {
class ScriptContext;

// An ObjectBackedNativeHandler is a factory for JS objects with functions on
// them that map to native C++ functions. Subclasses should call RouteFunction()
// in their constructor to define functions on the created JS objects.
class ObjectBackedNativeHandler : public NativeHandler {
 public:
  explicit ObjectBackedNativeHandler(ScriptContext* context);
  ~ObjectBackedNativeHandler() override;

  // Create an object with bindings to the native functions defined through
  // RouteFunction().
  v8::Local<v8::Object> NewInstance() override;

  v8::Isolate* GetIsolate() const;

 protected:
  typedef base::Callback<void(const v8::FunctionCallbackInfo<v8::Value>&)>
      HandlerFunction;

  // Installs a new 'route' from |name| to |handler_function|. This means that
  // NewInstance()s of this ObjectBackedNativeHandler will have a property
  // |name| which will be handled by |handler_function|.
  //
  // Routed functions are destroyed along with the destruction of this class,
  // and are never called back into, therefore it's safe for |handler_function|
  // to bind to base::Unretained.
  void RouteFunction(const std::string& name,
                     const HandlerFunction& handler_function);
  void RouteFunction(const std::string& name,
                     const std::string& feature_name,
                     const HandlerFunction& handler_function);

  ScriptContext* context() const { return context_; }

  void Invalidate() override;

  // The following methods are convenience wrappers for methods on v8::Object
  // with the corresponding names.
  void SetPrivate(v8::Local<v8::Object> obj,
                  const char* key,
                  v8::Local<v8::Value> value);
  static void SetPrivate(v8::Local<v8::Context> context,
                         v8::Local<v8::Object> obj,
                         const char* key,
                         v8::Local<v8::Value> value);
  bool GetPrivate(v8::Local<v8::Object> obj,
                  const char* key,
                  v8::Local<v8::Value>* result);
  static bool GetPrivate(v8::Local<v8::Context> context,
                         v8::Local<v8::Object> obj,
                         const char* key,
                         v8::Local<v8::Value>* result);
  void DeletePrivate(v8::Local<v8::Object> obj, const char* key);
  static void DeletePrivate(v8::Local<v8::Context> context,
                            v8::Local<v8::Object> obj,
                            const char* key);

 private:
  // Callback for RouteFunction which routes the V8 call to the correct
  // base::Bound callback.
  static void Router(const v8::FunctionCallbackInfo<v8::Value>& args);

  // When RouteFunction is called we create a v8::Object to hold the data we
  // need when handling it in Router() - this is the base::Bound function to
  // route to.
  //
  // We need a v8::Object because it's possible for v8 to outlive the
  // base::Bound function; the lifetime of an ObjectBackedNativeHandler is the
  // lifetime of webkit's involvement with it, not the life of the v8 context.
  // A scenario when v8 will outlive us is if a frame holds onto the
  // contentWindow of an iframe after it's removed.
  //
  // So, we use v8::Objects here to hold that data, effectively refcounting
  // the data. When |this| is destroyed we remove the base::Bound function from
  // the object to indicate that it shoudn't be called.
  typedef v8::PersistentValueVector<v8::Object> RouterData;
  RouterData router_data_;

  ScriptContext* context_;

  v8::Global<v8::ObjectTemplate> object_template_;

  DISALLOW_COPY_AND_ASSIGN(ObjectBackedNativeHandler);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_OBJECT_BACKED_NATIVE_HANDLER_H_
