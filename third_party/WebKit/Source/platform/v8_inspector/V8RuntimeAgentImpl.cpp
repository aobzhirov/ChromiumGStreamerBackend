/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/v8_inspector/V8RuntimeAgentImpl.h"

#include "platform/inspector_protocol/Values.h"
#include "platform/v8_inspector/IgnoreExceptionsScope.h"
#include "platform/v8_inspector/InjectedScript.h"
#include "platform/v8_inspector/InjectedScriptHost.h"
#include "platform/v8_inspector/InjectedScriptManager.h"
#include "platform/v8_inspector/RemoteObjectId.h"
#include "platform/v8_inspector/V8DebuggerImpl.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include "platform/v8_inspector/public/V8DebuggerClient.h"

namespace blink {

namespace V8RuntimeAgentImplState {
static const char customObjectFormatterEnabled[] = "customObjectFormatterEnabled";
};

using protocol::Runtime::ExceptionDetails;
using protocol::Runtime::RemoteObject;

static bool hasInternalError(ErrorString* errorString, bool hasError)
{
    if (hasError)
        *errorString = "Internal error";
    return hasError;
}

PassOwnPtr<V8RuntimeAgent> V8RuntimeAgent::create(V8Debugger* debugger, int contextGroupId)
{
    return adoptPtr(new V8RuntimeAgentImpl(static_cast<V8DebuggerImpl*>(debugger), contextGroupId));
}

V8RuntimeAgentImpl::V8RuntimeAgentImpl(V8DebuggerImpl* debugger, int contextGroupId)
    : m_contextGroupId(contextGroupId)
    , m_state(nullptr)
    , m_frontend(nullptr)
    , m_injectedScriptManager(InjectedScriptManager::create(debugger))
    , m_debugger(debugger)
    , m_enabled(false)
{
    m_debugger->addRuntimeAgent(m_contextGroupId, this);
}

V8RuntimeAgentImpl::~V8RuntimeAgentImpl()
{
    m_debugger->removeRuntimeAgent(m_contextGroupId);
}

void V8RuntimeAgentImpl::evaluate(
    ErrorString* errorString,
    const String16& expression,
    const Maybe<String16>& objectGroup,
    const Maybe<bool>& includeCommandLineAPI,
    const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
    const Maybe<int>& executionContextId,
    const Maybe<bool>& returnByValue,
    const Maybe<bool>& generatePreview,
    const Maybe<bool>& userGesture,
    OwnPtr<RemoteObject>* result,
    Maybe<bool>* wasThrown,
    Maybe<ExceptionDetails>* exceptionDetails)
{
    if (!executionContextId.isJust()) {
        *errorString = "Cannot find default execution context";
        return;
    }
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, executionContextId.fromJust());
    if (!injectedScript)
        return;

    v8::HandleScope scope(injectedScript->isolate());
    v8::Local<v8::Context> localContext = injectedScript->context();
    v8::Context::Scope contextScope(localContext);

    if (!injectedScript->canAccessInspectedWindow()) {
        *errorString = "Can not access given context";
        return;
    }

    v8::MaybeLocal<v8::Object> commandLineAPI = includeCommandLineAPI.fromMaybe(false) ? injectedScript->commandLineAPI(errorString) : v8::MaybeLocal<v8::Object>();
    if (includeCommandLineAPI.fromMaybe(false) && commandLineAPI.IsEmpty())
        return;

    v8::TryCatch tryCatch(injectedScript->isolate());
    v8::MaybeLocal<v8::Value> maybeResultValue = evaluateInternal(injectedScript, doNotPauseOnExceptionsAndMuteConsole.fromMaybe(false), expression, commandLineAPI);

    // InjectedScript may be gone after any evaluate call - find it again.
    injectedScript = m_injectedScriptManager->findInjectedScript(errorString, executionContextId.fromJust());
    if (!injectedScript)
        return;

    injectedScript->wrapEvaluateResult(errorString,
        maybeResultValue,
        tryCatch,
        objectGroup.fromMaybe(""),
        returnByValue.fromMaybe(false),
        generatePreview.fromMaybe(false),
        result,
        wasThrown,
        exceptionDetails);
}

