// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/svg/SVGResourceClient.h"

#include "core/fetch/DocumentResourceReference.h"
#include "core/layout/svg/LayoutSVGResourceContainer.h"
#include "core/layout/svg/ReferenceFilterBuilder.h"

namespace blink {

SVGResourceClient::~SVGResourceClient()
{
}

void SVGResourceClient::addFilterReferences(const FilterOperations& operations, const Document& document)
{
    for (size_t i = 0; i < operations.size(); ++i) {
        RawPtr<FilterOperation> filterOperation = operations.operations().at(i);
        if (filterOperation->type() != FilterOperation::REFERENCE)
            continue;
        ReferenceFilterOperation* referenceFilterOperation = toReferenceFilterOperation(filterOperation.get());
        DocumentResourceReference* documentReference = ReferenceFilterBuilder::documentResourceReference(referenceFilterOperation);
        DocumentResource* cachedSVGDocument = documentReference ? documentReference->document() : 0;

        if (cachedSVGDocument) {
            cachedSVGDocument->addClient(this);
            m_externalFilterReferences.append(cachedSVGDocument);
        } else {
            Element* element = document.getElementById(referenceFilterOperation->fragment());
            if (!isSVGFilterElement(element))
                continue;
            SVGFilterElement* filter = toSVGFilterElement(element);
            if (filter->layoutObject())
                toLayoutSVGResourceContainer(filter->layoutObject())->addResourceClient(this);
            else
                filter->addClient(this);
            m_internalFilterReferences.add(filter);
        }
    }
}

void SVGResourceClient::clearFilterReferences()
{
    for (SVGFilterElement* filter : m_internalFilterReferences) {
        if (filter->layoutObject())
            toLayoutSVGResourceContainer(filter->layoutObject())->removeResourceClient(this);
        else
            filter->removeClient(this);
    }
    m_internalFilterReferences.clear();

    for (RawPtr<DocumentResource> documentResource : m_externalFilterReferences)
        documentResource->removeClient(this);
    m_externalFilterReferences.clear();
}

void SVGResourceClient::filterWillBeDestroyed(SVGFilterElement* filter)
{
    m_internalFilterReferences.remove(filter);
    filterNeedsInvalidation();
}

void SVGResourceClient::notifyFinished(Resource*)
{
    filterNeedsInvalidation();
}

} // namespace blink
