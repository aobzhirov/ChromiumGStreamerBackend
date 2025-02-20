/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller <mueller@kde.org>
    Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
    Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef ImageResource_h
#define ImageResource_h

#include "core/CoreExport.h"
#include "core/fetch/MultipartImageResourceParser.h"
#include "core/fetch/Resource.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/IntSizeHash.h"
#include "platform/geometry/LayoutSize.h"
#include "platform/graphics/ImageObserver.h"
#include "platform/graphics/ImageOrientation.h"
#include "wtf/HashMap.h"

namespace blink {

class FetchRequest;
class FloatSize;
class ImageResourceObserver;
class MemoryCache;
class ResourceClient;
class ResourceFetcher;
class SecurityOrigin;

class CORE_EXPORT ImageResource final : public Resource, public ImageObserver, public MultipartImageResourceParser::Client {
    friend class MemoryCache;
    USING_GARBAGE_COLLECTED_MIXIN(ImageResource);
public:
    using ClientType = ResourceClient;

    static RawPtr<ImageResource> fetch(FetchRequest&, ResourceFetcher*);

    static RawPtr<ImageResource> create(blink::Image* image)
    {
        return new ImageResource(image, ResourceLoaderOptions());
    }

    // Exposed for testing
    static RawPtr<ImageResource> create(const ResourceRequest& request, blink::Image* image)
    {
        return new ImageResource(request, image, ResourceLoaderOptions());
    }

    ~ImageResource() override;

    blink::Image* getImage(); // Returns the nullImage() if the image is not available yet.
    bool hasImage() const { return m_image.get(); }

    static std::pair<blink::Image*, float> brokenImage(float deviceScaleFactor); // Returns an image and the image's resolution scale factor.
    bool willPaintBrokenImage() const;

    bool usesImageContainerSize() const;
    bool imageHasRelativeSize() const;
    // The device pixel ratio we got from the server for this image, or 1.0.
    float devicePixelRatioHeaderValue() const { return m_devicePixelRatioHeaderValue; }
    bool hasDevicePixelRatioHeaderValue() const { return m_hasDevicePixelRatioHeaderValue; }

    enum SizeType {
        IntrinsicSize, // Report the intrinsic size.
        IntrinsicCorrectedToDPR, // Report the intrinsic size corrected to account for image density.
    };
    // This method takes a zoom multiplier that can be used to increase the natural size of the image by the zoom.
    LayoutSize imageSize(RespectImageOrientationEnum shouldRespectImageOrientation, float multiplier, SizeType = IntrinsicSize);

    bool isAccessAllowed(SecurityOrigin*);

    void updateImageAnimationPolicy();

    // If this ImageResource has the Lo-Fi response headers, reload it with
    // the Lo-Fi state set to off and bypassing the cache.
    void reloadIfLoFi(ResourceFetcher*);

    void addObserver(ImageResourceObserver*);
    void removeObserver(ImageResourceObserver*);
    bool hasClientsOrObservers() const override { return Resource::hasClientsOrObservers() || !m_observers.isEmpty() || !m_finishedObservers.isEmpty(); }

    ResourcePriority priorityFromObservers() override;

    void allClientsAndObserversRemoved() override;

    void appendData(const char*, size_t) override;
    void error(Resource::Status) override;
    void responseReceived(const ResourceResponse&, PassOwnPtr<WebDataConsumerHandle>) override;
    void finish() override;

    // For compatibility, images keep loading even if there are HTTP errors.
    bool shouldIgnoreHTTPStatusCodeErrors() const override { return true; }

    bool isImage() const override { return true; }

    // ImageObserver
    void decodedSizeChanged(const blink::Image*, int delta) override;
    void didDraw(const blink::Image*) override;

    bool shouldPauseAnimation(const blink::Image*) override;
    void animationAdvanced(const blink::Image*) override;
    void changedInRect(const blink::Image*, const IntRect&) override;

    // MultipartImageResourceParser::Client
    void onePartInMultipartReceived(const ResourceResponse&) final;
    void multipartDataReceived(const char*, size_t) final;

    DECLARE_VIRTUAL_TRACE();

protected:
    bool isSafeToUnlock() const override;
    void destroyDecodedDataIfPossible() override;
    void destroyDecodedDataForFailedRevalidation() override;

private:
    explicit ImageResource(blink::Image*, const ResourceLoaderOptions&);
    ImageResource(const ResourceRequest&, blink::Image*, const ResourceLoaderOptions&);

    enum class MultipartParsingState : uint8_t {
        WaitingForFirstPart,
        ParsingFirstPart,
        FinishedParsingFirstPart,
    };

    class ImageResourceFactory : public ResourceFactory {
    public:
        ImageResourceFactory()
            : ResourceFactory(Resource::Image) { }

        RawPtr<Resource> create(const ResourceRequest& request, const ResourceLoaderOptions& options, const String&) const override
        {
            return new ImageResource(request, options);
        }
    };
    ImageResource(const ResourceRequest&, const ResourceLoaderOptions&);

    void clear();

    void setCustomAcceptHeader();
    void createImage();
    void updateImage(bool allDataReceived);
    void clearImage();
    // If not null, changeRect is the changed part of the image.
    void notifyObservers(bool isNotifyingFinish, const IntRect* changeRect = nullptr);
    void notifyObserver(ImageResourceObserver*, bool isNotifyingFinish, const IntRect* changeRect = nullptr);

    float m_devicePixelRatioHeaderValue;

    Member<MultipartImageResourceParser> m_multipartParser;
    RefPtr<blink::Image> m_image;
    MultipartParsingState m_multipartParsingState = MultipartParsingState::WaitingForFirstPart;
    bool m_hasDevicePixelRatioHeaderValue;
    HashCountedSet<ImageResourceObserver*> m_observers;
    HashCountedSet<ImageResourceObserver*> m_finishedObservers;
};

DEFINE_RESOURCE_TYPE_CASTS(Image);

} // namespace blink

#endif