v8::MaybeLocal<v8::Value> V8RuntimeAgentImpl::evaluateInternal(InjectedScript* injectedScript, bool doNotPauseOnExceptionsAndMuteConsole, const String& expression, v8::MaybeLocal<v8::Object> extension)
{
    InjectedScript::ScopedGlobalObjectExtension scopeExtension(injectedScript, extension);
    IgnoreExceptionsScope ignoreExceptionsScope(doNotPauseOnExceptionsAndMuteConsole ? m_debugger : nullptr);
    return m_debugger->compileAndRunInternalScript(injectedScript->context(), toV8String(injectedScript->isolate(), expression));
}

void V8RuntimeAgentImpl::callFunctionOn(ErrorString* errorString,
    const String16& objectId,
    const String16& expression,
    const Maybe<protocol::Array<protocol::Runtime::CallArgument>>& optionalArguments,
    const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
    const Maybe<bool>& returnByValue,
    const Maybe<bool>& generatePreview,
    const Maybe<bool>& userGesture,
    OwnPtr<RemoteObject>* result,
    Maybe<bool>* wasThrown)
{
    OwnPtr<RemoteObjectId> remoteId = RemoteObjectId::parse(errorString, objectId);
    if (!remoteId)
        return;
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return;

    v8::HandleScope scope(injectedScript->isolate());
    v8::Local<v8::Context> localContext = injectedScript->context();
    v8::Context::Scope contextScope(localContext);

    if (!injectedScript->canAccessInspectedWindow()) {
        *errorString = "Can not access given context";
        return;
    }

    String16 objectGroupName = injectedScript->objectGroupName(*remoteId);
    v8::Local<v8::Value> object;
    if (!injectedScript->findObject(errorString, *remoteId, &object))
        return;
    OwnPtr<v8::Local<v8::Value>[]> argv = nullptr;
    int argc = 0;
    if (optionalArguments.isJust()) {
        protocol::Array<protocol::Runtime::CallArgument>* arguments = optionalArguments.fromJust();
        argc = arguments->length();
        argv = adoptArrayPtr(new v8::Local<v8::Value>[argc]);
        for (int i = 0; i < argc; ++i) {
            v8::Local<v8::Value> argumentValue;
            if (!injectedScript->resolveCallArgument(errorString, arguments->get(i)).ToLocal(&argumentValue))
                return;
            argv[i] = argumentValue;
        }
    }

    IgnoreExceptionsScope ignoreExceptionsScope(doNotPauseOnExceptionsAndMuteConsole.fromMaybe(false) ? m_debugger : nullptr);

    v8::MaybeLocal<v8::Object> remoteObjectAPI = injectedScript->remoteObjectAPI(errorString, objectGroupName);
    if (remoteObjectAPI.IsEmpty())
        return;

    v8::TryCatch tryCatch(injectedScript->isolate());
    v8::MaybeLocal<v8::Value> maybeFunctionValue = evaluateInternal(injectedScript, doNotPauseOnExceptionsAndMuteConsole.fromMaybe(false), "(" + expression + ")", remoteObjectAPI);
    // InjectedScript may be gone after any evaluate call - find it again.
    injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return;

    if (tryCatch.HasCaught()) {
        injectedScript->wrapEvaluateResult(errorString, maybeFunctionValue, tryCatch, objectGroupName, false, false, result, wasThrown, nullptr);
        return;
    }

    v8::Local<v8::Value> functionValue;
    if (!maybeFunctionValue.ToLocal(&functionValue) || !functionValue->IsFunction()) {
        *errorString = "Given expression does not evaluate to a function";
        return;
    }

    v8::MaybeLocal<v8::Value> maybeResultValue = m_debugger->callFunction(functionValue.As<v8::Function>(), injectedScript->context(), object, argc, argv.get());
    // InjectedScript may be gone after any evaluate call - find it again.
    injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return;

    injectedScript->wrapEvaluateResult(errorString, maybeResultValue, tryCatch, objectGroupName, returnByValue.fromMaybe(false), generatePreview.fromMaybe(false), result, wasThrown, nullptr);
}

