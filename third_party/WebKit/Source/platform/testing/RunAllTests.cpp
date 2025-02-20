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

#include "base/memory/discardable_memory_allocator.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/test/test_io_thread.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/test/scoped_ipc_support.h"
#include "platform/EventTracer.h"
#include "platform/HTTPNames.h"
#include "platform/graphics/CompositorFactory.h"
#include "platform/heap/Heap.h"
#include "platform/testing/TestingPlatformSupport.h"
#include "public/platform/Platform.h"
#include "wtf/CryptographicallyRandomNumber.h"
#include "wtf/CurrentTime.h"
#include "wtf/Partitions.h"
#include "wtf/WTF.h"
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/command_line.h>
#include <base/metrics/statistics_recorder.h>
#include <base/test/launcher/unit_test_launcher.h>
#include <base/test/test_suite.h>
#include <cc/blink/web_compositor_support_impl.h>

namespace {

double dummyCurrentTime()
{
    return 0.0;
}

int runTestSuite(base::TestSuite* testSuite)
{
    int result = testSuite->Run();
    blink::Heap::collectAllGarbage();
    return result;
}

class DummyPlatform final : public blink::Platform {
public:
    DummyPlatform() { }
};

} // namespace

int main(int argc, char** argv)
{
    base::CommandLine::Init(argc, argv);

    base::TestDiscardableMemoryAllocator discardableMemoryAllocator;
    base::DiscardableMemoryAllocator::SetInstance(&discardableMemoryAllocator);

    base::StatisticsRecorder::Initialize();

    OwnPtr<DummyPlatform> platform = adoptPtr(new DummyPlatform);
    blink::Platform::setCurrentPlatformForTesting(platform.get());

    WTF::Partitions::initialize(nullptr);
    WTF::setTimeFunctionsForTesting(dummyCurrentTime);
    WTF::initialize(nullptr);
    blink::CompositorFactory::initializeDefault();
    int result = 0;
    {
        blink::TestingPlatformSupport::Config platformConfig;
        cc_blink::WebCompositorSupportImpl compositorSupport;
        platformConfig.compositorSupport = &compositorSupport;
        blink::TestingPlatformSupport platform(platformConfig);

        blink::Heap::init();
        blink::ThreadState::attachMainThread();
        blink::ThreadState::current()->registerTraceDOMWrappers(nullptr, nullptr);
        blink::EventTracer::initialize();
        blink::HTTPNames::init();

        base::TestSuite testSuite(argc, argv);

        mojo::edk::Init();
        base::TestIOThread testIoThread(base::TestIOThread::kAutoStart);
        WTF::OwnPtr<mojo::edk::test::ScopedIPCSupport> ipcSupport(adoptPtr(new mojo::edk::test::ScopedIPCSupport(testIoThread.task_runner())));
        result = base::LaunchUnitTests(argc, argv, base::Bind(runTestSuite, base::Unretained(&testSuite)));

        blink::ThreadState::detachMainThread();
        blink::Heap::shutdown();
    }
    blink::CompositorFactory::shutdown();
    WTF::shutdown();
    WTF::Partitions::shutdown();
    return result;
}
