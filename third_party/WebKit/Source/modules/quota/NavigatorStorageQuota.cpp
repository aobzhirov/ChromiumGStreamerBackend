/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#include "modules/quota/NavigatorStorageQuota.h"

#include "core/frame/Navigator.h"
#include "modules/quota/DeprecatedStorageQuota.h"
#include "modules/quota/StorageManager.h"
#include "modules/quota/StorageQuota.h"

namespace blink {

NavigatorStorageQuota::NavigatorStorageQuota(LocalFrame* frame)
    : DOMWindowProperty(frame)
{
}

NavigatorStorageQuota::~NavigatorStorageQuota()
{
}

const char* NavigatorStorageQuota::supplementName()
{
    return "NavigatorStorageQuota";
}

NavigatorStorageQuota& NavigatorStorageQuota::from(Navigator& navigator)
{
    NavigatorStorageQuota* supplement = static_cast<NavigatorStorageQuota*>(Supplement<Navigator>::from(navigator, supplementName()));
    if (!supplement) {
        supplement = new NavigatorStorageQuota(navigator.frame());
        provideTo(navigator, supplementName(), supplement);
    }
    return *supplement;
}

StorageQuota* NavigatorStorageQuota::storageQuota(Navigator& navigator)
{
    return NavigatorStorageQuota::from(navigator).storageQuota();
}

DeprecatedStorageQuota* NavigatorStorageQuota::webkitTemporaryStorage(Navigator& navigator)
{
    return NavigatorStorageQuota::from(navigator).webkitTemporaryStorage();
}

DeprecatedStorageQuota* NavigatorStorageQuota::webkitPersistentStorage(Navigator& navigator)
{
    return NavigatorStorageQuota::from(navigator).webkitPersistentStorage();
}

StorageManager* NavigatorStorageQuota::storage(Navigator& navigator)
{
    return NavigatorStorageQuota::from(navigator).storage();
}

StorageQuota* NavigatorStorageQuota::storageQuota() const
{
    if (!m_storageQuota && frame())
        m_storageQuota = StorageQuota::create();
    return m_storageQuota.get();
}

DeprecatedStorageQuota* NavigatorStorageQuota::webkitTemporaryStorage() const
{
    if (!m_temporaryStorage && frame())
        m_temporaryStorage = DeprecatedStorageQuota::create(DeprecatedStorageQuota::Temporary);
    return m_temporaryStorage.get();
}

DeprecatedStorageQuota* NavigatorStorageQuota::webkitPersistentStorage() const
{
    if (!m_persistentStorage && frame())
        m_persistentStorage = DeprecatedStorageQuota::create(DeprecatedStorageQuota::Persistent);
    return m_persistentStorage.get();
}

StorageManager* NavigatorStorageQuota::storage() const
{
    if (!m_storageManager && frame())
        m_storageManager = new StorageManager();
    return m_storageManager.get();
}

DEFINE_TRACE(NavigatorStorageQuota)
{
    visitor->trace(m_storageQuota);
    visitor->trace(m_temporaryStorage);
    visitor->trace(m_persistentStorage);
    visitor->trace(m_storageManager);
    Supplement<Navigator>::trace(visitor);
    DOMWindowProperty::trace(visitor);
}

} // namespace blink
