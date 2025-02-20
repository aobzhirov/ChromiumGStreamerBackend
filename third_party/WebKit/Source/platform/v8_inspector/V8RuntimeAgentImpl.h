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

#ifndef V8RuntimeAgentImpl_h
#define V8RuntimeAgentImpl_h

#include "platform/inspector_protocol/Allocator.h"
#include "platform/inspector_protocol/Frontend.h"
#include "platform/v8_inspector/public/V8RuntimeAgent.h"

namespace blink {

class InjectedScript;
class InjectedScriptManager;
class RemoteObjectIdBase;
class V8DebuggerImpl;

namespace protocol {
class DictionaryValue;
}

using protocol::Maybe;

class V8RuntimeAgentImpl : public V8RuntimeAgent {
    PROTOCOL_DISALLOW_COPY(V8RuntimeAgentImpl);
public:
    V8RuntimeAgentImpl(V8DebuggerImpl*, int contextGroupId);
    ~V8RuntimeAgentImpl() override;

    // State management methods.
    void setInspectorState(protocol::DictionaryValue*) override;
    void setFrontend(protocol::Frontend::Runtime*) override;
    void clearFrontend() override;
    void restore() override;

    // Part of the protocol.
    void enable(ErrorString*) override;
    void disable(ErrorString*) override;
    void evaluate(ErrorString*,
        const String16& expression,
        const Maybe<String16>& objectGroup,
        const Maybe<bool>& includeCommandLineAPI,
        const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
        const Maybe<int>& executionContextId,
        const Maybe<bool>& returnByValue,
        const Maybe<bool>& generatePreview,
        const Maybe<bool>& userGesture,
        OwnPtr<protocol::Runtime::RemoteObject>* result,
        Maybe<bool>* wasThrown,
        Maybe<protocol::Runtime::ExceptionDetails>*) override;
    void callFunctionOn(ErrorString*,
        const String16& objectId,
        const String16& expression,
        const Maybe<protocol::Array<protocol::Runtime::CallArgument>>& optionalArguments,
        const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
        const Maybe<bool>& returnByValue,
        const Maybe<bool>& generatePreview,
        const Maybe<bool>& userGesture,
        OwnPtr<protocol::Runtime::RemoteObject>* result,
        Maybe<bool>* wasThrown) override;
    void releaseObject(ErrorString*, const String16& objectId) override;
    void getProperties(ErrorString*,
        const String16& objectId,
        const Maybe<bool>& ownProperties,
        const Maybe<bool>& accessorPropertiesOnly,
        const Maybe<bool>& generatePreview,
        OwnPtr<protocol::Array<protocol::Runtime::PropertyDescriptor>>* result,
        Maybe<protocol::Array<protocol::Runtime::InternalPropertyDescriptor>>* internalProperties,
        Maybe<protocol::Runtime::ExceptionDetails>*) override;
    void releaseObjectGroup(ErrorString*, const String16& objectGroup) override;
    void run(ErrorString*) override;
    void setCustomObjectFormatterEnabled(ErrorString*, bool) override;
    void compileScript(ErrorString*,
        const String16& expression,
        const String16& sourceURL,
        bool persistScript,
        int executionContextId,
        Maybe<String16>*,
        Maybe<protocol::Runtime::ExceptionDetails>*) override;
    void runScript(ErrorString*,
        const String16&,
        int executionContextId,
        const Maybe<String16>& objectGroup,
        const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
        const Maybe<bool>& includeCommandLineAPI,
        OwnPtr<protocol::Runtime::RemoteObject>* result,
        Maybe<protocol::Runtime::ExceptionDetails>*) override;

    V8DebuggerImpl* debugger() { return m_debugger; }
    InjectedScriptManager* getInjectedScriptManager() { return m_injectedScriptManager.get(); }
    int contextGroupId() { return m_contextGroupId; }

    void reset();
    void reportExecutionContextCreated(const V8ContextInfo&);
    void reportExecutionContextDestroyed(v8::Local<v8::Context>);

    void setClearConsoleCallback(PassOwnPtr<ClearConsoleCallback>) override;
    void setInspectObjectCallback(PassOwnPtr<InspectCallback>) override;
    int ensureDefaultContextAvailable(v8::Local<v8::Context>) override;
    PassOwnPtr<protocol::Runtime::RemoteObject> wrapObject(v8::Local<v8::Context>, v8::Local<v8::Value>, const String16& groupName, bool generatePreview = false) override;
    PassOwnPtr<protocol::Runtime::RemoteObject> wrapTable(v8::Local<v8::Context>, v8::Local<v8::Value> table, v8::Local<v8::Value> columns) override;
    void disposeObjectGroup(const String16&) override;
    v8::Local<v8::Value> findObject(ErrorString*, const String16& objectId, v8::Local<v8::Context>* = nullptr, String16* groupName = nullptr) override;
    void addInspectedObject(PassOwnPtr<Inspectable>) override;

private:
    v8::MaybeLocal<v8::Value> evaluateInternal(InjectedScript*, bool doNotPauseOnExceptionsAndMuteConsole, const String& expression, v8::MaybeLocal<v8::Object> extension);

    int m_contextGroupId;
    protocol::DictionaryValue* m_state;
    protocol::Frontend::Runtime* m_frontend;
    OwnPtr<InjectedScriptManager> m_injectedScriptManager;
    V8DebuggerImpl* m_debugger;
    bool m_enabled;
    protocol::HashMap<String16, OwnPtr<v8::Global<v8::Script>>> m_compiledScripts;
};

} // namespace blink

#endif // V8RuntimeAgentImpl_h
