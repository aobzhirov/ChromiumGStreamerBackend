/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef InspectorDOMStorageAgent_h
#define InspectorDOMStorageAgent_h

#include "core/inspector/InspectorBaseAgent.h"
#include "modules/ModulesExport.h"
#include "modules/storage/StorageArea.h"
#include "wtf/HashMap.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/text/WTFString.h"

namespace blink {

class LocalFrame;
class Page;
class StorageArea;

namespace protocol {
class DictionaryValue;
}


class MODULES_EXPORT InspectorDOMStorageAgent final : public InspectorBaseAgent<InspectorDOMStorageAgent, protocol::Frontend::DOMStorage>, public protocol::Backend::DOMStorage {
public:
    static RawPtr<InspectorDOMStorageAgent> create(Page* page)
    {
        return new InspectorDOMStorageAgent(page);
    }

    ~InspectorDOMStorageAgent() override;
    DECLARE_VIRTUAL_TRACE();

    void didDispatchDOMStorageEvent(const String& key, const String& oldValue, const String& newValue, StorageType, SecurityOrigin*);

private:
    explicit InspectorDOMStorageAgent(Page*);

    // InspectorBaseAgent overrides.
    void restore() override;

    // protocol::Dispatcher::DOMStorageCommandHandler overrides.
    void enable(ErrorString*) override;
    void disable(ErrorString*) override;
    void getDOMStorageItems(ErrorString*, PassOwnPtr<protocol::DOMStorage::StorageId> in_storageId, OwnPtr<protocol::Array<protocol::Array<String>>>* out_entries) override;
    void setDOMStorageItem(ErrorString*, PassOwnPtr<protocol::DOMStorage::StorageId> in_storageId, const String& in_key, const String& in_value) override;
    void removeDOMStorageItem(ErrorString*, PassOwnPtr<protocol::DOMStorage::StorageId> in_storageId, const String& in_key) override;

    StorageArea* findStorageArea(ErrorString*, PassOwnPtr<protocol::DOMStorage::StorageId>, LocalFrame*&);
    PassOwnPtr<protocol::DOMStorage::StorageId> storageId(SecurityOrigin*, bool isLocalStorage);

    Member<Page> m_page;
    bool m_isEnabled;
};

} // namespace blink

#endif // !defined(InspectorDOMStorageAgent_h)
