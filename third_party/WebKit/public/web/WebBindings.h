/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#ifndef WebBindings_h
#define WebBindings_h

#include "../platform/WebCommon.h"
#include <bindings/npruntime.h>

namespace blink {

// A haphazard collection of functions for dealing with plugins.
class WebBindings {
public:
    // NPN Functions ------------------------------------------------------
    // These are all defined in npruntime.h and are well documented.

    // NPN_Construct
    BLINK_EXPORT static bool construct(NPP, NPObject*, const NPVariant* args, uint32_t argCount, NPVariant* result);

    // NPN_CreateObject
    BLINK_EXPORT static NPObject* createObject(NPP, NPClass*);

    // NPN_Enumerate
    BLINK_EXPORT static bool enumerate(NPP, NPObject*, NPIdentifier**, uint32_t* identifierCount);

    // NPN_Evaluate
    BLINK_EXPORT static bool evaluate(NPP, NPObject*, NPString* script, NPVariant* result);

    // NPN_GetIntIdentifier
    BLINK_EXPORT static NPIdentifier getIntIdentifier(int32_t number);

    // NPN_GetProperty
    BLINK_EXPORT static bool getProperty(NPP, NPObject*, NPIdentifier property, NPVariant *result);

    // NPN_GetStringIdentifier
    BLINK_EXPORT static NPIdentifier getStringIdentifier(const NPUTF8* string);

    // NPN_GetStringIdentifiers
    BLINK_EXPORT static void getStringIdentifiers(const NPUTF8** names, int32_t nameCount, NPIdentifier*);

    // NPN_HasMethod
    BLINK_EXPORT static bool hasMethod(NPP, NPObject*, NPIdentifier method);

    // NPN_HasProperty
    BLINK_EXPORT static bool hasProperty(NPP, NPObject*, NPIdentifier property);

    // NPN_IdentifierIsString
    BLINK_EXPORT static bool identifierIsString(NPIdentifier);

    // NPN_IntFromIdentifier
    BLINK_EXPORT static int32_t intFromIdentifier(NPIdentifier);

    // NPN_Invoke
    BLINK_EXPORT static bool invoke(NPP, NPObject*, NPIdentifier method, const NPVariant* args, uint32_t argCount, NPVariant* result);

    // NPN_InvokeDefault
    BLINK_EXPORT static bool invokeDefault(NPP, NPObject*, const NPVariant* args, uint32_t argCount, NPVariant* result);

    // NPN_ReleaseObject
    BLINK_EXPORT static void releaseObject(NPObject*);

    // NPN_ReleaseVariantValue
    BLINK_EXPORT static void releaseVariantValue(NPVariant*);

    // NPN_RemoveProperty
    BLINK_EXPORT static bool removeProperty(NPP, NPObject*, NPIdentifier);

    // NPN_RetainObject
    BLINK_EXPORT static NPObject* retainObject(NPObject*);

    // NPN_SetException
    BLINK_EXPORT static void setException(NPObject*, const NPUTF8* message);

    // NPN_SetProperty
    BLINK_EXPORT static bool setProperty(NPP, NPObject*, NPIdentifier, const NPVariant*);

    // NPN_UTF8FromIdentifier
    BLINK_EXPORT static NPUTF8* utf8FromIdentifier(NPIdentifier);
};

} // namespace blink

#endif
