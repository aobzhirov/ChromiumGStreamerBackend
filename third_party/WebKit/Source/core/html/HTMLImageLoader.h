/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004 Apple Computer, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef HTMLImageLoader_h
#define HTMLImageLoader_h

#include "core/CoreExport.h"
#include "core/loader/ImageLoader.h"

namespace blink {

class CORE_EXPORT HTMLImageLoader final : public ImageLoader {
public:
    static RawPtr<HTMLImageLoader> create(Element* element)
    {
        return new HTMLImageLoader(element);
    }
    ~HTMLImageLoader() override;

    void dispatchLoadEvent() override;

    void notifyFinished(Resource*) override;
    String debugName() const override { return "HTMLImageLoader"; }

private:
    explicit HTMLImageLoader(Element*);
    void noImageResourceToLoad() override;
    void ensureFallbackContent();
};

} // namespace blink

#endif