void V8RuntimeAgentImpl::getProperties(
    ErrorString* errorString,
    const String16& objectId,
    const Maybe<bool>& ownProperties,
    const Maybe<bool>& accessorPropertiesOnly,
    const Maybe<bool>& generatePreview,
    OwnPtr<protocol::Array<protocol::Runtime::PropertyDescriptor>>* result,
    Maybe<protocol::Array<protocol::Runtime::InternalPropertyDescriptor>>* internalProperties,
    Maybe<ExceptionDetails>* exceptionDetails)
{
    using protocol::Runtime::InternalPropertyDescriptor;

    OwnPtr<RemoteObjectId> remoteId = RemoteObjectId::parse(errorString, objectId);
    if (!remoteId)
        return;
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return;

    IgnoreExceptionsScope ignoreExceptionsScope(m_debugger);

    v8::HandleScope handles(injectedScript->isolate());
    v8::Context::Scope scope(injectedScript->context());
    v8::Local<v8::Value> objectValue;
    if (!injectedScript->findObject(errorString, *remoteId, &objectValue))
        return;
    if (!objectValue->IsObject()) {
        *errorString = "Value with given id is not an object";
        return;
    }

    v8::Local<v8::Object> object = objectValue.As<v8::Object>();
    String16 objectGroupName = injectedScript->objectGroupName(*remoteId);
    injectedScript->getProperties(errorString, object, objectGroupName, ownProperties.fromMaybe(false), accessorPropertiesOnly.fromMaybe(false), generatePreview.fromMaybe(false), result, exceptionDetails);
    if (!errorString->isEmpty() || exceptionDetails->isJust() || accessorPropertiesOnly.fromMaybe(false))
        return;
    v8::Local<v8::Array> propertiesArray;
    if (hasInternalError(errorString, !v8::Debug::GetInternalProperties(injectedScript->isolate(), objectValue).ToLocal(&propertiesArray)))
        return;
    OwnPtr<protocol::Array<InternalPropertyDescriptor>> propertiesProtocolArray = protocol::Array<InternalPropertyDescriptor>::create();
    for (uint32_t i = 0; i < propertiesArray->Length(); i += 2) {
        v8::Local<v8::Value> name;
        if (hasInternalError(errorString, !propertiesArray->Get(injectedScript->context(), i).ToLocal(&name)) || !name->IsString())
            return;
        v8::Local<v8::Value> value;
        if (hasInternalError(errorString, !propertiesArray->Get(injectedScript->context(), i + 1).ToLocal(&value)))
            return;
        OwnPtr<RemoteObject> wrappedValue = injectedScript->wrapObject(errorString, value, objectGroupName);
        if (!wrappedValue)
            return;
        propertiesProtocolArray->addItem(InternalPropertyDescriptor::create()
            .setName(toProtocolString(name.As<v8::String>()))
            .setValue(wrappedValue.release()).build());
    }
    if (!propertiesProtocolArray->length())
        return;
    *internalProperties = propertiesProtocolArray.release();
}

void V8RuntimeAgentImpl::releaseObject(ErrorString* errorString, const String16& objectId)
{
    OwnPtr<RemoteObjectId> remoteId = RemoteObjectId::parse(errorString, objectId);
    if (!remoteId)
        return;
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return;
    bool pausingOnNextStatement = m_debugger->pausingOnNextStatement();
    if (pausingOnNextStatement)
        m_debugger->setPauseOnNextStatement(false);
    injectedScript->releaseObject(objectId);
    if (pausingOnNextStatement)
        m_debugger->setPauseOnNextStatement(true);
}

void V8RuntimeAgentImpl::releaseObjectGroup(ErrorString*, const String16& objectGroup)
{
    bool pausingOnNextStatement = m_debugger->pausingOnNextStatement();
    if (pausingOnNextStatement)
        m_debugger->setPauseOnNextStatement(false);
    m_injectedScriptManager->releaseObjectGroup(objectGroup);
    if (pausingOnNextStatement)
        m_debugger->setPauseOnNextStatement(true);
}

void V8RuntimeAgentImpl::run(ErrorString* errorString)
{
    *errorString = "Not paused";
}

void V8RuntimeAgentImpl::setCustomObjectFormatterEnabled(ErrorString*, bool enabled)
{
    m_state->setBoolean(V8RuntimeAgentImplState::customObjectFormatterEnabled, enabled);
    m_injectedScriptManager->setCustomObjectFormatterEnabled(enabled);
}

