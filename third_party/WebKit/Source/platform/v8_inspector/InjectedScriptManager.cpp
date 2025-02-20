/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Matt Lilek <webkit@mattlilek.com>
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/v8_inspector/InjectedScriptManager.h"

#include "platform/v8_inspector/InjectedScript.h"
#include "platform/v8_inspector/InjectedScriptHost.h"
#include "platform/v8_inspector/InjectedScriptNative.h"
#include "platform/v8_inspector/InjectedScriptSource.h"
#include "platform/v8_inspector/RemoteObjectId.h"
#include "platform/v8_inspector/V8DebuggerImpl.h"
#include "platform/v8_inspector/V8InjectedScriptHost.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include "platform/v8_inspector/public/V8DebuggerClient.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

PassOwnPtr<InjectedScriptManager> InjectedScriptManager::create(V8DebuggerImpl* debugger)
{
    return adoptPtr(new InjectedScriptManager(debugger));
}

InjectedScriptManager::InjectedScriptManager(V8DebuggerImpl* debugger)
    : m_injectedScriptHost(InjectedScriptHost::create(debugger))
    , m_customObjectFormatterEnabled(false)
    , m_debugger(debugger)
{
}

InjectedScriptManager::~InjectedScriptManager()
{
    m_injectedScriptHost->disconnect();
}

InjectedScriptHost* InjectedScriptManager::injectedScriptHost()
{
    return m_injectedScriptHost.get();
}

InjectedScript* InjectedScriptManager::findInjectedScript(ErrorString* errorString, int id) const
{
    IdToInjectedScriptMap::const_iterator it = m_idToInjectedScript.find(id);
    if (it != m_idToInjectedScript.end())
        return it->second;
    *errorString = "Inspected frame has gone";
    return nullptr;
}

InjectedScript* InjectedScriptManager::findInjectedScript(ErrorString* errorString, RemoteObjectIdBase* objectId) const
{
    return objectId ? findInjectedScript(errorString, objectId->contextId()) : nullptr;
}

void InjectedScriptManager::discardInjectedScripts()
{
    m_idToInjectedScript.clear();
}

int InjectedScriptManager::discardInjectedScriptFor(v8::Local<v8::Context> context)
{
    return discardInjectedScript(V8Debugger::contextId(context));
}

int InjectedScriptManager::discardInjectedScript(int contextId)
{
    if (!m_idToInjectedScript.contains(contextId))
        return 0;
    m_idToInjectedScript.remove(contextId);
    return contextId;
}

void InjectedScriptManager::releaseObjectGroup(const String16& objectGroup)
{
    protocol::Vector<int> keys;
    for (auto& it : m_idToInjectedScript)
        keys.append(it.first);
    for (auto& key : keys) {
        if (m_idToInjectedScript.contains(key)) // m_idToInjectedScript may change here.
            m_idToInjectedScript.get(key)->releaseObjectGroup(objectGroup);
    }
}

void InjectedScriptManager::setCustomObjectFormatterEnabled(bool enabled)
{
    m_customObjectFormatterEnabled = enabled;
    IdToInjectedScriptMap::iterator end = m_idToInjectedScript.end();
    for (IdToInjectedScriptMap::iterator it = m_idToInjectedScript.begin(); it != end; ++it) {
        it->second->setCustomObjectFormatterEnabled(enabled);
    }
}

InjectedScript* InjectedScriptManager::injectedScriptFor(v8::Local<v8::Context> context)
{
    v8::Context::Scope scope(context);
    int contextId = V8Debugger::contextId(context);

    IdToInjectedScriptMap::iterator it = m_idToInjectedScript.find(contextId);
    if (it != m_idToInjectedScript.end())
        return it->second;

    v8::Local<v8::Context> callingContext = context->GetIsolate()->GetCallingContext();
    if (!callingContext.IsEmpty() && !m_debugger->client()->callingContextCanAccessContext(callingContext, context))
        return nullptr;

    OwnPtr<InjectedScriptNative> injectedScriptNative = adoptPtr(new InjectedScriptNative(context->GetIsolate()));
    String16 injectedScriptSource(reinterpret_cast<const char*>(InjectedScriptSource_js), sizeof(InjectedScriptSource_js));

    v8::Local<v8::Object> object = createInjectedScript(injectedScriptSource, context, contextId, injectedScriptNative.get());
    OwnPtr<InjectedScript> result = adoptPtr(new InjectedScript(this, context, object, injectedScriptNative.release(), contextId));
    InjectedScript* resultPtr = result.get();
    if (m_customObjectFormatterEnabled)
        result->setCustomObjectFormatterEnabled(m_customObjectFormatterEnabled);
    m_idToInjectedScript.set(contextId, result.release());

    return resultPtr;
}

v8::Local<v8::Object> InjectedScriptManager::createInjectedScript(const String16& source, v8::Local<v8::Context> context, int id, InjectedScriptNative* injectedScriptNative)
{
    v8::Isolate* isolate = context->GetIsolate();
    v8::Context::Scope scope(context);

    v8::Local<v8::FunctionTemplate> wrapperTemplate = m_injectedScriptHost->wrapperTemplate(isolate);
    if (wrapperTemplate.IsEmpty()) {
        wrapperTemplate = V8InjectedScriptHost::createWrapperTemplate(isolate);
        m_injectedScriptHost->setWrapperTemplate(wrapperTemplate, isolate);
    }

    v8::Local<v8::Object> scriptHostWrapper = V8InjectedScriptHost::wrap(wrapperTemplate, context, m_injectedScriptHost.get());
    if (scriptHostWrapper.IsEmpty())
        return v8::Local<v8::Object>();

    injectedScriptNative->setOnInjectedScriptHost(scriptHostWrapper);

    // Inject javascript into the context. The compiled script is supposed to evaluate into
    // a single anonymous function(it's anonymous to avoid cluttering the global object with
    // inspector's stuff) the function is called a few lines below with InjectedScriptHost wrapper,
    // injected script id and explicit reference to the inspected global object. The function is expected
    // to create and configure InjectedScript instance that is going to be used by the inspector.
    v8::Local<v8::Value> value;
    if (!m_debugger->compileAndRunInternalScript(context, toV8String(isolate, source)).ToLocal(&value))
        return v8::Local<v8::Object>();
    ASSERT(value->IsFunction());
    v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(value);
    v8::Local<v8::Object> windowGlobal = context->Global();
    v8::Local<v8::Value> info[] = { scriptHostWrapper, windowGlobal, v8::Number::New(context->GetIsolate(), id) };
    v8::MicrotasksScope microtasksScope(isolate, v8::MicrotasksScope::kDoNotRunMicrotasks);
    v8::Local<v8::Value> injectedScriptValue;
    if (!function->Call(context, windowGlobal, WTF_ARRAY_LENGTH(info), info).ToLocal(&injectedScriptValue))
        return v8::Local<v8::Object>();
    if (!injectedScriptValue->IsObject())
        return v8::Local<v8::Object>();
    return injectedScriptValue.As<v8::Object>();
}

} // namespace blink
