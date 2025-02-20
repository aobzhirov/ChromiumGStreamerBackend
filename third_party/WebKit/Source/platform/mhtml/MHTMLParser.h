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

#ifndef MHTMLParser_h
#define MHTMLParser_h

#include "platform/SharedBufferChunkReader.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"
#include "wtf/RefPtr.h"
#include "wtf/Vector.h"

namespace WTF {
class String;
}

namespace blink {

class ArchiveResource;
class MIMEHeader;
class SharedBuffer;

class PLATFORM_EXPORT MHTMLParser final {
    STACK_ALLOCATED();
public:
    explicit MHTMLParser(SharedBuffer*);

    HeapVector<Member<ArchiveResource>> parseArchive();

    // Translates |contentIDFromMimeHeader| (of the form "<foo@bar.com>")
    // into a cid-scheme URI (of the form "cid:foo@bar.com").
    //
    // Returns KURL() - an invalid URL - if contentID is invalid.
    //
    // See rfc2557 - section 8.3 - "Use of the Content-ID header and CID URLs".
    static KURL convertContentIDToURI(const String& contentID);

private:
    bool parseArchiveWithHeader(MIMEHeader*, HeapVector<Member<ArchiveResource>>&);
    RawPtr<ArchiveResource> parseNextPart(const MIMEHeader&, const String& endOfPartBoundary, const String& endOfDocumentBoundary, bool& endOfArchiveReached);

    SharedBufferChunkReader m_lineReader;
};

} // namespace blink

#endif