void V8RuntimeAgentImpl::compileScript(ErrorString* errorString,
    const String16& expression,
    const String16& sourceURL,
    bool persistScript,
    int executionContextId,
    Maybe<String16>* scriptId,
    Maybe<ExceptionDetails>* exceptionDetails)
{
    if (!m_enabled) {
        *errorString = "Runtime agent is not enabled";
        return;
    }
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, executionContextId);
    if (!injectedScript)
        return;

    v8::Isolate* isolate = injectedScript->isolate();
    v8::HandleScope handles(isolate);
    v8::Context::Scope scope(injectedScript->context());
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::Script> script = m_debugger->compileInternalScript(injectedScript->context(), toV8String(isolate, expression), sourceURL);
    if (script.IsEmpty()) {
        v8::Local<v8::Message> message = tryCatch.Message();
        if (!message.IsEmpty())
            *exceptionDetails = injectedScript->createExceptionDetails(message);
        else
            *errorString = "Script compilation failed";
        return;
    }

    if (!persistScript)
        return;

    String16 scriptValueId = String16::number(script->GetUnboundScript()->GetId());
    OwnPtr<v8::Global<v8::Script>> global = adoptPtr(new v8::Global<v8::Script>(isolate, script));
    m_compiledScripts.set(scriptValueId, global.release());
    *scriptId = scriptValueId;
}

void V8RuntimeAgentImpl::runScript(ErrorString* errorString,
    const String16& scriptId,
    int executionContextId,
    const Maybe<String16>& objectGroup,
    const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
    const Maybe<bool>& includeCommandLineAPI,
    OwnPtr<RemoteObject>* result,
    Maybe<ExceptionDetails>* exceptionDetails)
{
    if (!m_enabled) {
        *errorString = "Runtime agent is not enabled";
        return;
    }
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, executionContextId);
    if (!injectedScript)
        return;

    IgnoreExceptionsScope ignoreExceptionsScope(doNotPauseOnExceptionsAndMuteConsole.fromMaybe(false) ? m_debugger : nullptr);

    if (!m_compiledScripts.contains(scriptId)) {
        *errorString = "Script execution failed";
        return;
    }

    v8::Isolate* isolate = injectedScript->isolate();
    v8::HandleScope handles(isolate);
    v8::Local<v8::Context> context = injectedScript->context();
    v8::Context::Scope scope(context);
    OwnPtr<v8::Global<v8::Script>> scriptWrapper = m_compiledScripts.take(scriptId);
    v8::Local<v8::Script> script = scriptWrapper->Get(isolate);

    if (script.IsEmpty()) {
        *errorString = "Script execution failed";
        return;
    }

    v8::MaybeLocal<v8::Object> commandLineAPI = includeCommandLineAPI.fromMaybe(false) ? injectedScript->commandLineAPI(errorString) : v8::MaybeLocal<v8::Object>();
    if (includeCommandLineAPI.fromMaybe(false) && commandLineAPI.IsEmpty())
        return;

    InjectedScript::ScopedGlobalObjectExtension scopeExtension(injectedScript, commandLineAPI);

    v8::TryCatch tryCatch(isolate);
    v8::MaybeLocal<v8::Value> maybeResultValue = m_debugger->runCompiledScript(context, script);

    // InjectedScript may be gone after any evaluate call - find it again.
    injectedScript = m_injectedScriptManager->findInjectedScript(errorString, executionContextId);
    if (!injectedScript)
        return;

    injectedScript->wrapEvaluateResult(errorString, maybeResultValue, tryCatch, objectGroup.fromMaybe(""), false, false, result, nullptr, exceptionDetails);
}

void V8RuntimeAgentImpl::setInspectorState(protocol::DictionaryValue* state)
{
    m_state = state;
}

void V8RuntimeAgentImpl::setFrontend(protocol::Frontend::Runtime* frontend)
{
    m_frontend = frontend;
}

void V8RuntimeAgentImpl::clearFrontend()
{
    ErrorString error;
    disable(&error);
    ASSERT(m_frontend);
    m_frontend = nullptr;
}

void V8RuntimeAgentImpl::restore()
{
    m_frontend->executionContextsCleared();
    ErrorString error;
    enable(&error);
    if (m_state->booleanProperty(V8RuntimeAgentImplState::customObjectFormatterEnabled, false))
        m_injectedScriptManager->setCustomObjectFormatterEnabled(true);
}

void V8RuntimeAgentImpl::enable(ErrorString* errorString)
{
    m_enabled = true;
    v8::HandleScope handles(m_debugger->isolate());
    V8ContextInfoVector contexts;
    m_debugger->client()->contextsToReport(m_contextGroupId, contexts);
    for (const V8ContextInfo& info : contexts)
        reportExecutionContextCreated(info);
}

void V8RuntimeAgentImpl::disable(ErrorString* errorString)
{
    if (!m_enabled)
        return;
    m_enabled = false;
    reset();
}

int V8RuntimeAgentImpl::ensureDefaultContextAvailable(v8::Local<v8::Context> context)
{
    InjectedScript* injectedScript = m_injectedScriptManager->injectedScriptFor(context);
    return injectedScript ? injectedScript->contextId() : 0;
}

void V8RuntimeAgentImpl::setClearConsoleCallback(PassOwnPtr<V8RuntimeAgent::ClearConsoleCallback> callback)
{
    m_injectedScriptManager->injectedScriptHost()->setClearConsoleCallback(callback);
}

void V8RuntimeAgentImpl::setInspectObjectCallback(PassOwnPtr<V8RuntimeAgent::InspectCallback> callback)
{
    m_injectedScriptManager->injectedScriptHost()->setInspectObjectCallback(callback);
}

PassOwnPtr<RemoteObject> V8RuntimeAgentImpl::wrapObject(v8::Local<v8::Context> context, v8::Local<v8::Value> value, const String16& groupName, bool generatePreview)
{
    InjectedScript* injectedScript = m_injectedScriptManager->injectedScriptFor(context);
    if (!injectedScript)
        return nullptr;
    ErrorString errorString;
    return injectedScript->wrapObject(&errorString, value, groupName, false, generatePreview);
}

PassOwnPtr<RemoteObject> V8RuntimeAgentImpl::wrapTable(v8::Local<v8::Context> context, v8::Local<v8::Value> table, v8::Local<v8::Value> columns)
{
    InjectedScript* injectedScript = m_injectedScriptManager->injectedScriptFor(context);
    if (!injectedScript)
        return nullptr;
    return injectedScript->wrapTable(table, columns);
}

void V8RuntimeAgentImpl::disposeObjectGroup(const String16& groupName)
{
    m_injectedScriptManager->releaseObjectGroup(groupName);
}

v8::Local<v8::Value> V8RuntimeAgentImpl::findObject(ErrorString* errorString, const String16& objectId, v8::Local<v8::Context>* context, String16* groupName)
{
    OwnPtr<RemoteObjectId> remoteId = RemoteObjectId::parse(errorString, objectId);
    if (!remoteId)
        return v8::Local<v8::Value>();
    InjectedScript* injectedScript = m_injectedScriptManager->findInjectedScript(errorString, remoteId.get());
    if (!injectedScript)
        return v8::Local<v8::Value>();
    v8::Local<v8::Value> objectValue;
    injectedScript->findObject(errorString, *remoteId, &objectValue);
    if (objectValue.IsEmpty())
        return v8::Local<v8::Value>();
    if (context)
        *context = injectedScript->context();
    if (groupName)
        *groupName = injectedScript->objectGroupName(*remoteId);
    return objectValue;
}

void V8RuntimeAgentImpl::addInspectedObject(PassOwnPtr<Inspectable> inspectable)
{
    m_injectedScriptManager->injectedScriptHost()->addInspectedObject(inspectable);
}

void V8RuntimeAgentImpl::reset()
{
    m_compiledScripts.clear();
    m_injectedScriptManager->injectedScriptHost()->clearInspectedObjects();
    m_injectedScriptManager->discardInjectedScripts();
    if (m_enabled)
        m_frontend->executionContextsCleared();
}

void V8RuntimeAgentImpl::reportExecutionContextCreated(const V8ContextInfo& info)
{
    if (!m_enabled)
        return;
    int contextId = V8Debugger::contextId(info.context);
    OwnPtr<protocol::Runtime::ExecutionContextDescription> description = protocol::Runtime::ExecutionContextDescription::create()
        .setId(contextId)
        .setIsDefault(info.isDefault)
        .setName(info.humanReadableName)
        .setOrigin(info.origin)
        .setFrameId(info.frameId).build();
    m_frontend->executionContextCreated(description.release());
    InjectedScript* injectedScript = m_injectedScriptManager->injectedScriptFor(info.context);
    if (injectedScript)
        injectedScript->setOrigin(info.origin);
}

void V8RuntimeAgentImpl::reportExecutionContextDestroyed(v8::Local<v8::Context> context)
{
    int contextId = m_injectedScriptManager->discardInjectedScriptFor(context);
    if (m_enabled && contextId)
        m_frontend->executionContextDestroyed(contextId);
}

} // namespace blink
