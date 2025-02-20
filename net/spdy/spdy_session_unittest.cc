// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_session.h"

#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/proxy_delegate.h"
#include "net/base/request_priority.h"
#include "net/base/test_data_directory.h"
#include "net/base/test_data_stream.h"
#include "net/base/test_proxy_delegate.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_entry.h"
#include "net/log/test_net_log_util.h"
#include "net/proxy/proxy_server.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_test_util.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/spdy/spdy_session_test_util.h"
#include "net/spdy/spdy_stream.h"
#include "net/spdy/spdy_stream_test_util.h"
#include "net/spdy/spdy_test_util_common.h"
#include "net/spdy/spdy_test_utils.h"
#include "net/test/cert_test_util.h"
#include "testing/platform_test.h"

namespace net {

namespace {

enum TestCase {
  // Test using the SPDY/3.1 protocol.
  kTestCaseSPDY31,

  // Test using the HTTP/2 protocol, without specifying a stream
  // dependency based on the RequestPriority.
  kTestCaseHTTP2NoPriorityDependencies,

  // Test using the HTTP/2 protocol, specifying a stream
  // dependency based on the RequestPriority.
  kTestCaseHTTP2PriorityDependencies
};

const char kHttpURLFromAnotherOrigin[] = "http://www.example2.org/a.dat";
const char kHttpsURLFromAnotherOrigin[] = "https://www.example2.org/b.dat";

const char kBodyData[] = "Body data";
const size_t kBodyDataSize = arraysize(kBodyData);
const base::StringPiece kBodyDataStringPiece(kBodyData, kBodyDataSize);

static base::TimeDelta g_time_delta;
static base::TimeTicks g_time_now;

base::TimeTicks TheNearFuture() {
  return base::TimeTicks::Now() + g_time_delta;
}

base::TimeTicks SlowReads() {
  g_time_delta +=
      base::TimeDelta::FromMilliseconds(2 * kYieldAfterDurationMilliseconds);
  return base::TimeTicks::Now() + g_time_delta;
}

base::TimeTicks InstantaneousReads() {
  return g_time_now;
}

}  // namespace

class SpdySessionTest : public PlatformTest,
                        public ::testing::WithParamInterface<TestCase> {
 public:
  // Functions used with RunResumeAfterUnstallTest().

  void StallSessionOnly(SpdyStream* stream) { StallSessionSend(); }

  void StallStreamOnly(SpdyStream* stream) { StallStreamSend(stream); }

  void StallSessionStream(SpdyStream* stream) {
    StallSessionSend();
    StallStreamSend(stream);
  }

  void StallStreamSession(SpdyStream* stream) {
    StallStreamSend(stream);
    StallSessionSend();
  }

  void UnstallSessionOnly(SpdyStream* stream, int32_t delta_window_size) {
    UnstallSessionSend(delta_window_size);
  }

  void UnstallStreamOnly(SpdyStream* stream, int32_t delta_window_size) {
    UnstallStreamSend(stream, delta_window_size);
  }

  void UnstallSessionStream(SpdyStream* stream, int32_t delta_window_size) {
    UnstallSessionSend(delta_window_size);
    UnstallStreamSend(stream, delta_window_size);
  }

  void UnstallStreamSession(SpdyStream* stream, int32_t delta_window_size) {
    UnstallStreamSend(stream, delta_window_size);
    UnstallSessionSend(delta_window_size);
  }

 protected:
  NextProto GetProtocol() const {
    return GetParam() == kTestCaseSPDY31 ? kProtoSPDY31 : kProtoHTTP2;
  }

  bool GetDependenciesFromPriority() const {
    return GetParam() == kTestCaseHTTP2PriorityDependencies;
  }

  SpdySessionTest()
      : old_max_group_sockets_(ClientSocketPoolManager::max_sockets_per_group(
            HttpNetworkSession::NORMAL_SOCKET_POOL)),
        old_max_pool_sockets_(ClientSocketPoolManager::max_sockets_per_pool(
            HttpNetworkSession::NORMAL_SOCKET_POOL)),
        spdy_util_(GetProtocol(), GetDependenciesFromPriority()),
        session_deps_(GetProtocol()),
        spdy_session_pool_(nullptr),
        test_url_(kDefaultURL),
        test_host_port_pair_(HostPortPair::FromURL(test_url_)),
        key_(test_host_port_pair_,
             ProxyServer::Direct(),
             PRIVACY_MODE_DISABLED) {
    session_deps_.enable_priority_dependencies = GetDependenciesFromPriority();
  }

  virtual ~SpdySessionTest() {
    // Important to restore the per-pool limit first, since the pool limit must
    // always be greater than group limit, and the tests reduce both limits.
    ClientSocketPoolManager::set_max_sockets_per_pool(
        HttpNetworkSession::NORMAL_SOCKET_POOL, old_max_pool_sockets_);
    ClientSocketPoolManager::set_max_sockets_per_group(
        HttpNetworkSession::NORMAL_SOCKET_POOL, old_max_group_sockets_);
  }

  void SetUp() override {
    g_time_delta = base::TimeDelta();
    g_time_now = base::TimeTicks::Now();
    session_deps_.net_log = log_.bound().net_log();
  }

  void CreateNetworkSession() {
    DCHECK(!http_session_);
    DCHECK(!spdy_session_pool_);
    http_session_ =
        SpdySessionDependencies::SpdyCreateSession(&session_deps_);
    spdy_session_pool_ = http_session_->spdy_session_pool();
  }

  void CreateInsecureSpdySession() {
    DCHECK(!session_);
    session_ = ::net::CreateInsecureSpdySession(http_session_.get(), key_,
                                                log_.bound());
  }

  void StallSessionSend() {
    // Reduce the send window size to 0 to stall.
    while (session_->session_send_window_size_ > 0) {
      session_->DecreaseSendWindowSize(std::min(
          kMaxSpdyFrameChunkSize, session_->session_send_window_size_));
    }
  }

  void UnstallSessionSend(int32_t delta_window_size) {
    session_->IncreaseSendWindowSize(delta_window_size);
  }

  void StallStreamSend(SpdyStream* stream) {
    // Reduce the send window size to 0 to stall.
    while (stream->send_window_size() > 0) {
      stream->DecreaseSendWindowSize(
          std::min(kMaxSpdyFrameChunkSize, stream->send_window_size()));
    }
  }

  void UnstallStreamSend(SpdyStream* stream, int32_t delta_window_size) {
    stream->IncreaseSendWindowSize(delta_window_size);
  }

  void RunResumeAfterUnstallTest(
      const base::Callback<void(SpdyStream*)>& stall_function,
      const base::Callback<void(SpdyStream*, int32_t)>& unstall_function);

  // Original socket limits.  Some tests set these.  Safest to always restore
  // them once each test has been run.
  int old_max_group_sockets_;
  int old_max_pool_sockets_;

  SpdyTestUtil spdy_util_;
  SpdySessionDependencies session_deps_;
  scoped_ptr<HttpNetworkSession> http_session_;
  base::WeakPtr<SpdySession> session_;
  SpdySessionPool* spdy_session_pool_;
  GURL test_url_;
  HostPortPair test_host_port_pair_;
  SpdySessionKey key_;
  BoundTestNetLog log_;
};

INSTANTIATE_TEST_CASE_P(ProtoPlusDepend,
                        SpdySessionTest,
                        testing::Values(kTestCaseSPDY31,
                                        kTestCaseHTTP2NoPriorityDependencies,
                                        kTestCaseHTTP2PriorityDependencies));

// Try to create a SPDY session that will fail during
// initialization. Nothing should blow up.
TEST_P(SpdySessionTest, InitialReadError) {
  CreateNetworkSession();

  session_ = TryCreateFakeSpdySessionExpectingFailure(spdy_session_pool_, key_,
                                                      ERR_CONNECTION_CLOSED);
  EXPECT_TRUE(session_);
  // Flush the read.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

namespace {

// A helper class that vends a callback that, when fired, destroys a
// given SpdyStreamRequest.
class StreamRequestDestroyingCallback : public TestCompletionCallbackBase {
 public:
  StreamRequestDestroyingCallback() {}

  ~StreamRequestDestroyingCallback() override {}

  void SetRequestToDestroy(scoped_ptr<SpdyStreamRequest> request) {
    request_ = std::move(request);
  }

  CompletionCallback MakeCallback() {
    return base::Bind(&StreamRequestDestroyingCallback::OnComplete,
                      base::Unretained(this));
  }

 private:
  void OnComplete(int result) {
    request_.reset();
    SetResult(result);
  }

  scoped_ptr<SpdyStreamRequest> request_;
};

}  // namespace

// Request kInitialMaxConcurrentStreams streams.  Request two more
// streams, but have the callback for one destroy the second stream
// request. Close the session. Nothing should blow up. This is a
// regression test for http://crbug.com/250841 .
TEST_P(SpdySessionTest, PendingStreamCancellingAnother) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {MockRead(ASYNC, 0, 0), };

  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Create the maximum number of concurrent streams.
  for (size_t i = 0; i < kInitialMaxConcurrentStreams; ++i) {
    base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
        SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
    ASSERT_TRUE(spdy_stream != nullptr);
  }

  SpdyStreamRequest request1;
  scoped_ptr<SpdyStreamRequest> request2(new SpdyStreamRequest);

  StreamRequestDestroyingCallback callback1;
  ASSERT_EQ(
      ERR_IO_PENDING,
      request1.StartRequest(SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                            MEDIUM, BoundNetLog(), callback1.MakeCallback()));

  // |callback2| is never called.
  TestCompletionCallback callback2;
  ASSERT_EQ(ERR_IO_PENDING, request2->StartRequest(
                                SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                MEDIUM, BoundNetLog(), callback2.callback()));

  callback1.SetRequestToDestroy(std::move(request2));

  session_->CloseSessionOnError(ERR_ABORTED, "Aborting session");

  EXPECT_EQ(ERR_ABORTED, callback1.WaitForResult());
}

// A session receiving a GOAWAY frame with no active streams should close.
TEST_P(SpdySessionTest, GoAwayWithNoActiveStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  MockRead reads[] = {
    CreateMockRead(*goaway, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the GOAWAY frame.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(session_);
}

// A session receiving a GOAWAY frame immediately with no active
// streams should then close.
TEST_P(SpdySessionTest, GoAwayImmediatelyWithNoActiveStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  MockRead reads[] = {
      CreateMockRead(*goaway, 0, SYNCHRONOUS), MockRead(ASYNC, 0, 1)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();

  session_ = TryCreateInsecureSpdySessionExpectingFailure(
      http_session_.get(), key_, ERR_CONNECTION_CLOSED, BoundNetLog());
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(session_);
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(data.AllReadDataConsumed());
}

// A session receiving a GOAWAY frame with active streams should close
// when the last active stream is closed.
TEST_P(SpdySessionTest, GoAwayWithActiveStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*goaway, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      MockRead(ASYNC, 0, 5)  // EOF
  };
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  scoped_ptr<SpdyHeaderBlock> headers2(new SpdyHeaderBlock(*headers));

  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_EQ(3u, spdy_stream2->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the GOAWAY frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_FALSE(session_->IsStreamActive(3));
  EXPECT_FALSE(spdy_stream2);
  EXPECT_TRUE(session_->IsStreamActive(1));

  EXPECT_TRUE(session_->IsGoingAway());

  // Should close the session.
  spdy_stream1->Close();
  EXPECT_FALSE(spdy_stream1);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Regression test for https://crbug.com/547130.
TEST_P(SpdySessionTest, GoAwayWithActiveAndCreatedStream) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(0));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), CreateMockRead(*goaway, 2),
  };

  // No |req2|, because the second stream will never get activated.
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
      CreateMockWrite(*req1, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);
  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  EXPECT_EQ(0u, spdy_stream1->stream_id());

  // Active stream 1.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_TRUE(session_->IsStreamActive(1));

  // Create stream corresponding to the next request.
  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());

  EXPECT_EQ(0u, spdy_stream2->stream_id());

  // Read and process the GOAWAY frame before the second stream could be
  // activated.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(session_);

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Have a session receive two GOAWAY frames, with the last one causing
// the last active stream to be closed. The session should then be
// closed after the second GOAWAY frame.
TEST_P(SpdySessionTest, GoAwayTwice) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway1(spdy_util_.ConstructSpdyGoAway(1));
  scoped_ptr<SpdyFrame> goaway2(spdy_util_.ConstructSpdyGoAway(0));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*goaway1, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      CreateMockRead(*goaway2, 5),
      MockRead(ASYNC, ERR_IO_PENDING, 6),
      MockRead(ASYNC, 0, 7)  // EOF
  };
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  scoped_ptr<SpdyHeaderBlock> headers2(new SpdyHeaderBlock(*headers));

  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_EQ(3u, spdy_stream2->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the first GOAWAY frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_FALSE(session_->IsStreamActive(3));
  EXPECT_FALSE(spdy_stream2);
  EXPECT_TRUE(session_->IsStreamActive(1));
  EXPECT_TRUE(session_->IsGoingAway());

  // Read and process the second GOAWAY frame, which should close the
  // session.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Have a session with active streams receive a GOAWAY frame and then
// close it. It should handle the close properly (i.e., not try to
// make itself unavailable in its pool twice).
TEST_P(SpdySessionTest, GoAwayWithActiveStreamsThenClose) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*goaway, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      MockRead(ASYNC, 0, 5)  // EOF
  };
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  scoped_ptr<SpdyHeaderBlock> headers2(new SpdyHeaderBlock(*headers));

  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_EQ(3u, spdy_stream2->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the GOAWAY frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_FALSE(session_->IsStreamActive(3));
  EXPECT_FALSE(spdy_stream2);
  EXPECT_TRUE(session_->IsStreamActive(1));
  EXPECT_TRUE(session_->IsGoingAway());

  session_->CloseSessionOnError(ERR_ABORTED, "Aborting session");
  EXPECT_FALSE(spdy_stream1);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Process a joint read buffer which causes the session to begin draining, and
// then processes a GOAWAY. The session should gracefully drain. Regression test
// for crbug.com/379469
TEST_P(SpdySessionTest, GoAwayWhileDraining) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  scoped_ptr<SpdyFrame> body(spdy_util_.ConstructSpdyBodyFrame(1, true));
  size_t joint_size = goaway->size() * 2 + body->size();

  // Compose interleaved |goaway| and |body| frames into a single read.
  scoped_ptr<char[]> buffer(new char[joint_size]);
  {
    size_t out = 0;
    memcpy(&buffer[out], goaway->data(), goaway->size());
    out += goaway->size();
    memcpy(&buffer[out], body->data(), body->size());
    out += body->size();
    memcpy(&buffer[out], goaway->data(), goaway->size());
    out += goaway->size();
    ASSERT_EQ(out, joint_size);
  }
  SpdyFrame joint_frames(buffer.get(), joint_size, false);

  MockRead reads[] = {
      CreateMockRead(*resp, 1), CreateMockRead(joint_frames, 2),
      MockRead(ASYNC, 0, 3)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  // Stream and session closed gracefully.
  EXPECT_TRUE(delegate.StreamIsClosed());
  EXPECT_EQ(OK, delegate.WaitForClose());
  EXPECT_EQ(kUploadData, delegate.TakeReceivedData());
  EXPECT_FALSE(session_);
}

// Try to create a stream after receiving a GOAWAY frame. It should
// fail.
TEST_P(SpdySessionTest, CreateStreamAfterGoAway) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*goaway, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      MockRead(ASYNC, 0, 4)  // EOF
  };
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the GOAWAY frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_TRUE(session_->IsStreamActive(1));

  SpdyStreamRequest stream_request;
  int rv = stream_request.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_,
                                       test_url_, MEDIUM, BoundNetLog(),
                                       CompletionCallback());
  EXPECT_EQ(ERR_FAILED, rv);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Receiving a SYN_STREAM frame after a GOAWAY frame should result in
// the stream being refused.
TEST_P(SpdySessionTest, SynStreamAfterGoAway) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(1));
  scoped_ptr<SpdyFrame> push(
      spdy_util_.ConstructSpdyPush(nullptr, 0, 2, 1, kDefaultURL));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*goaway, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*push, 4),
      MockRead(ASYNC, 0, 6)  // EOF
  };
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(2, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {CreateMockWrite(*req, 0), CreateMockWrite(*rst, 5)};
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Read and process the GOAWAY frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_TRUE(session_->IsStreamActive(1));

  // Read and process the SYN_STREAM frame, the subsequent RST_STREAM,
  // and EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// A session observing a network change with active streams should close
// when the last active stream is closed.
TEST_P(SpdySessionTest, NetworkChangeWithActiveStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), MockRead(ASYNC, 0, 2)  // EOF
  };
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(spdy_util_.spdy_version(), session_->GetProtocolVersion());

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));

  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream->stream_id());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  spdy_session_pool_->OnIPAddressChanged();

  // The SpdySessionPool behavior differs based on how the OSs reacts to
  // network changes; see comment in SpdySessionPool::OnIPAddressChanged().
#if defined(OS_ANDROID) || defined(OS_WIN) || defined(OS_IOS)
  // For OSs where the TCP connections will close upon relevant network
  // changes, SpdySessionPool doesn't need to force them to close, so in these
  // cases verify the session has become unavailable but remains open and the
  // pre-existing stream is still active.
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_TRUE(session_->IsGoingAway());

  EXPECT_TRUE(session_->IsStreamActive(1));

  // Should close the session.
  spdy_stream->Close();
#endif
  EXPECT_FALSE(spdy_stream);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, ClientPing) {
  session_deps_.enable_ping = true;
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> read_ping(spdy_util_.ConstructSpdyPing(1, true));
  MockRead reads[] = {
      CreateMockRead(*read_ping, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      MockRead(ASYNC, 0, 3)  // EOF
  };
  scoped_ptr<SpdyFrame> write_ping(spdy_util_.ConstructSpdyPing(1, false));
  MockWrite writes[] = {
    CreateMockWrite(*write_ping, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  test::StreamDelegateSendImmediate delegate(spdy_stream1, nullptr);
  spdy_stream1->SetDelegate(&delegate);

  base::TimeTicks before_ping_time = base::TimeTicks::Now();

  session_->set_connection_at_risk_of_loss_time(
      base::TimeDelta::FromSeconds(-1));
  session_->set_hung_interval(base::TimeDelta::FromMilliseconds(50));

  session_->SendPrefacePingIfNoneInFlight();

  base::RunLoop().RunUntilIdle();

  session_->CheckPingStatus(before_ping_time);

  EXPECT_EQ(0, session_->pings_in_flight());
  EXPECT_GE(session_->next_ping_id(), 1U);
  EXPECT_FALSE(session_->check_ping_status_pending());
  EXPECT_GE(session_->last_activity_time(), before_ping_time);

  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate.WaitForClose());

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, ServerPing) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> read_ping(spdy_util_.ConstructSpdyPing(2, false));
  MockRead reads[] = {
    CreateMockRead(*read_ping),
    MockRead(SYNCHRONOUS, 0, 0)  // EOF
  };
  scoped_ptr<SpdyFrame> write_ping(spdy_util_.ConstructSpdyPing(2, true));
  MockWrite writes[] = {
    CreateMockWrite(*write_ping),
  };
  StaticSocketDataProvider data(
      reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  test::StreamDelegateSendImmediate delegate(spdy_stream1, nullptr);
  spdy_stream1->SetDelegate(&delegate);

  // Flush the read completion task.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_FALSE(session_);
  EXPECT_FALSE(spdy_stream1);
}

// Cause a ping to be sent out while producing a write. The write loop
// should handle this properly, i.e. another DoWriteLoop task should
// not be posted. This is a regression test for
// http://crbug.com/261043 .
TEST_P(SpdySessionTest, PingAndWriteLoop) {
  session_deps_.enable_ping = true;
  session_deps_.time_func = TheNearFuture;

  scoped_ptr<SpdyFrame> write_ping(spdy_util_.ConstructSpdyPing(1, false));
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
    CreateMockWrite(*write_ping, 1),
  };

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2), MockRead(ASYNC, 0, 3)  // EOF
  };

  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);

  // Shift time so that a ping will be sent out.
  g_time_delta = base::TimeDelta::FromSeconds(11);

  base::RunLoop().RunUntilIdle();
  session_->CloseSessionOnError(ERR_ABORTED, "Aborting");

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, StreamIdSpaceExhausted) {
  const SpdyStreamId kLastStreamId = 0x7fffffff;
  session_deps_.host_resolver->set_synchronous_mode(true);

  // Test setup: |stream_hi_water_mark_| and |max_concurrent_streams_| are
  // fixed to allow for two stream ID assignments, and three concurrent
  // streams. Four streams are started, and two are activated. Verify the
  // session goes away, and that the created (but not activated) and
  // stalled streams are aborted. Also verify the activated streams complete,
  // at which point the session closes.

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, kLastStreamId - 2, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, kLastStreamId, MEDIUM, true));

  MockWrite writes[] = {
      CreateMockWrite(*req1, 0), CreateMockWrite(*req2, 1),
  };

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, kLastStreamId - 2));
  scoped_ptr<SpdyFrame> resp2(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, kLastStreamId));

  scoped_ptr<SpdyFrame> body1(
      spdy_util_.ConstructSpdyBodyFrame(kLastStreamId - 2, true));
  scoped_ptr<SpdyFrame> body2(
      spdy_util_.ConstructSpdyBodyFrame(kLastStreamId, true));

  MockRead reads[] = {
      CreateMockRead(*resp1, 2),
      CreateMockRead(*resp2, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      CreateMockRead(*body1, 5),
      CreateMockRead(*body2, 6),
      MockRead(ASYNC, 0, 7)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Fix stream_hi_water_mark_ to allow for two stream activations.
  session_->stream_hi_water_mark_ = kLastStreamId - 2;
  // Fix max_concurrent_streams to allow for three stream creations.
  session_->max_concurrent_streams_ = 3;

  // Create three streams synchronously, and begin a fourth (which is stalled).
  base::WeakPtr<SpdyStream> stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(stream1);
  stream1->SetDelegate(&delegate1);

  base::WeakPtr<SpdyStream> stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate2(stream2);
  stream2->SetDelegate(&delegate2);

  base::WeakPtr<SpdyStream> stream3 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate3(stream3);
  stream3->SetDelegate(&delegate3);

  SpdyStreamRequest request4;
  TestCompletionCallback callback4;
  EXPECT_EQ(
      ERR_IO_PENDING,
      request4.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_,
                            MEDIUM, BoundNetLog(), callback4.callback()));

  // Streams 1-3 were created. 4th is stalled. No streams are active yet.
  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(3u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(MEDIUM));

  // Activate stream 1. One ID remains available.
  stream1->SendRequestHeaders(
      scoped_ptr<SpdyHeaderBlock>(
          spdy_util_.ConstructGetHeaderBlock(kDefaultURL)),
      NO_MORE_DATA_TO_SEND);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(kLastStreamId - 2u, stream1->stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(2u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(MEDIUM));

  // Activate stream 2. ID space is exhausted.
  stream2->SendRequestHeaders(
      scoped_ptr<SpdyHeaderBlock>(
          spdy_util_.ConstructGetHeaderBlock(kDefaultURL)),
      NO_MORE_DATA_TO_SEND);
  base::RunLoop().RunUntilIdle();

  // Active streams remain active.
  EXPECT_EQ(kLastStreamId, stream2->stream_id());
  EXPECT_EQ(2u, session_->num_active_streams());

  // Session is going away. Created and stalled streams were aborted.
  EXPECT_EQ(SpdySession::STATE_GOING_AWAY, session_->availability_state_);
  EXPECT_EQ(ERR_ABORTED, delegate3.WaitForClose());
  EXPECT_EQ(ERR_ABORTED, callback4.WaitForResult());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(MEDIUM));

  // Read responses on remaining active streams.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(OK, delegate1.WaitForClose());
  EXPECT_EQ(kUploadData, delegate1.TakeReceivedData());
  EXPECT_EQ(OK, delegate2.WaitForClose());
  EXPECT_EQ(kUploadData, delegate2.TakeReceivedData());

  // Session was destroyed.
  EXPECT_FALSE(session_);
}

// Regression test for https://crbug.com/481009.
TEST_P(SpdySessionTest, MaxConcurrentStreamsZero) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  int seq = 0;
  std::vector<MockRead> reads;

  // Receive SETTINGS frame that sets max_concurrent_streams to zero.
  SettingsMap settings_zero;
  settings_zero[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, 0);
  scoped_ptr<SpdyFrame> settings_frame_zero(
      spdy_util_.ConstructSpdySettings(settings_zero));
  reads.push_back(CreateMockRead(*settings_frame_zero, seq++));

  // Acknowledge it.
  std::vector<MockWrite> writes;
  scoped_ptr<SpdyFrame> settings_ack0;
  if (GetProtocol() == kProtoHTTP2) {
    settings_ack0.reset(spdy_util_.ConstructSpdySettingsAck());
    writes.push_back(CreateMockWrite(*settings_ack0, seq++));
  }

  // Pause.
  reads.push_back(MockRead(ASYNC, ERR_IO_PENDING, seq++));

  // Receive SETTINGS frame that sets max_concurrent_streams to one.
  SettingsMap settings_one;
  settings_one[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, 1);
  scoped_ptr<SpdyFrame> settings_frame_one(
      spdy_util_.ConstructSpdySettings(settings_one));
  reads.push_back(CreateMockRead(*settings_frame_one, seq++));

  // Acknowledge it.
  scoped_ptr<SpdyFrame> settings_ack1;
  if (GetProtocol() == kProtoHTTP2) {
    settings_ack1.reset(spdy_util_.ConstructSpdySettingsAck());
    writes.push_back(CreateMockWrite(*settings_ack1, seq++));
  }

  // Request and response.
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  writes.push_back(CreateMockWrite(*req, seq++));

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  reads.push_back(CreateMockRead(*resp, seq++));

  scoped_ptr<SpdyFrame> body(spdy_util_.ConstructSpdyBodyFrame(1, true));
  reads.push_back(CreateMockRead(*body, seq++));

  reads.push_back(MockRead(ASYNC, 0, seq++));

  SequencedSocketData data(reads.data(), reads.size(), writes.data(),
                           writes.size());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Create session.
  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Receive SETTINGS frame that sets max_concurrent_streams to zero.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, session_->max_concurrent_streams_);

  // Start request.
  SpdyStreamRequest request;
  TestCompletionCallback callback;
  int rv =
      request.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_,
                           MEDIUM, BoundNetLog(), callback.callback());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  // Stream is stalled.
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(MEDIUM));
  EXPECT_EQ(0u, session_->num_created_streams());

  // Receive SETTINGS frame that sets max_concurrent_streams to one.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, session_->max_concurrent_streams_);

  // Stream is created.
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(MEDIUM));
  EXPECT_EQ(1u, session_->num_created_streams());

  EXPECT_EQ(OK, callback.WaitForResult());

  // Send request.
  base::WeakPtr<SpdyStream> stream = request.ReleaseStream();
  test::StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);
  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(stream->HasUrlFromHeaders());

  EXPECT_EQ(OK, delegate.WaitForClose());
  EXPECT_EQ("hello!", delegate.TakeReceivedData());

  // Session is destroyed.
  EXPECT_FALSE(session_);
}

// Verifies that an unstalled pending stream creation racing with a new stream
// creation doesn't violate the maximum stream concurrency. Regression test for
// crbug.com/373858.
TEST_P(SpdySessionTest, UnstallRacesWithStreamCreation) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
      MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Fix max_concurrent_streams to allow for one open stream.
  session_->max_concurrent_streams_ = 1;

  // Create two streams: one synchronously, and one which stalls.
  base::WeakPtr<SpdyStream> stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());

  SpdyStreamRequest request2;
  TestCompletionCallback callback2;
  EXPECT_EQ(
      ERR_IO_PENDING,
      request2.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_,
                            MEDIUM, BoundNetLog(), callback2.callback()));

  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(MEDIUM));

  // Cancel the first stream. A callback to unstall the second stream was
  // posted. Don't run it yet.
  stream1->Cancel();

  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(MEDIUM));

  // Create a third stream prior to the second stream's callback.
  base::WeakPtr<SpdyStream> stream3 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());

  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(MEDIUM));

  // Now run the message loop. The unstalled stream will re-stall itself.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(MEDIUM));

  // Cancel the third stream and run the message loop. Verify that the second
  // stream creation now completes.
  stream3->Cancel();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(MEDIUM));
  EXPECT_EQ(OK, callback2.WaitForResult());
}

TEST_P(SpdySessionTest, DeleteExpiredPushStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = TheNearFuture;

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(2, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {CreateMockWrite(*req, 0), CreateMockWrite(*rst, 5)};

  scoped_ptr<SpdyFrame> push_a(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, "http://www.example.org/a.dat"));
  scoped_ptr<SpdyFrame> push_a_body(
      spdy_util_.ConstructSpdyBodyFrame(2, false));
  // In ascii "0" < "a". We use it to verify that we properly handle std::map
  // iterators inside. See http://crbug.com/443490
  scoped_ptr<SpdyFrame> push_b(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 4, 1, "http://www.example.org/0.dat"));
  MockRead reads[] = {
      CreateMockRead(*push_a, 1),
      CreateMockRead(*push_a_body, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*push_b, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 6),
      MockRead(ASYNC, 0, 7)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Process the principal request, and the first push stream request & body.
  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);

  base::RunLoop().RunUntilIdle();

  // Verify that there is one unclaimed push stream.
  EXPECT_EQ(1u, session_->num_unclaimed_pushed_streams());
  EXPECT_EQ(1u, session_->count_unclaimed_pushed_streams_for_url(
                    GURL("http://www.example.org/a.dat")));

  // Unclaimed push body consumed bytes from the session window.
  EXPECT_EQ(
      SpdySession::GetDefaultInitialWindowSize(GetProtocol()) - kUploadDataSize,
      session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  // Shift time to expire the push stream. Read the second SYN_STREAM,
  // and verify a RST_STREAM was written.
  g_time_delta = base::TimeDelta::FromSeconds(301);
  data.Resume();
  base::RunLoop().RunUntilIdle();

  // Verify that the second pushed stream evicted the first pushed stream.
  EXPECT_EQ(1u, session_->num_unclaimed_pushed_streams());
  EXPECT_EQ(1u, session_->count_unclaimed_pushed_streams_for_url(
                    GURL("http://www.example.org/0.dat")));

  // Verify that the session window reclaimed the evicted stream body.
  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(kUploadDataSize, session_->session_unacked_recv_window_bytes_);

  // Read and process EOF.
  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, FailedPing) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
      MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };
  scoped_ptr<SpdyFrame> write_ping(spdy_util_.ConstructSpdyPing(1, false));
  scoped_ptr<SpdyFrame> goaway(
      spdy_util_.ConstructSpdyGoAway(0, GOAWAY_PROTOCOL_ERROR, "Failed ping."));
  MockWrite writes[] = {CreateMockWrite(*write_ping), CreateMockWrite(*goaway)};

  StaticSocketDataProvider data(
      reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  test::StreamDelegateSendImmediate delegate(spdy_stream1, nullptr);
  spdy_stream1->SetDelegate(&delegate);

  session_->set_connection_at_risk_of_loss_time(
      base::TimeDelta::FromSeconds(0));
  session_->set_hung_interval(base::TimeDelta::FromSeconds(0));

  // Send a PING frame.
  session_->WritePingFrame(1, false);
  EXPECT_LT(0, session_->pings_in_flight());
  EXPECT_GE(session_->next_ping_id(), 1U);
  EXPECT_TRUE(session_->check_ping_status_pending());

  // Assert session is not closed.
  EXPECT_TRUE(session_->IsAvailable());
  EXPECT_LT(0u,
            session_->num_active_streams() + session_->num_created_streams());
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // We set last time we have received any data in 1 sec less than now.
  // CheckPingStatus will trigger timeout because hung interval is zero.
  base::TimeTicks now = base::TimeTicks::Now();
  session_->last_activity_time_ = now - base::TimeDelta::FromSeconds(1);
  session_->CheckPingStatus(now);
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(session_);
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(spdy_stream1);
}

// Request kInitialMaxConcurrentStreams + 1 streams.  Receive a
// settings frame increasing the max concurrent streams by 1.  Make
// sure nothing blows up. This is a regression test for
// http://crbug.com/57331 .
TEST_P(SpdySessionTest, OnSettings) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  const SpdySettingsIds kSpdySettingsIds = SETTINGS_MAX_CONCURRENT_STREAMS;

  int seq = 0;
  std::vector<MockWrite> writes;
  scoped_ptr<SpdyFrame> settings_ack(spdy_util_.ConstructSpdySettingsAck());
  if (GetProtocol() == kProtoHTTP2) {
    writes.push_back(CreateMockWrite(*settings_ack, ++seq));
  }

  SettingsMap new_settings;
  const uint32_t max_concurrent_streams = kInitialMaxConcurrentStreams + 1;
  new_settings[kSpdySettingsIds] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, max_concurrent_streams);
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(new_settings));
  MockRead reads[] = {
      CreateMockRead(*settings_frame, 0),
      MockRead(ASYNC, ERR_IO_PENDING, ++seq),
      MockRead(ASYNC, 0, ++seq),
  };

  SequencedSocketData data(reads, arraysize(reads), writes.data(),
                           writes.size());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Create the maximum number of concurrent streams.
  for (size_t i = 0; i < kInitialMaxConcurrentStreams; ++i) {
    base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
        SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
    ASSERT_TRUE(spdy_stream != nullptr);
  }

  StreamReleaserCallback stream_releaser;
  SpdyStreamRequest request;
  ASSERT_EQ(ERR_IO_PENDING,
            request.StartRequest(SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                 MEDIUM, BoundNetLog(),
                                 stream_releaser.MakeCallback(&request)));

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(OK, stream_releaser.WaitForResult());

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Start with a persisted value for max concurrent streams. Receive a
// settings frame increasing the max concurrent streams by 1 and which
// also clears the persisted data. Verify that persisted data is
// correct.
TEST_P(SpdySessionTest, ClearSettings) {
  if (spdy_util_.spdy_version() >= HTTP2) {
    // HTTP/2 doesn't include settings persistence, or a CLEAR_SETTINGS flag.
    // Flag 0x1, CLEAR_SETTINGS in SPDY3, is instead settings ACK in HTTP/2.
    return;
  }
  session_deps_.host_resolver->set_synchronous_mode(true);

  SettingsMap new_settings;
  const uint32_t max_concurrent_streams = kInitialMaxConcurrentStreams + 1;
  new_settings[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, max_concurrent_streams);
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(new_settings));
  uint8_t flags = SETTINGS_FLAG_CLEAR_PREVIOUSLY_PERSISTED_SETTINGS;
  test::SetFrameFlags(settings_frame.get(), flags, spdy_util_.spdy_version());
  MockRead reads[] = {
      CreateMockRead(*settings_frame, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 0, 2),
  };

  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();

  // Initialize the SpdySetting with the default.
  spdy_session_pool_->http_server_properties()->SetSpdySetting(
      test_host_port_pair_,
      SETTINGS_MAX_CONCURRENT_STREAMS,
      SETTINGS_FLAG_PLEASE_PERSIST,
      kInitialMaxConcurrentStreams);

  EXPECT_FALSE(
      spdy_session_pool_->http_server_properties()->GetSpdySettings(
          test_host_port_pair_).empty());

  CreateInsecureSpdySession();

  // Create the maximum number of concurrent streams.
  for (size_t i = 0; i < kInitialMaxConcurrentStreams; ++i) {
    base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
        SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
    ASSERT_TRUE(spdy_stream != nullptr);
  }

  StreamReleaserCallback stream_releaser;

  SpdyStreamRequest request;
  ASSERT_EQ(ERR_IO_PENDING,
            request.StartRequest(SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                 MEDIUM, BoundNetLog(),
                                 stream_releaser.MakeCallback(&request)));

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(OK, stream_releaser.WaitForResult());

  // Make sure that persisted data is cleared.
  EXPECT_TRUE(
      spdy_session_pool_->http_server_properties()->GetSpdySettings(
          test_host_port_pair_).empty());

  // Make sure session's max_concurrent_streams is correct.
  EXPECT_EQ(kInitialMaxConcurrentStreams + 1,
            session_->max_concurrent_streams_);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Start with max concurrent streams set to 1.  Request two streams.
// When the first completes, have the callback close its stream, which
// should trigger the second stream creation.  Then cancel that one
// immediately.  Don't crash.  This is a regression test for
// http://crbug.com/63532 .
TEST_P(SpdySessionTest, CancelPendingCreateStream) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();

  // Initialize the SpdySetting with 1 max concurrent streams.
  spdy_session_pool_->http_server_properties()->SetSpdySetting(
      test_host_port_pair_,
      SETTINGS_MAX_CONCURRENT_STREAMS,
      SETTINGS_FLAG_PLEASE_PERSIST,
      1);

  CreateInsecureSpdySession();

  // Leave room for only one more stream to be created.
  for (size_t i = 0; i < kInitialMaxConcurrentStreams - 1; ++i) {
    base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
        SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
    ASSERT_TRUE(spdy_stream != nullptr);
  }

  // Create 2 more streams.  First will succeed.  Second will be pending.
  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);

  // Use scoped_ptr to let us invalidate the memory when we want to, to trigger
  // a valgrind error if the callback is invoked when it's not supposed to be.
  scoped_ptr<TestCompletionCallback> callback(new TestCompletionCallback);

  SpdyStreamRequest request;
  ASSERT_EQ(ERR_IO_PENDING,
            request.StartRequest(SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                 MEDIUM, BoundNetLog(), callback->callback()));

  // Release the first one, this will allow the second to be created.
  spdy_stream1->Cancel();
  EXPECT_FALSE(spdy_stream1);

  request.CancelRequest();
  callback.reset();

  // Should not crash when running the pending callback.
  base::RunLoop().RunUntilIdle();
}

TEST_P(SpdySessionTest, SendInitialDataOnNewSession) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };

  SettingsMap settings;
  settings[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, kMaxConcurrentPushedStreams);
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(settings));
  std::vector<MockWrite> writes;
  if (GetProtocol() == kProtoHTTP2) {
    writes.push_back(
        MockWrite(ASYNC,
                  kHttp2ConnectionHeaderPrefix,
                  kHttp2ConnectionHeaderPrefixSize));
  }
  writes.push_back(CreateMockWrite(*settings_frame));

  SettingsMap server_settings;
  const uint32_t initial_max_concurrent_streams = 1;
  server_settings[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_PERSISTED,
                            initial_max_concurrent_streams);
  scoped_ptr<SpdyFrame> server_settings_frame(
      spdy_util_.ConstructSpdySettings(server_settings));
  if (GetProtocol() == kProtoSPDY31) {
    writes.push_back(CreateMockWrite(*server_settings_frame));
  }

  StaticSocketDataProvider data(reads, arraysize(reads), writes.data(),
                                writes.size());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();

  spdy_session_pool_->http_server_properties()->SetSpdySetting(
      test_host_port_pair_,
      SETTINGS_MAX_CONCURRENT_STREAMS,
      SETTINGS_FLAG_PLEASE_PERSIST,
      initial_max_concurrent_streams);

  SpdySessionPoolPeer pool_peer(spdy_session_pool_);
  pool_peer.SetEnableSendingInitialData(true);

  CreateInsecureSpdySession();

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_P(SpdySessionTest, ClearSettingsStorageOnIPAddressChanged) {
  CreateNetworkSession();

  base::WeakPtr<HttpServerProperties> test_http_server_properties =
      spdy_session_pool_->http_server_properties();
  SettingsFlagsAndValue flags_and_value1(SETTINGS_FLAG_PLEASE_PERSIST, 2);
  test_http_server_properties->SetSpdySetting(
      test_host_port_pair_,
      SETTINGS_MAX_CONCURRENT_STREAMS,
      SETTINGS_FLAG_PLEASE_PERSIST,
      2);
  EXPECT_NE(0u, test_http_server_properties->GetSpdySettings(
      test_host_port_pair_).size());
  spdy_session_pool_->OnIPAddressChanged();
  EXPECT_EQ(0u, test_http_server_properties->GetSpdySettings(
      test_host_port_pair_).size());
}

TEST_P(SpdySessionTest, Initialize) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(ASYNC, 0, 0)  // EOF
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Flush the read completion task.
  base::RunLoop().RunUntilIdle();

  TestNetLogEntry::List entries;
  log_.GetEntries(&entries);
  EXPECT_LT(0u, entries.size());

  // Check that we logged TYPE_HTTP2_SESSION_INITIALIZED correctly.
  int pos = ExpectLogContainsSomewhere(
      entries, 0, NetLog::TYPE_HTTP2_SESSION_INITIALIZED, NetLog::PHASE_NONE);
  EXPECT_LT(0, pos);

  TestNetLogEntry entry = entries[pos];
  NetLog::Source socket_source;
  EXPECT_TRUE(NetLog::Source::FromEventParameters(entry.params.get(),
                                                  &socket_source));
  EXPECT_TRUE(socket_source.IsValid());
  EXPECT_NE(log_.bound().source().id, socket_source.id);
}

TEST_P(SpdySessionTest, NetLogOnSessionGoaway) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(
      spdy_util_.ConstructSpdyGoAway(42, GOAWAY_ENHANCE_YOUR_CALM, "foo"));
  MockRead reads[] = {
    CreateMockRead(*goaway),
    MockRead(SYNCHRONOUS, 0, 0)  // EOF
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Flush the read completion task.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(session_);

  // Check that the NetLog was filled reasonably.
  TestNetLogEntry::List entries;
  log_.GetEntries(&entries);
  EXPECT_LT(0u, entries.size());

  if (GetProtocol() == kProtoHTTP2) {
    int pos = ExpectLogContainsSomewhere(
        entries, 0, NetLog::TYPE_HTTP2_SESSION_GOAWAY, NetLog::PHASE_NONE);
    TestNetLogEntry entry = entries[pos];
    int last_accepted_stream_id;
    ASSERT_TRUE(entry.GetIntegerValue("last_accepted_stream_id",
                                      &last_accepted_stream_id));
    EXPECT_EQ(42, last_accepted_stream_id);
    int active_streams;
    ASSERT_TRUE(entry.GetIntegerValue("active_streams", &active_streams));
    EXPECT_EQ(0, active_streams);
    int unclaimed_streams;
    ASSERT_TRUE(entry.GetIntegerValue("unclaimed_streams", &unclaimed_streams));
    EXPECT_EQ(0, unclaimed_streams);
    int status;
    ASSERT_TRUE(entry.GetIntegerValue("status", &status));
    EXPECT_EQ(GOAWAY_ENHANCE_YOUR_CALM, status);
    std::string debug_data;
    ASSERT_TRUE(entry.GetStringValue("debug_data", &debug_data));
    EXPECT_EQ("foo", debug_data);
  }

  // Check that we logged SPDY_SESSION_CLOSE correctly.
  int pos = ExpectLogContainsSomewhere(
      entries, 0, NetLog::TYPE_HTTP2_SESSION_CLOSE, NetLog::PHASE_NONE);
  TestNetLogEntry entry = entries[pos];
  int error_code = 0;
  ASSERT_TRUE(entry.GetNetErrorCode(&error_code));
  EXPECT_EQ(OK, error_code);
}

TEST_P(SpdySessionTest, NetLogOnSessionEOF) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
      MockRead(SYNCHRONOUS, 0, 0)  // EOF
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Flush the read completion task.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));
  EXPECT_FALSE(session_);

  // Check that the NetLog was filled reasonably.
  TestNetLogEntry::List entries;
  log_.GetEntries(&entries);
  EXPECT_LT(0u, entries.size());

  // Check that we logged SPDY_SESSION_CLOSE correctly.
  int pos = ExpectLogContainsSomewhere(
      entries, 0, NetLog::TYPE_HTTP2_SESSION_CLOSE, NetLog::PHASE_NONE);

  if (pos < static_cast<int>(entries.size())) {
    TestNetLogEntry entry = entries[pos];
    int error_code = 0;
    ASSERT_TRUE(entry.GetNetErrorCode(&error_code));
    EXPECT_EQ(ERR_CONNECTION_CLOSED, error_code);
  } else {
    ADD_FAILURE();
  }
}

TEST_P(SpdySessionTest, SynCompressionHistograms) {
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
  };
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), MockRead(ASYNC, 0, 2)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  // Write request headers & capture resulting histogram update.
  base::HistogramTester histogram_tester;

  base::RunLoop().RunUntilIdle();
  // Regression test of compression performance under the request fixture.
  switch (spdy_util_.spdy_version()) {
    case SPDY3:
      histogram_tester.ExpectBucketCount(
          "Net.SpdySynStreamCompressionPercentage", 30, 1);
      break;
    case HTTP2:
      histogram_tester.ExpectBucketCount(
          "Net.SpdySynStreamCompressionPercentage", 81, 1);
      break;
    default:
      NOTREACHED();
  }

  // Read and process EOF.
  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Queue up a low-priority SYN_STREAM followed by a high-priority
// one. The high priority one should still send first and receive
// first.
TEST_P(SpdySessionTest, OutOfOrderSynStreams) {
  // Construct the request.
  scoped_ptr<SpdyFrame> req_highest(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, HIGHEST, true));
  scoped_ptr<SpdyFrame> req_lowest(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, LOWEST, true));
  MockWrite writes[] = {
    CreateMockWrite(*req_highest, 0),
    CreateMockWrite(*req_lowest, 1),
  };

  scoped_ptr<SpdyFrame> resp_highest(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> body_highest(
      spdy_util_.ConstructSpdyBodyFrame(1, true));
  scoped_ptr<SpdyFrame> resp_lowest(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 3));
  scoped_ptr<SpdyFrame> body_lowest(
      spdy_util_.ConstructSpdyBodyFrame(3, true));
  MockRead reads[] = {
    CreateMockRead(*resp_highest, 2),
    CreateMockRead(*body_highest, 3),
    CreateMockRead(*resp_lowest, 4),
    CreateMockRead(*body_lowest, 5),
    MockRead(ASYNC, 0, 6)  // EOF
  };

  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream_lowest = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream_lowest);
  EXPECT_EQ(0u, spdy_stream_lowest->stream_id());
  test::StreamDelegateDoNothing delegate_lowest(spdy_stream_lowest);
  spdy_stream_lowest->SetDelegate(&delegate_lowest);

  base::WeakPtr<SpdyStream> spdy_stream_highest =
      CreateStreamSynchronously(SPDY_REQUEST_RESPONSE_STREAM, session_,
                                test_url_, HIGHEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream_highest);
  EXPECT_EQ(0u, spdy_stream_highest->stream_id());
  test::StreamDelegateDoNothing delegate_highest(spdy_stream_highest);
  spdy_stream_highest->SetDelegate(&delegate_highest);

  // Queue the lower priority one first.

  scoped_ptr<SpdyHeaderBlock> headers_lowest(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream_lowest->SendRequestHeaders(std::move(headers_lowest),
                                         NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream_lowest->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers_highest(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream_highest->SendRequestHeaders(std::move(headers_highest),
                                          NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream_highest->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(spdy_stream_lowest);
  EXPECT_FALSE(spdy_stream_highest);
  EXPECT_EQ(3u, delegate_lowest.stream_id());
  EXPECT_EQ(1u, delegate_highest.stream_id());
}

TEST_P(SpdySessionTest, CancelStream) {
  // Request 1, at HIGHEST priority, will be cancelled before it writes data.
  // Request 2, at LOWEST priority, will be a full request and will be id 1.
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  MockWrite writes[] = {
    CreateMockWrite(*req2, 0),
  };

  scoped_ptr<SpdyFrame> resp2(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> body2(spdy_util_.ConstructSpdyBodyFrame(1, true));
  MockRead reads[] = {
      CreateMockRead(*resp2, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*body2, 3),
      MockRead(ASYNC, 0, 4)  // EOF
  };

  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 =
      CreateStreamSynchronously(SPDY_REQUEST_RESPONSE_STREAM, session_,
                                test_url_, HIGHEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream2.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream2->stream_id());
  test::StreamDelegateDoNothing delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  EXPECT_EQ(0u, spdy_stream1->stream_id());

  spdy_stream1->Cancel();
  EXPECT_FALSE(spdy_stream1);

  EXPECT_EQ(0u, delegate1.stream_id());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0u, delegate1.stream_id());
  EXPECT_EQ(1u, delegate2.stream_id());

  spdy_stream2->Cancel();
  EXPECT_FALSE(spdy_stream2);
}

// Create two streams that are set to re-close themselves on close,
// and then close the session. Nothing should blow up. Also a
// regression test for http://crbug.com/139518 .
TEST_P(SpdySessionTest, CloseSessionWithTwoCreatedSelfClosingStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);


  // No actual data will be sent.
  MockWrite writes[] = {
    MockWrite(ASYNC, 0, 1)  // EOF
  };

  MockRead reads[] = {
    MockRead(ASYNC, 0, 0)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, HIGHEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream2.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  test::ClosingDelegate delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  test::ClosingDelegate delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  // Ensure that the streams have not yet been activated and assigned an id.
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  // Ensure we don't crash while closing the session.
  session_->CloseSessionOnError(ERR_ABORTED, std::string());

  EXPECT_FALSE(spdy_stream1);
  EXPECT_FALSE(spdy_stream2);

  EXPECT_TRUE(delegate1.StreamIsClosed());
  EXPECT_TRUE(delegate2.StreamIsClosed());

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Create two streams that are set to close each other on close, and
// then close the session. Nothing should blow up.
TEST_P(SpdySessionTest, CloseSessionWithTwoCreatedMutuallyClosingStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(nullptr, 0, nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, HIGHEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream2);
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  // Make |spdy_stream1| close |spdy_stream2|.
  test::ClosingDelegate delegate1(spdy_stream2);
  spdy_stream1->SetDelegate(&delegate1);

  // Make |spdy_stream2| close |spdy_stream1|.
  test::ClosingDelegate delegate2(spdy_stream1);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  // Ensure that the streams have not yet been activated and assigned an id.
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  // Ensure we don't crash while closing the session.
  session_->CloseSessionOnError(ERR_ABORTED, std::string());

  EXPECT_FALSE(spdy_stream1);
  EXPECT_FALSE(spdy_stream2);

  EXPECT_TRUE(delegate1.StreamIsClosed());
  EXPECT_TRUE(delegate2.StreamIsClosed());

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Create two streams that are set to re-close themselves on close,
// activate them, and then close the session. Nothing should blow up.
TEST_P(SpdySessionTest, CloseSessionWithTwoActivatedSelfClosingStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2), MockRead(ASYNC, 0, 3)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream2.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  test::ClosingDelegate delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  test::ClosingDelegate delegate2(spdy_stream2);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  // Ensure that the streams have not yet been activated and assigned an id.
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_EQ(3u, spdy_stream2->stream_id());

  // Ensure we don't crash while closing the session.
  session_->CloseSessionOnError(ERR_ABORTED, std::string());

  EXPECT_FALSE(spdy_stream1);
  EXPECT_FALSE(spdy_stream2);

  EXPECT_TRUE(delegate1.StreamIsClosed());
  EXPECT_TRUE(delegate2.StreamIsClosed());

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Create two streams that are set to close each other on close,
// activate them, and then close the session. Nothing should blow up.
TEST_P(SpdySessionTest, CloseSessionWithTwoActivatedMutuallyClosingStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2), MockRead(ASYNC, 0, 3)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream2);
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  // Make |spdy_stream1| close |spdy_stream2|.
  test::ClosingDelegate delegate1(spdy_stream2);
  spdy_stream1->SetDelegate(&delegate1);

  // Make |spdy_stream2| close |spdy_stream1|.
  test::ClosingDelegate delegate2(spdy_stream1);
  spdy_stream2->SetDelegate(&delegate2);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream2->HasUrlFromHeaders());

  // Ensure that the streams have not yet been activated and assigned an id.
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  EXPECT_EQ(0u, spdy_stream2->stream_id());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream1->stream_id());
  EXPECT_EQ(3u, spdy_stream2->stream_id());

  // Ensure we don't crash while closing the session.
  session_->CloseSessionOnError(ERR_ABORTED, std::string());

  EXPECT_FALSE(spdy_stream1);
  EXPECT_FALSE(spdy_stream2);

  EXPECT_TRUE(delegate1.StreamIsClosed());
  EXPECT_TRUE(delegate2.StreamIsClosed());

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Delegate that closes a given session when the stream is closed.
class SessionClosingDelegate : public test::StreamDelegateDoNothing {
 public:
  SessionClosingDelegate(const base::WeakPtr<SpdyStream>& stream,
                         const base::WeakPtr<SpdySession>& session_to_close)
      : StreamDelegateDoNothing(stream),
        session_to_close_(session_to_close) {}

  ~SessionClosingDelegate() override {}

  void OnClose(int status) override {
    session_to_close_->CloseSessionOnError(ERR_SPDY_PROTOCOL_ERROR, "Error");
  }

 private:
  base::WeakPtr<SpdySession> session_to_close_;
};

// Close an activated stream that closes its session. Nothing should
// blow up. This is a regression test for https://crbug.com/263691.
TEST_P(SpdySessionTest, CloseActivatedStreamThatClosesSession) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));
  scoped_ptr<SpdyFrame> goaway(
      spdy_util_.ConstructSpdyGoAway(0, GOAWAY_PROTOCOL_ERROR, "Error"));
  // The GOAWAY has higher-priority than the RST_STREAM, and is written first
  // despite being queued second.
  MockWrite writes[] = {
      CreateMockWrite(*req, 0),
      CreateMockWrite(*goaway, 1),
      CreateMockWrite(*rst, 3),
  };

  MockRead reads[] = {
      MockRead(ASYNC, 0, 2)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream->stream_id());

  SessionClosingDelegate delegate(spdy_stream, session_);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  EXPECT_EQ(0u, spdy_stream->stream_id());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream->stream_id());

  // Ensure we don't crash while closing the stream (which closes the
  // session).
  spdy_stream->Cancel();

  EXPECT_FALSE(spdy_stream);
  EXPECT_TRUE(delegate.StreamIsClosed());

  // Write the RST_STREAM & GOAWAY.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_P(SpdySessionTest, VerifyDomainAuthentication) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(nullptr, 0, nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "spdy_pooling.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), test_cert.get());

  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.cert = test_cert;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  CreateNetworkSession();

  session_ = CreateSecureSpdySession(http_session_.get(), key_, BoundNetLog());

  EXPECT_TRUE(session_->VerifyDomainAuthentication("www.example.org"));
  EXPECT_TRUE(session_->VerifyDomainAuthentication("mail.example.org"));
  EXPECT_TRUE(session_->VerifyDomainAuthentication("mail.example.com"));
  EXPECT_FALSE(session_->VerifyDomainAuthentication("mail.google.com"));
}

TEST_P(SpdySessionTest, ConnectionPooledWithTlsChannelId) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(nullptr, 0, nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "spdy_pooling.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), test_cert.get());

  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.channel_id_sent = true;
  ssl.cert = test_cert;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  CreateNetworkSession();

  session_ = CreateSecureSpdySession(http_session_.get(), key_, BoundNetLog());

  EXPECT_TRUE(session_->VerifyDomainAuthentication("www.example.org"));
  EXPECT_TRUE(session_->VerifyDomainAuthentication("mail.example.org"));
  EXPECT_FALSE(session_->VerifyDomainAuthentication("mail.example.com"));
  EXPECT_FALSE(session_->VerifyDomainAuthentication("mail.google.com"));
}

TEST_P(SpdySessionTest, CloseTwoStalledCreateStream) {
  // TODO(rtenneti): Define a helper class/methods and move the common code in
  // this file.
  SettingsMap new_settings;
  const SpdySettingsIds kSpdySettingsIds1 = SETTINGS_MAX_CONCURRENT_STREAMS;
  const uint32_t max_concurrent_streams = 1;
  new_settings[kSpdySettingsIds1] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, max_concurrent_streams);

  scoped_ptr<SpdyFrame> settings_ack(spdy_util_.ConstructSpdySettingsAck());
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  spdy_util_.UpdateWithStreamDestruction(1);
  scoped_ptr<SpdyFrame> req2(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 3, LOWEST, true));
  spdy_util_.UpdateWithStreamDestruction(3);
  scoped_ptr<SpdyFrame> req3(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 5, LOWEST, true));
  MockWrite writes[] = {
    CreateMockWrite(*settings_ack, 1),
    CreateMockWrite(*req1, 2),
    CreateMockWrite(*req2, 5),
    CreateMockWrite(*req3, 8),
  };

  // Set up the socket so we read a SETTINGS frame that sets max concurrent
  // streams to 1.
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(new_settings));

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> body1(spdy_util_.ConstructSpdyBodyFrame(1, true));

  scoped_ptr<SpdyFrame> resp2(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 3));
  scoped_ptr<SpdyFrame> body2(spdy_util_.ConstructSpdyBodyFrame(3, true));

  scoped_ptr<SpdyFrame> resp3(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 5));
  scoped_ptr<SpdyFrame> body3(spdy_util_.ConstructSpdyBodyFrame(5, true));

  MockRead reads[] = {
      CreateMockRead(*settings_frame, 0),
      CreateMockRead(*resp1, 3),
      CreateMockRead(*body1, 4),
      CreateMockRead(*resp2, 6),
      CreateMockRead(*body2, 7),
      CreateMockRead(*resp3, 9),
      CreateMockRead(*body3, 10),
      MockRead(ASYNC, ERR_IO_PENDING, 11),
      MockRead(ASYNC, 0, 12)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Read the settings frame.
  base::RunLoop().RunUntilIdle();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  TestCompletionCallback callback2;
  SpdyStreamRequest request2;
  ASSERT_EQ(
      ERR_IO_PENDING,
      request2.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_,
                            LOWEST, BoundNetLog(), callback2.callback()));

  TestCompletionCallback callback3;
  SpdyStreamRequest request3;
  ASSERT_EQ(
      ERR_IO_PENDING,
      request3.StartRequest(SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_,
                            LOWEST, BoundNetLog(), callback3.callback()));

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(2u, session_->pending_create_stream_queue_size(LOWEST));

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st stream is activated and then closed.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream1);
  EXPECT_EQ(1u, delegate1.stream_id());

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(LOWEST));

  // Pump loop for SpdySession::ProcessPendingStreamRequests() to
  // create the 2nd stream.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(LOWEST));

  base::WeakPtr<SpdyStream> stream2 = request2.ReleaseStream();
  test::StreamDelegateDoNothing delegate2(stream2);
  stream2->SetDelegate(&delegate2);
  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  stream2->SendRequestHeaders(std::move(headers2), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(stream2->HasUrlFromHeaders());

  // Run until 2nd stream is activated and then closed.
  EXPECT_EQ(0u, delegate2.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stream2);
  EXPECT_EQ(3u, delegate2.stream_id());

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(LOWEST));

  // Pump loop for SpdySession::ProcessPendingStreamRequests() to
  // create the 3rd stream.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(LOWEST));

  base::WeakPtr<SpdyStream> stream3 = request3.ReleaseStream();
  test::StreamDelegateDoNothing delegate3(stream3);
  stream3->SetDelegate(&delegate3);
  scoped_ptr<SpdyHeaderBlock> headers3(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  stream3->SendRequestHeaders(std::move(headers3), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(stream3->HasUrlFromHeaders());

  // Run until 2nd stream is activated and then closed.
  EXPECT_EQ(0u, delegate3.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stream3);
  EXPECT_EQ(5u, delegate3.stream_id());

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(LOWEST));

  data.Resume();
  base::RunLoop().RunUntilIdle();
}

TEST_P(SpdySessionTest, CancelTwoStalledCreateStream) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Leave room for only one more stream to be created.
  for (size_t i = 0; i < kInitialMaxConcurrentStreams - 1; ++i) {
    base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
        SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
    ASSERT_TRUE(spdy_stream != nullptr);
  }

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  TestCompletionCallback callback2;
  SpdyStreamRequest request2;
  ASSERT_EQ(ERR_IO_PENDING, request2.StartRequest(
                                SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                LOWEST, BoundNetLog(), callback2.callback()));

  TestCompletionCallback callback3;
  SpdyStreamRequest request3;
  ASSERT_EQ(ERR_IO_PENDING, request3.StartRequest(
                                SPDY_BIDIRECTIONAL_STREAM, session_, test_url_,
                                LOWEST, BoundNetLog(), callback3.callback()));

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(kInitialMaxConcurrentStreams, session_->num_created_streams());
  EXPECT_EQ(2u, session_->pending_create_stream_queue_size(LOWEST));

  // Cancel the first stream; this will allow the second stream to be created.
  EXPECT_TRUE(spdy_stream1);
  spdy_stream1->Cancel();
  EXPECT_FALSE(spdy_stream1);

  EXPECT_EQ(OK, callback2.WaitForResult());
  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(kInitialMaxConcurrentStreams, session_->num_created_streams());
  EXPECT_EQ(1u, session_->pending_create_stream_queue_size(LOWEST));

  // Cancel the second stream; this will allow the third stream to be created.
  base::WeakPtr<SpdyStream> spdy_stream2 = request2.ReleaseStream();
  spdy_stream2->Cancel();
  EXPECT_FALSE(spdy_stream2);

  EXPECT_EQ(OK, callback3.WaitForResult());
  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(kInitialMaxConcurrentStreams, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(LOWEST));

  // Cancel the third stream.
  base::WeakPtr<SpdyStream> spdy_stream3 = request3.ReleaseStream();
  spdy_stream3->Cancel();
  EXPECT_FALSE(spdy_stream3);
  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(kInitialMaxConcurrentStreams - 1, session_->num_created_streams());
  EXPECT_EQ(0u, session_->pending_create_stream_queue_size(LOWEST));
}

// Test that SpdySession::DoReadLoop reads data from the socket
// without yielding.  This test makes 32k - 1 bytes of data available
// on the socket for reading. It then verifies that it has read all
// the available data without yielding.
TEST_P(SpdySessionTest, ReadDataWithoutYielding) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = InstantaneousReads;

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
  };

  // Build buffer of size kYieldAfterBytesRead / 4
  // (-spdy_data_frame_size).
  ASSERT_EQ(32 * 1024, kYieldAfterBytesRead);
  const int kPayloadSize =
      kYieldAfterBytesRead / 4 - framer.GetControlFrameHeaderSize();
  TestDataStream test_stream;
  scoped_refptr<IOBuffer> payload(new IOBuffer(kPayloadSize));
  char* payload_data = payload->data();
  test_stream.GetBytes(payload_data, kPayloadSize);

  scoped_ptr<SpdyFrame> partial_data_frame(
      framer.CreateDataFrame(1, payload_data, kPayloadSize, DATA_FLAG_NONE));
  scoped_ptr<SpdyFrame> finish_data_frame(
      framer.CreateDataFrame(1, payload_data, kPayloadSize - 1, DATA_FLAG_FIN));

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  // Write 1 byte less than kMaxReadBytes to check that DoRead reads up to 32k
  // bytes.
  MockRead reads[] = {
      CreateMockRead(*resp1, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*partial_data_frame, 3),
      CreateMockRead(*partial_data_frame, 4, SYNCHRONOUS),
      CreateMockRead(*partial_data_frame, 5, SYNCHRONOUS),
      CreateMockRead(*finish_data_frame, 6, SYNCHRONOUS),
      MockRead(ASYNC, 0, 7)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Set up the TaskObserver to verify SpdySession::DoReadLoop doesn't
  // post a task.
  SpdySessionTestTaskObserver observer("spdy_session.cc", "DoReadLoop");

  // Run until 1st read.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(0u, observer.executed_count());

  // Read all the data and verify SpdySession::DoReadLoop has not
  // posted a task.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream1);

  // Verify task observer's executed_count is zero, which indicates DoRead read
  // all the available data.
  EXPECT_EQ(0u, observer.executed_count());
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Test that SpdySession::DoReadLoop yields if more than
// |kYieldAfterDurationMilliseconds| has passed.  This test uses a mock time
// function that makes the response frame look very slow to read.
TEST_P(SpdySessionTest, TestYieldingSlowReads) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = SlowReads;

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
      CreateMockWrite(*req1, 0),
  };

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  MockRead reads[] = {
      CreateMockRead(*resp1, 1), MockRead(ASYNC, 0, 2)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Set up the TaskObserver to verify that SpdySession::DoReadLoop posts a
  // task.
  SpdySessionTestTaskObserver observer("spdy_session.cc", "DoReadLoop");

  EXPECT_EQ(0u, delegate1.stream_id());
  EXPECT_EQ(0u, observer.executed_count());

  // Read all the data and verify that SpdySession::DoReadLoop has posted a
  // task.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_FALSE(spdy_stream1);

  // Verify task that the observer's executed_count is 1, which indicates DoRead
  // has posted only one task and thus yielded though there is data available
  // for it to read.
  EXPECT_EQ(1u, observer.executed_count());
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Regression test for https://crbug.com/531570.
// Test the case where DoRead() takes long but returns synchronously.
TEST_P(SpdySessionTest, TestYieldingSlowSynchronousReads) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = SlowReads;

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
      CreateMockWrite(*req1, 0),
  };

  scoped_ptr<SpdyFrame> partial_data_frame(
      framer.CreateDataFrame(1, "foo ", 4, DATA_FLAG_NONE));
  scoped_ptr<SpdyFrame> finish_data_frame(
      framer.CreateDataFrame(1, "bar", 3, DATA_FLAG_FIN));

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  MockRead reads[] = {
      CreateMockRead(*resp1, 1), MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*partial_data_frame, 3, ASYNC),
      CreateMockRead(*partial_data_frame, 4, SYNCHRONOUS),
      CreateMockRead(*partial_data_frame, 5, SYNCHRONOUS),
      CreateMockRead(*finish_data_frame, 6, SYNCHRONOUS),
      MockRead(ASYNC, 0, 7)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st read.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());

  // Read all the data and verify SpdySession::DoReadLoop has posted a task.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ("foo foo foo bar", delegate1.TakeReceivedData());
  EXPECT_FALSE(spdy_stream1);

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Test that SpdySession::DoReadLoop yields while reading the
// data. This test makes 32k + 1 bytes of data available on the socket
// for reading. It then verifies that DoRead has yielded even though
// there is data available for it to read (i.e, socket()->Read didn't
// return ERR_IO_PENDING during socket reads).
TEST_P(SpdySessionTest, TestYieldingDuringReadData) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = InstantaneousReads;

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
  };

  // Build buffer of size kYieldAfterBytesRead / 4
  // (-spdy_data_frame_size).
  ASSERT_EQ(32 * 1024, kYieldAfterBytesRead);
  const int kPayloadSize =
      kYieldAfterBytesRead / 4 - framer.GetControlFrameHeaderSize();
  TestDataStream test_stream;
  scoped_refptr<IOBuffer> payload(new IOBuffer(kPayloadSize));
  char* payload_data = payload->data();
  test_stream.GetBytes(payload_data, kPayloadSize);

  scoped_ptr<SpdyFrame> partial_data_frame(
      framer.CreateDataFrame(1, payload_data, kPayloadSize, DATA_FLAG_NONE));
  scoped_ptr<SpdyFrame> finish_data_frame(
      framer.CreateDataFrame(1, "h", 1, DATA_FLAG_FIN));

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  // Write 1 byte more than kMaxReadBytes to check that DoRead yields.
  MockRead reads[] = {
      CreateMockRead(*resp1, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*partial_data_frame, 3),
      CreateMockRead(*partial_data_frame, 4, SYNCHRONOUS),
      CreateMockRead(*partial_data_frame, 5, SYNCHRONOUS),
      CreateMockRead(*partial_data_frame, 6, SYNCHRONOUS),
      CreateMockRead(*finish_data_frame, 7, SYNCHRONOUS),
      MockRead(ASYNC, 0, 8)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Set up the TaskObserver to verify SpdySession::DoReadLoop posts a task.
  SpdySessionTestTaskObserver observer("spdy_session.cc", "DoReadLoop");

  // Run until 1st read.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(0u, observer.executed_count());

  // Read all the data and verify SpdySession::DoReadLoop has posted a task.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream1);

  // Verify task observer's executed_count is 1, which indicates DoRead has
  // posted only one task and thus yielded though there is data available for it
  // to read.
  EXPECT_EQ(1u, observer.executed_count());
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Test that SpdySession::DoReadLoop() tests interactions of yielding
// + async, by doing the following MockReads.
//
// MockRead of SYNCHRONOUS 8K, SYNCHRONOUS 8K, SYNCHRONOUS 8K, SYNCHRONOUS 2K
// ASYNC 8K, SYNCHRONOUS 8K, SYNCHRONOUS 8K, SYNCHRONOUS 8K, SYNCHRONOUS 2K.
//
// The above reads 26K synchronously. Since that is less that 32K, we
// will attempt to read again. However, that DoRead() will return
// ERR_IO_PENDING (because of async read), so DoReadLoop() will
// yield. When we come back, DoRead() will read the results from the
// async read, and rest of the data synchronously.
TEST_P(SpdySessionTest, TestYieldingDuringAsyncReadData) {
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.time_func = InstantaneousReads;

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
  };

  // Build buffer of size kYieldAfterBytesRead / 4
  // (-spdy_data_frame_size).
  ASSERT_EQ(32 * 1024, kYieldAfterBytesRead);
  TestDataStream test_stream;
  const int kEightKPayloadSize =
      kYieldAfterBytesRead / 4 - framer.GetControlFrameHeaderSize();
  scoped_refptr<IOBuffer> eightk_payload(new IOBuffer(kEightKPayloadSize));
  char* eightk_payload_data = eightk_payload->data();
  test_stream.GetBytes(eightk_payload_data, kEightKPayloadSize);

  // Build buffer of 2k size.
  TestDataStream test_stream2;
  const int kTwoKPayloadSize = kEightKPayloadSize - 6 * 1024;
  scoped_refptr<IOBuffer> twok_payload(new IOBuffer(kTwoKPayloadSize));
  char* twok_payload_data = twok_payload->data();
  test_stream2.GetBytes(twok_payload_data, kTwoKPayloadSize);

  scoped_ptr<SpdyFrame> eightk_data_frame(framer.CreateDataFrame(
      1, eightk_payload_data, kEightKPayloadSize, DATA_FLAG_NONE));
  scoped_ptr<SpdyFrame> twok_data_frame(framer.CreateDataFrame(
      1, twok_payload_data, kTwoKPayloadSize, DATA_FLAG_NONE));
  scoped_ptr<SpdyFrame> finish_data_frame(framer.CreateDataFrame(
      1, "h", 1, DATA_FLAG_FIN));

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  MockRead reads[] = {
      CreateMockRead(*resp1, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*eightk_data_frame, 3),
      CreateMockRead(*eightk_data_frame, 4, SYNCHRONOUS),
      CreateMockRead(*eightk_data_frame, 5, SYNCHRONOUS),
      CreateMockRead(*twok_data_frame, 6, SYNCHRONOUS),
      CreateMockRead(*eightk_data_frame, 7, ASYNC),
      CreateMockRead(*eightk_data_frame, 8, SYNCHRONOUS),
      CreateMockRead(*eightk_data_frame, 9, SYNCHRONOUS),
      CreateMockRead(*eightk_data_frame, 10, SYNCHRONOUS),
      CreateMockRead(*twok_data_frame, 11, SYNCHRONOUS),
      CreateMockRead(*finish_data_frame, 12, SYNCHRONOUS),
      MockRead(ASYNC, 0, 13)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Set up the TaskObserver to monitor SpdySession::DoReadLoop
  // posting of tasks.
  SpdySessionTestTaskObserver observer("spdy_session.cc", "DoReadLoop");

  // Run until 1st read.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(0u, observer.executed_count());

  // Read all the data and verify SpdySession::DoReadLoop has posted a
  // task.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream1);

  // Verify task observer's executed_count is 1, which indicates DoRead has
  // posted only one task and thus yielded though there is data available for
  // it to read.
  EXPECT_EQ(1u, observer.executed_count());
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Send a GoAway frame when SpdySession is in DoReadLoop. Make sure
// nothing blows up.
TEST_P(SpdySessionTest, GoAwayWhileInDoReadLoop) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
  };

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> body1(spdy_util_.ConstructSpdyBodyFrame(1, true));
  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway());

  MockRead reads[] = {
      CreateMockRead(*resp1, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*body1, 3),
      CreateMockRead(*goaway, 4),
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers1), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st read.
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, spdy_stream1->stream_id());

  // Run until GoAway.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream1);
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
  EXPECT_FALSE(session_);
}

// Within this framework, a SpdySession should be initialized with
// flow control disabled for protocol version 2, with flow control
// enabled only for streams for protocol version 3, and with flow
// control enabled for streams and sessions for higher versions.
TEST_P(SpdySessionTest, ProtocolNegotiation) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, 0, 0)  // EOF
  };
  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  session_ = CreateFakeSpdySession(spdy_session_pool_, key_);

  EXPECT_EQ(spdy_util_.spdy_version(),
            session_->buffered_spdy_framer_->protocol_version());
  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_send_window_size_);
  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);
}

// Tests the case of a non-SPDY request closing an idle SPDY session when no
// pointers to the idle session are currently held.
TEST_P(SpdySessionTest, CloseOneIdleConnection) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };
  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();

  TransportClientSocketPool* pool =
      http_session_->GetTransportSocketPool(
          HttpNetworkSession::NORMAL_SOCKET_POOL);

  // Create an idle SPDY session.
  CreateInsecureSpdySession();
  EXPECT_FALSE(pool->IsStalled());

  // Trying to create a new connection should cause the pool to be stalled, and
  // post a task asynchronously to try and close the session.
  TestCompletionCallback callback2;
  HostPortPair host_port2("2.com", 80);
  scoped_refptr<TransportSocketParams> params2(new TransportSocketParams(
      host_port2, false, OnHostResolutionCallback(),
      TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
  scoped_ptr<ClientSocketHandle> connection2(new ClientSocketHandle);
  EXPECT_EQ(ERR_IO_PENDING,
            connection2->Init(host_port2.ToString(), params2, DEFAULT_PRIORITY,
                              ClientSocketPool::RespectLimits::ENABLED,
                              callback2.callback(), pool, BoundNetLog()));
  EXPECT_TRUE(pool->IsStalled());

  // The socket pool should close the connection asynchronously and establish a
  // new connection.
  EXPECT_EQ(OK, callback2.WaitForResult());
  EXPECT_FALSE(pool->IsStalled());
  EXPECT_FALSE(session_);
}

// Tests the case of a non-SPDY request closing an idle SPDY session when no
// pointers to the idle session are currently held, in the case the SPDY session
// has an alias.
TEST_P(SpdySessionTest, CloseOneIdleConnectionWithAlias) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };
  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.host_resolver->rules()->AddIPLiteralRule(
      "1.com", "192.168.0.2", std::string());
  session_deps_.host_resolver->rules()->AddIPLiteralRule(
      "2.com", "192.168.0.2", std::string());
  // Not strictly needed.
  session_deps_.host_resolver->rules()->AddIPLiteralRule(
      "3.com", "192.168.0.3", std::string());

  CreateNetworkSession();

  TransportClientSocketPool* pool =
      http_session_->GetTransportSocketPool(
          HttpNetworkSession::NORMAL_SOCKET_POOL);

  // Create an idle SPDY session.
  SpdySessionKey key1(HostPortPair("1.com", 80), ProxyServer::Direct(),
                      PRIVACY_MODE_DISABLED);
  base::WeakPtr<SpdySession> session1 = ::net::CreateInsecureSpdySession(
      http_session_.get(), key1, BoundNetLog());
  EXPECT_FALSE(pool->IsStalled());

  // Set up an alias for the idle SPDY session, increasing its ref count to 2.
  SpdySessionKey key2(HostPortPair("2.com", 80), ProxyServer::Direct(),
                      PRIVACY_MODE_DISABLED);
  HostResolver::RequestInfo info(key2.host_port_pair());
  AddressList addresses;
  // Pre-populate the DNS cache, since a synchronous resolution is required in
  // order to create the alias.
  session_deps_.host_resolver->Resolve(info, DEFAULT_PRIORITY, &addresses,
                                       CompletionCallback(), nullptr,
                                       BoundNetLog());
  // Get a session for |key2|, which should return the session created earlier.
  base::WeakPtr<SpdySession> session2 =
      spdy_session_pool_->FindAvailableSession(key2, GURL(), BoundNetLog());
  ASSERT_EQ(session1.get(), session2.get());
  EXPECT_FALSE(pool->IsStalled());

  // Trying to create a new connection should cause the pool to be stalled, and
  // post a task asynchronously to try and close the session.
  TestCompletionCallback callback3;
  HostPortPair host_port3("3.com", 80);
  scoped_refptr<TransportSocketParams> params3(new TransportSocketParams(
      host_port3, false, OnHostResolutionCallback(),
      TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
  scoped_ptr<ClientSocketHandle> connection3(new ClientSocketHandle);
  EXPECT_EQ(ERR_IO_PENDING,
            connection3->Init(host_port3.ToString(), params3, DEFAULT_PRIORITY,
                              ClientSocketPool::RespectLimits::ENABLED,
                              callback3.callback(), pool, BoundNetLog()));
  EXPECT_TRUE(pool->IsStalled());

  // The socket pool should close the connection asynchronously and establish a
  // new connection.
  EXPECT_EQ(OK, callback3.WaitForResult());
  EXPECT_FALSE(pool->IsStalled());
  EXPECT_FALSE(session1);
  EXPECT_FALSE(session2);
}

// Tests that when a SPDY session becomes idle, it closes itself if there is
// a lower layer pool stalled on the per-pool socket limit.
TEST_P(SpdySessionTest, CloseSessionOnIdleWhenPoolStalled) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };
  scoped_ptr<SpdyFrame> req1(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> cancel1(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 1),
    CreateMockWrite(*cancel1, 1),
  };
  StaticSocketDataProvider data(reads, arraysize(reads),
                                writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  MockRead http_reads[] = {
    MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };
  StaticSocketDataProvider http_data(http_reads, arraysize(http_reads), nullptr,
                                     0);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);


  CreateNetworkSession();

  TransportClientSocketPool* pool =
      http_session_->GetTransportSocketPool(
          HttpNetworkSession::NORMAL_SOCKET_POOL);

  // Create a SPDY session.
  CreateInsecureSpdySession();
  EXPECT_FALSE(pool->IsStalled());

  // Create a stream using the session, and send a request.

  TestCompletionCallback callback1;
  base::WeakPtr<SpdyStream> spdy_stream1 =
      CreateStreamSynchronously(SPDY_REQUEST_RESPONSE_STREAM, session_,
                                test_url_, DEFAULT_PRIORITY, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  EXPECT_EQ(ERR_IO_PENDING, spdy_stream1->SendRequestHeaders(
                                std::move(headers1), NO_MORE_DATA_TO_SEND));
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  base::RunLoop().RunUntilIdle();

  // Trying to create a new connection should cause the pool to be stalled, and
  // post a task asynchronously to try and close the session.
  TestCompletionCallback callback2;
  HostPortPair host_port2("2.com", 80);
  scoped_refptr<TransportSocketParams> params2(new TransportSocketParams(
      host_port2, false, OnHostResolutionCallback(),
      TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));
  scoped_ptr<ClientSocketHandle> connection2(new ClientSocketHandle);
  EXPECT_EQ(ERR_IO_PENDING,
            connection2->Init(host_port2.ToString(), params2, DEFAULT_PRIORITY,
                              ClientSocketPool::RespectLimits::ENABLED,
                              callback2.callback(), pool, BoundNetLog()));
  EXPECT_TRUE(pool->IsStalled());

  // Running the message loop should cause the socket pool to ask the SPDY
  // session to close an idle socket, but since the socket is in use, nothing
  // happens.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(pool->IsStalled());
  EXPECT_FALSE(callback2.have_result());

  // Cancelling the request should result in the session's socket being
  // closed, since the pool is stalled.
  ASSERT_TRUE(spdy_stream1.get());
  spdy_stream1->Cancel();
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(pool->IsStalled());
  EXPECT_EQ(OK, callback2.WaitForResult());
}

// Verify that SpdySessionKey and therefore SpdySession is different when
// privacy mode is enabled or disabled.
TEST_P(SpdySessionTest, SpdySessionKeyPrivacyMode) {
  CreateNetworkSession();

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key_privacy_enabled(host_port_pair, ProxyServer::Direct(),
                                     PRIVACY_MODE_ENABLED);
  SpdySessionKey key_privacy_disabled(host_port_pair, ProxyServer::Direct(),
                                     PRIVACY_MODE_DISABLED);

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_enabled));
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_disabled));

  // Add SpdySession with PrivacyMode Enabled to the pool.
  base::WeakPtr<SpdySession> session_privacy_enabled =
      CreateFakeSpdySession(spdy_session_pool_, key_privacy_enabled);

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_privacy_enabled));
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_disabled));

  // Add SpdySession with PrivacyMode Disabled to the pool.
  base::WeakPtr<SpdySession> session_privacy_disabled =
      CreateFakeSpdySession(spdy_session_pool_, key_privacy_disabled);

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_privacy_enabled));
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_privacy_disabled));

  session_privacy_enabled->CloseSessionOnError(ERR_ABORTED, std::string());
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_enabled));
  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_privacy_disabled));

  session_privacy_disabled->CloseSessionOnError(ERR_ABORTED, std::string());
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_enabled));
  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_privacy_disabled));
}

// Delegate that creates another stream when its stream is closed.
class StreamCreatingDelegate : public test::StreamDelegateDoNothing {
 public:
  StreamCreatingDelegate(const base::WeakPtr<SpdyStream>& stream,
                         const base::WeakPtr<SpdySession>& session)
      : StreamDelegateDoNothing(stream),
        session_(session) {}

  ~StreamCreatingDelegate() override {}

  void OnClose(int status) override {
    GURL url(kDefaultURL);
    ignore_result(
        CreateStreamSynchronously(SPDY_REQUEST_RESPONSE_STREAM,
                                  session_, url, MEDIUM, BoundNetLog()));
  }

 private:
  const base::WeakPtr<SpdySession> session_;
};

// Create another stream in response to a stream being reset. Nothing
// should blow up. This is a regression test for
// http://crbug.com/263690 .
TEST_P(SpdySessionTest, CreateStreamOnStreamReset) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, MEDIUM, true));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
  };

  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_REFUSED_STREAM));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*rst, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      MockRead(ASYNC, 0, 4)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream->stream_id());

  StreamCreatingDelegate delegate(spdy_stream, session_);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  EXPECT_EQ(0u, spdy_stream->stream_id());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, spdy_stream->stream_id());

  // Cause the stream to be reset, which should cause another stream
  // to be created.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(spdy_stream);
  EXPECT_TRUE(delegate.StreamIsClosed());
  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, UpdateStreamsSendWindowSize) {
  // Set SETTINGS_INITIAL_WINDOW_SIZE to a small number so that WINDOW_UPDATE
  // gets sent.
  SettingsMap new_settings;
  int32_t window_size = 1;
  new_settings[SETTINGS_INITIAL_WINDOW_SIZE] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, window_size);

  // Set up the socket so we read a SETTINGS frame that sets
  // INITIAL_WINDOW_SIZE.
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(new_settings));
  MockRead reads[] = {
      CreateMockRead(*settings_frame, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 0, 2)  // EOF
  };

  scoped_ptr<SpdyFrame> settings_ack(spdy_util_.ConstructSpdySettingsAck());
  MockWrite writes[] = {
      CreateMockWrite(*settings_ack, 3),
  };

  session_deps_.host_resolver->set_synchronous_mode(true);

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  TestCompletionCallback callback1;
  EXPECT_NE(spdy_stream1->send_window_size(), window_size);

  // Process the SETTINGS frame.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(session_->stream_initial_send_window_size(), window_size);
  EXPECT_EQ(spdy_stream1->send_window_size(), window_size);

  // Release the first one, this will allow the second to be created.
  spdy_stream1->Cancel();
  EXPECT_FALSE(spdy_stream1);

  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(spdy_stream2.get() != nullptr);
  EXPECT_EQ(spdy_stream2->send_window_size(), window_size);
  spdy_stream2->Cancel();
  EXPECT_FALSE(spdy_stream2);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// SpdySession::{Increase,Decrease}RecvWindowSize should properly
// adjust the session receive window size. In addition,
// SpdySession::IncreaseRecvWindowSize should trigger
// sending a WINDOW_UPDATE frame for a large enough delta.
TEST_P(SpdySessionTest, AdjustRecvWindowSize) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  const int32_t initial_window_size =
      SpdySession::GetDefaultInitialWindowSize(GetProtocol());
  const int32_t delta_window_size = 100;

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), MockRead(ASYNC, 0, 2)  // EOF
  };
  scoped_ptr<SpdyFrame> window_update(spdy_util_.ConstructSpdyWindowUpdate(
      kSessionFlowControlStreamId, initial_window_size + delta_window_size));
  MockWrite writes[] = {
    CreateMockWrite(*window_update, 0),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  session_->IncreaseRecvWindowSize(delta_window_size);
  EXPECT_EQ(initial_window_size + delta_window_size,
            session_->session_recv_window_size_);
  EXPECT_EQ(delta_window_size, session_->session_unacked_recv_window_bytes_);

  // Should trigger sending a WINDOW_UPDATE frame.
  session_->IncreaseRecvWindowSize(initial_window_size);
  EXPECT_EQ(initial_window_size + delta_window_size + initial_window_size,
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  base::RunLoop().RunUntilIdle();

  // DecreaseRecvWindowSize() expects |in_io_loop_| to be true.
  session_->in_io_loop_ = true;
  session_->DecreaseRecvWindowSize(initial_window_size + delta_window_size +
                                   initial_window_size);
  session_->in_io_loop_ = false;
  EXPECT_EQ(0, session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// SpdySession::{Increase,Decrease}SendWindowSize should properly
// adjust the session send window size when the "enable_spdy_31" flag
// is set.
TEST_P(SpdySessionTest, AdjustSendWindowSize) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
    MockRead(SYNCHRONOUS, 0, 0)  // EOF
  };
  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  session_ = CreateFakeSpdySession(spdy_session_pool_, key_);

  const int32_t initial_window_size =
      SpdySession::GetDefaultInitialWindowSize(GetProtocol());
  const int32_t delta_window_size = 100;

  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);

  session_->IncreaseSendWindowSize(delta_window_size);
  EXPECT_EQ(initial_window_size + delta_window_size,
            session_->session_send_window_size_);

  session_->DecreaseSendWindowSize(delta_window_size);
  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);
}

// Incoming data for an inactive stream should not cause the session
// receive window size to decrease, but it should cause the unacked
// bytes to increase.
TEST_P(SpdySessionTest, SessionFlowControlInactiveStream) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> resp(spdy_util_.ConstructSpdyBodyFrame(1, false));
  MockRead reads[] = {
      CreateMockRead(*resp, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 0, 2)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(kUploadDataSize, session_->session_unacked_recv_window_bytes_);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// The frame header is not included in flow control, but frame payload
// (including optional pad length and padding) is.
TEST_P(SpdySessionTest, SessionFlowControlPadding) {
  // Padding only exists in HTTP/2.
  if (GetProtocol() < kProtoHTTP2)
    return;

  session_deps_.host_resolver->set_synchronous_mode(true);

  const int padding_length = 42;
  scoped_ptr<SpdyFrame> resp(spdy_util_.ConstructSpdyBodyFrame(
      1, kUploadData, kUploadDataSize, false, padding_length));
  MockRead reads[] = {
      CreateMockRead(*resp, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 0, 2)  // EOF
  };
  SequencedSocketData data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(SpdySession::GetDefaultInitialWindowSize(GetProtocol()),
            session_->session_recv_window_size_);
  EXPECT_EQ(kUploadDataSize + padding_length,
            session_->session_unacked_recv_window_bytes_);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Peer sends more data than stream level receiving flow control window.
TEST_P(SpdySessionTest, StreamFlowControlTooMuchData) {
  const int32_t stream_max_recv_window_size = 1024;
  const int32_t data_frame_size = 2 * stream_max_recv_window_size;

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_FLOW_CONTROL_ERROR));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 4),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  const std::string payload(data_frame_size, 'a');
  scoped_ptr<SpdyFrame> data_frame(spdy_util_.ConstructSpdyBodyFrame(
      1, payload.data(), data_frame_size, false));
  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*data_frame, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 5),
      MockRead(ASYNC, 0, 6),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  CreateNetworkSession();

  SpdySessionPoolPeer pool_peer(spdy_session_pool_);
  pool_peer.SetStreamInitialRecvWindowSize(stream_max_recv_window_size);
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  EXPECT_EQ(stream_max_recv_window_size, spdy_stream->recv_window_size());

  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  EXPECT_EQ(ERR_IO_PENDING, spdy_stream->SendRequestHeaders(
                                std::move(headers), NO_MORE_DATA_TO_SEND));

  // Request and response.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, spdy_stream->stream_id());

  // Too large data frame causes flow control error, should close stream.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream);

  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Regression test for a bug that was caused by including unsent WINDOW_UPDATE
// deltas in the receiving window size when checking incoming frames for flow
// control errors at session level.
TEST_P(SpdySessionTest, SessionFlowControlTooMuchDataTwoDataFrames) {
  const int32_t session_max_recv_window_size = 500;
  const int32_t first_data_frame_size = 200;
  const int32_t second_data_frame_size = 400;

  // First data frame should not trigger a WINDOW_UPDATE.
  ASSERT_GT(session_max_recv_window_size / 2, first_data_frame_size);
  // Second data frame would be fine had there been a WINDOW_UPDATE.
  ASSERT_GT(session_max_recv_window_size, second_data_frame_size);
  // But in fact, the two data frames together overflow the receiving window at
  // session level.
  ASSERT_LT(session_max_recv_window_size,
            first_data_frame_size + second_data_frame_size);

  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(
      0, GOAWAY_FLOW_CONTROL_ERROR,
      "delta_window_size is 400 in DecreaseRecvWindowSize, which is larger "
      "than the receive window size of 500"));
  MockWrite writes[] = {
      CreateMockWrite(*goaway, 4),
  };

  const std::string first_data_frame(first_data_frame_size, 'a');
  scoped_ptr<SpdyFrame> first(spdy_util_.ConstructSpdyBodyFrame(
      1, first_data_frame.data(), first_data_frame_size, false));
  const std::string second_data_frame(second_data_frame_size, 'b');
  scoped_ptr<SpdyFrame> second(spdy_util_.ConstructSpdyBodyFrame(
      1, second_data_frame.data(), second_data_frame_size, false));
  MockRead reads[] = {
      CreateMockRead(*first, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*second, 2),
      MockRead(ASYNC, 0, 3),
  };
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  // Setting session level receiving window size to smaller than initial is not
  // possible via SpdySessionPoolPeer.
  session_->session_recv_window_size_ = session_max_recv_window_size;

  // First data frame is immediately consumed and does not trigger
  // WINDOW_UPDATE.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(first_data_frame_size,
            session_->session_unacked_recv_window_bytes_);
  EXPECT_EQ(session_max_recv_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(SpdySession::STATE_AVAILABLE, session_->availability_state_);

  // Second data frame overflows receiving window, causes session to close.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SpdySession::STATE_DRAINING, session_->availability_state_);
}

// Regression test for a bug that was caused by including unsent WINDOW_UPDATE
// deltas in the receiving window size when checking incoming data frames for
// flow control errors at stream level.
TEST_P(SpdySessionTest, StreamFlowControlTooMuchDataTwoDataFrames) {
  const int32_t stream_max_recv_window_size = 500;
  const int32_t first_data_frame_size = 200;
  const int32_t second_data_frame_size = 400;

  // First data frame should not trigger a WINDOW_UPDATE.
  ASSERT_GT(stream_max_recv_window_size / 2, first_data_frame_size);
  // Second data frame would be fine had there been a WINDOW_UPDATE.
  ASSERT_GT(stream_max_recv_window_size, second_data_frame_size);
  // But in fact, they should overflow the receiving window at stream level.
  ASSERT_LT(stream_max_recv_window_size,
            first_data_frame_size + second_data_frame_size);

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_FLOW_CONTROL_ERROR));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 6),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  const std::string first_data_frame(first_data_frame_size, 'a');
  scoped_ptr<SpdyFrame> first(spdy_util_.ConstructSpdyBodyFrame(
      1, first_data_frame.data(), first_data_frame_size, false));
  const std::string second_data_frame(second_data_frame_size, 'b');
  scoped_ptr<SpdyFrame> second(spdy_util_.ConstructSpdyBodyFrame(
      1, second_data_frame.data(), second_data_frame_size, false));
  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      CreateMockRead(*first, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      CreateMockRead(*second, 5),
      MockRead(ASYNC, ERR_IO_PENDING, 7),
      MockRead(ASYNC, 0, 8),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  SpdySessionPoolPeer pool_peer(spdy_session_pool_);
  pool_peer.SetStreamInitialRecvWindowSize(stream_max_recv_window_size);

  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  EXPECT_EQ(ERR_IO_PENDING, spdy_stream->SendRequestHeaders(
                                std::move(headers), NO_MORE_DATA_TO_SEND));

  // Request and response.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(spdy_stream->IsLocallyClosed());
  EXPECT_EQ(stream_max_recv_window_size, spdy_stream->recv_window_size());

  // First data frame.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(spdy_stream->IsLocallyClosed());
  EXPECT_EQ(stream_max_recv_window_size - first_data_frame_size,
            spdy_stream->recv_window_size());

  // Consume first data frame.  This does not trigger a WINDOW_UPDATE.
  std::string received_data = delegate.TakeReceivedData();
  EXPECT_EQ(static_cast<size_t>(first_data_frame_size), received_data.size());
  EXPECT_EQ(stream_max_recv_window_size, spdy_stream->recv_window_size());

  // Second data frame overflows receiving window, causes the stream to close.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(spdy_stream.get());

  // RST_STREAM
  EXPECT_TRUE(session_);
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// A delegate that drops any received data.
class DropReceivedDataDelegate : public test::StreamDelegateSendImmediate {
 public:
  DropReceivedDataDelegate(const base::WeakPtr<SpdyStream>& stream,
                           base::StringPiece data)
      : StreamDelegateSendImmediate(stream, data) {}

  ~DropReceivedDataDelegate() override {}

  // Drop any received data.
  void OnDataReceived(scoped_ptr<SpdyBuffer> buffer) override {}
};

// Send data back and forth but use a delegate that drops its received
// data. The receive window should still increase to its original
// value, i.e. we shouldn't "leak" receive window bytes.
TEST_P(SpdySessionTest, SessionFlowControlNoReceiveLeaks) {
  const int32_t kMsgDataSize = 100;
  const std::string msg_data(kMsgDataSize, 'a');

  scoped_ptr<SpdyFrame> req(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kMsgDataSize, MEDIUM, nullptr, 0));
  scoped_ptr<SpdyFrame> msg(spdy_util_.ConstructSpdyBodyFrame(
      1, msg_data.data(), kMsgDataSize, false));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
    CreateMockWrite(*msg, 2),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> echo(spdy_util_.ConstructSpdyBodyFrame(
      1, msg_data.data(), kMsgDataSize, false));
  scoped_ptr<SpdyFrame> window_update(spdy_util_.ConstructSpdyWindowUpdate(
      kSessionFlowControlStreamId, kMsgDataSize));
  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      CreateMockRead(*echo, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),
      MockRead(ASYNC, 0, 5)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(stream.get() != nullptr);
  EXPECT_EQ(0u, stream->stream_id());

  DropReceivedDataDelegate delegate(stream, msg_data);
  stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kMsgDataSize));
  EXPECT_EQ(ERR_IO_PENDING,
            stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream->HasUrlFromHeaders());

  const int32_t initial_window_size =
      SpdySession::GetDefaultInitialWindowSize(GetProtocol());
  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(kMsgDataSize, session_->session_unacked_recv_window_bytes_);

  stream->Close();
  EXPECT_FALSE(stream);

  EXPECT_EQ(OK, delegate.WaitForClose());

  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(kMsgDataSize, session_->session_unacked_recv_window_bytes_);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Send data back and forth but close the stream before its data frame
// can be written to the socket. The send window should then increase
// to its original value, i.e. we shouldn't "leak" send window bytes.
TEST_P(SpdySessionTest, SessionFlowControlNoSendLeaks) {
  const int32_t kMsgDataSize = 100;
  const std::string msg_data(kMsgDataSize, 'a');

  scoped_ptr<SpdyFrame> req(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kMsgDataSize, MEDIUM, nullptr, 0));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*resp, 2),
      MockRead(ASYNC, 0, 3)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(stream.get() != nullptr);
  EXPECT_EQ(0u, stream->stream_id());

  test::StreamDelegateSendImmediate delegate(stream, msg_data);
  stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kMsgDataSize));
  EXPECT_EQ(ERR_IO_PENDING,
            stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream->HasUrlFromHeaders());

  const int32_t initial_window_size =
      SpdySession::GetDefaultInitialWindowSize(GetProtocol());
  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);

  // Write request.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);

  // Read response, but do not run the message loop, so that the body is not
  // written to the socket.
  data.Resume();

  EXPECT_EQ(initial_window_size - kMsgDataSize,
            session_->session_send_window_size_);

  // Closing the stream should increase the session's send window.
  stream->Close();
  EXPECT_FALSE(stream);

  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);

  EXPECT_EQ(OK, delegate.WaitForClose());

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Send data back and forth; the send and receive windows should
// change appropriately.
TEST_P(SpdySessionTest, SessionFlowControlEndToEnd) {
  const int32_t kMsgDataSize = 100;
  const std::string msg_data(kMsgDataSize, 'a');

  scoped_ptr<SpdyFrame> req(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kMsgDataSize, MEDIUM, nullptr, 0));
  scoped_ptr<SpdyFrame> msg(spdy_util_.ConstructSpdyBodyFrame(
      1, msg_data.data(), kMsgDataSize, false));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
    CreateMockWrite(*msg, 2),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> echo(spdy_util_.ConstructSpdyBodyFrame(
      1, msg_data.data(), kMsgDataSize, false));
  scoped_ptr<SpdyFrame> window_update(spdy_util_.ConstructSpdyWindowUpdate(
      kSessionFlowControlStreamId, kMsgDataSize));
  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*echo, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),
      CreateMockRead(*window_update, 6),
      MockRead(ASYNC, ERR_IO_PENDING, 7),
      MockRead(ASYNC, 0, 8)  // EOF
  };

  // Create SpdySession and SpdyStream and send the request.
  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(stream.get() != nullptr);
  EXPECT_EQ(0u, stream->stream_id());

  test::StreamDelegateSendImmediate delegate(stream, msg_data);
  stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kMsgDataSize));
  EXPECT_EQ(ERR_IO_PENDING,
            stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream->HasUrlFromHeaders());

  const int32_t initial_window_size =
      SpdySession::GetDefaultInitialWindowSize(GetProtocol());
  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  // Send request and message.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_window_size - kMsgDataSize,
            session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  // Read echo.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_window_size - kMsgDataSize,
            session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size - kMsgDataSize,
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  // Read window update.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size - kMsgDataSize,
            session_->session_recv_window_size_);
  EXPECT_EQ(0, session_->session_unacked_recv_window_bytes_);

  EXPECT_EQ(msg_data, delegate.TakeReceivedData());

  // Draining the delegate's read queue should increase the session's
  // receive window.
  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(kMsgDataSize, session_->session_unacked_recv_window_bytes_);

  stream->Close();
  EXPECT_FALSE(stream);

  EXPECT_EQ(OK, delegate.WaitForClose());

  EXPECT_EQ(initial_window_size, session_->session_send_window_size_);
  EXPECT_EQ(initial_window_size, session_->session_recv_window_size_);
  EXPECT_EQ(kMsgDataSize, session_->session_unacked_recv_window_bytes_);

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Given a stall function and an unstall function, runs a test to make
// sure that a stream resumes after unstall.
void SpdySessionTest::RunResumeAfterUnstallTest(
    const base::Callback<void(SpdyStream*)>& stall_function,
    const base::Callback<void(SpdyStream*, int32_t)>& unstall_function) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> body(
      spdy_util_.ConstructSpdyBodyFrame(1, kBodyData, kBodyDataSize, true));
  MockWrite writes[] = {
    CreateMockWrite(*req, 0),
    CreateMockWrite(*body, 1),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> echo(
      spdy_util_.ConstructSpdyBodyFrame(1, kBodyData, kBodyDataSize, false));
  MockRead reads[] = {
      CreateMockRead(*resp, 2), MockRead(ASYNC, 0, 3)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream.get() != nullptr);

  test::StreamDelegateWithBody delegate(stream, kBodyDataStringPiece);
  stream->SetDelegate(&delegate);

  EXPECT_FALSE(stream->HasUrlFromHeaders());
  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING,
            stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream->GetUrlFromHeaders().spec());

  stall_function.Run(stream.get());

  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(stream->send_stalled_by_flow_control());

  unstall_function.Run(stream.get(), kBodyDataSize);

  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate.WaitForClose());

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(), delegate.TakeReceivedData());
  EXPECT_FALSE(session_);
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

// Run the resume-after-unstall test with all possible stall and
// unstall sequences.

TEST_P(SpdySessionTest, ResumeAfterUnstallSession) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallSessionOnly,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallSessionOnly,
                 base::Unretained(this)));
}

// Equivalent to
// SpdyStreamTest.ResumeAfterSendWindowSizeIncrease.
TEST_P(SpdySessionTest, ResumeAfterUnstallStream) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallStreamOnly,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallStreamOnly,
                 base::Unretained(this)));
}

TEST_P(SpdySessionTest, StallSessionStreamResumeAfterUnstallSessionStream) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallSessionStream,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallSessionStream,
                 base::Unretained(this)));
}

TEST_P(SpdySessionTest, StallStreamSessionResumeAfterUnstallSessionStream) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallStreamSession,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallSessionStream,
                 base::Unretained(this)));
}

TEST_P(SpdySessionTest, StallStreamSessionResumeAfterUnstallStreamSession) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallStreamSession,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallStreamSession,
                 base::Unretained(this)));
}

TEST_P(SpdySessionTest, StallSessionStreamResumeAfterUnstallStreamSession) {
  RunResumeAfterUnstallTest(
      base::Bind(&SpdySessionTest::StallSessionStream,
                 base::Unretained(this)),
      base::Bind(&SpdySessionTest::UnstallStreamSession,
                 base::Unretained(this)));
}

// Cause a stall by reducing the flow control send window to 0. The
// streams should resume in priority order when that window is then
// increased.
TEST_P(SpdySessionTest, ResumeByPriorityAfterSendWindowSizeIncrease) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req1(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> req2(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 3, kBodyDataSize, MEDIUM, nullptr, 0));
  scoped_ptr<SpdyFrame> body1(
      spdy_util_.ConstructSpdyBodyFrame(1, kBodyData, kBodyDataSize, true));
  scoped_ptr<SpdyFrame> body2(
      spdy_util_.ConstructSpdyBodyFrame(3, kBodyData, kBodyDataSize, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
    CreateMockWrite(*body2, 2),
    CreateMockWrite(*body1, 3),
  };

  scoped_ptr<SpdyFrame> resp1(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> resp2(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 3));
  MockRead reads[] = {
      CreateMockRead(*resp1, 4),
      CreateMockRead(*resp2, 5),
      MockRead(ASYNC, 0, 6)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream1.get() != nullptr);

  test::StreamDelegateWithBody delegate1(stream1, kBodyDataStringPiece);
  stream1->SetDelegate(&delegate1);

  EXPECT_FALSE(stream1->HasUrlFromHeaders());

  base::WeakPtr<SpdyStream> stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, MEDIUM, BoundNetLog());
  ASSERT_TRUE(stream2.get() != nullptr);

  test::StreamDelegateWithBody delegate2(stream2, kBodyDataStringPiece);
  stream2->SetDelegate(&delegate2);

  EXPECT_FALSE(stream2->HasUrlFromHeaders());

  EXPECT_FALSE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());

  StallSessionSend();

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream1->SendRequestHeaders(std::move(headers1),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream1->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream1->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, stream1->stream_id());
  EXPECT_TRUE(stream1->send_stalled_by_flow_control());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream2->SendRequestHeaders(std::move(headers2),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream2->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream2->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3u, stream2->stream_id());
  EXPECT_TRUE(stream2->send_stalled_by_flow_control());

  // This should unstall only stream2.
  UnstallSessionSend(kBodyDataSize);

  EXPECT_TRUE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());

  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());

  // This should then unstall stream1.
  UnstallSessionSend(kBodyDataSize);

  EXPECT_FALSE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate1.WaitForClose());
  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate2.WaitForClose());

  EXPECT_TRUE(delegate1.send_headers_completed());
  EXPECT_EQ("200", delegate1.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(), delegate1.TakeReceivedData());

  EXPECT_TRUE(delegate2.send_headers_completed());
  EXPECT_EQ("200", delegate2.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(), delegate2.TakeReceivedData());

  EXPECT_FALSE(session_);
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Delegate that closes a given stream after sending its body.
class StreamClosingDelegate : public test::StreamDelegateWithBody {
 public:
  StreamClosingDelegate(const base::WeakPtr<SpdyStream>& stream,
                        base::StringPiece data)
      : StreamDelegateWithBody(stream, data) {}

  ~StreamClosingDelegate() override {}

  void set_stream_to_close(const base::WeakPtr<SpdyStream>& stream_to_close) {
    stream_to_close_ = stream_to_close;
  }

  void OnDataSent() override {
    test::StreamDelegateWithBody::OnDataSent();
    if (stream_to_close_.get()) {
      stream_to_close_->Close();
      EXPECT_FALSE(stream_to_close_);
    }
  }

 private:
  base::WeakPtr<SpdyStream> stream_to_close_;
};

// Cause a stall by reducing the flow control send window to
// 0. Unstalling the session should properly handle deleted streams.
TEST_P(SpdySessionTest, SendWindowSizeIncreaseWithDeletedStreams) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req1(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> req2(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 3, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> req3(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 5, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> body2(
      spdy_util_.ConstructSpdyBodyFrame(3, kBodyData, kBodyDataSize, true));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
    CreateMockWrite(*req3, 2),
    CreateMockWrite(*body2, 3),
  };

  scoped_ptr<SpdyFrame> resp2(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 3));
  MockRead reads[] = {
      CreateMockRead(*resp2, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),
      MockRead(ASYNC, 0, 6)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream1.get() != nullptr);

  test::StreamDelegateWithBody delegate1(stream1, kBodyDataStringPiece);
  stream1->SetDelegate(&delegate1);

  EXPECT_FALSE(stream1->HasUrlFromHeaders());

  base::WeakPtr<SpdyStream> stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream2.get() != nullptr);

  StreamClosingDelegate delegate2(stream2, kBodyDataStringPiece);
  stream2->SetDelegate(&delegate2);

  EXPECT_FALSE(stream2->HasUrlFromHeaders());

  base::WeakPtr<SpdyStream> stream3 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream3.get() != nullptr);

  test::StreamDelegateWithBody delegate3(stream3, kBodyDataStringPiece);
  stream3->SetDelegate(&delegate3);

  EXPECT_FALSE(stream3->HasUrlFromHeaders());

  EXPECT_FALSE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());
  EXPECT_FALSE(stream3->send_stalled_by_flow_control());

  StallSessionSend();

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream1->SendRequestHeaders(std::move(headers1),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream1->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream1->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, stream1->stream_id());
  EXPECT_TRUE(stream1->send_stalled_by_flow_control());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream2->SendRequestHeaders(std::move(headers2),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream2->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream2->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3u, stream2->stream_id());
  EXPECT_TRUE(stream2->send_stalled_by_flow_control());

  scoped_ptr<SpdyHeaderBlock> headers3(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream3->SendRequestHeaders(std::move(headers3),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream3->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream3->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(5u, stream3->stream_id());
  EXPECT_TRUE(stream3->send_stalled_by_flow_control());

  SpdyStreamId stream_id1 = stream1->stream_id();
  SpdyStreamId stream_id2 = stream2->stream_id();
  SpdyStreamId stream_id3 = stream3->stream_id();

  // Close stream1 preemptively.
  session_->CloseActiveStream(stream_id1, ERR_CONNECTION_CLOSED);
  EXPECT_FALSE(stream1);

  EXPECT_FALSE(session_->IsStreamActive(stream_id1));
  EXPECT_TRUE(session_->IsStreamActive(stream_id2));
  EXPECT_TRUE(session_->IsStreamActive(stream_id3));

  // Unstall stream2, which should then close stream3.
  delegate2.set_stream_to_close(stream3);
  UnstallSessionSend(kBodyDataSize);

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stream3);

  EXPECT_FALSE(stream2->send_stalled_by_flow_control());
  EXPECT_FALSE(session_->IsStreamActive(stream_id1));
  EXPECT_TRUE(session_->IsStreamActive(stream_id2));
  EXPECT_FALSE(session_->IsStreamActive(stream_id3));

  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stream2);
  EXPECT_FALSE(session_);

  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate1.WaitForClose());
  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate2.WaitForClose());
  EXPECT_EQ(OK, delegate3.WaitForClose());

  EXPECT_TRUE(delegate1.send_headers_completed());
  EXPECT_EQ(std::string(), delegate1.TakeReceivedData());

  EXPECT_TRUE(delegate2.send_headers_completed());
  EXPECT_EQ("200", delegate2.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(), delegate2.TakeReceivedData());

  EXPECT_TRUE(delegate3.send_headers_completed());
  EXPECT_EQ(std::string(), delegate3.TakeReceivedData());

  EXPECT_TRUE(data.AllWriteDataConsumed());
}

// Cause a stall by reducing the flow control send window to
// 0. Unstalling the session should properly handle the session itself
// being closed.
TEST_P(SpdySessionTest, SendWindowSizeIncreaseWithDeletedSession) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  scoped_ptr<SpdyFrame> req1(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 1, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> req2(spdy_util_.ConstructSpdyPost(
      kDefaultURL, 3, kBodyDataSize, LOWEST, nullptr, 0));
  scoped_ptr<SpdyFrame> body1(
      spdy_util_.ConstructSpdyBodyFrame(1, kBodyData, kBodyDataSize, false));
  MockWrite writes[] = {
    CreateMockWrite(*req1, 0),
    CreateMockWrite(*req2, 1),
  };

  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 2), MockRead(ASYNC, 0, 3)  // EOF
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream1.get() != nullptr);

  test::StreamDelegateWithBody delegate1(stream1, kBodyDataStringPiece);
  stream1->SetDelegate(&delegate1);

  EXPECT_FALSE(stream1->HasUrlFromHeaders());

  base::WeakPtr<SpdyStream> stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(stream2.get() != nullptr);

  test::StreamDelegateWithBody delegate2(stream2, kBodyDataStringPiece);
  stream2->SetDelegate(&delegate2);

  EXPECT_FALSE(stream2->HasUrlFromHeaders());

  EXPECT_FALSE(stream1->send_stalled_by_flow_control());
  EXPECT_FALSE(stream2->send_stalled_by_flow_control());

  StallSessionSend();

  scoped_ptr<SpdyHeaderBlock> headers1(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream1->SendRequestHeaders(std::move(headers1),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream1->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream1->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, stream1->stream_id());
  EXPECT_TRUE(stream1->send_stalled_by_flow_control());

  scoped_ptr<SpdyHeaderBlock> headers2(
      spdy_util_.ConstructPostHeaderBlock(kDefaultURL, kBodyDataSize));
  EXPECT_EQ(ERR_IO_PENDING, stream2->SendRequestHeaders(std::move(headers2),
                                                        MORE_DATA_TO_SEND));
  EXPECT_TRUE(stream2->HasUrlFromHeaders());
  EXPECT_EQ(kDefaultURL, stream2->GetUrlFromHeaders().spec());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3u, stream2->stream_id());
  EXPECT_TRUE(stream2->send_stalled_by_flow_control());

  EXPECT_TRUE(HasSpdySession(spdy_session_pool_, key_));

  // Unstall stream1.
  UnstallSessionSend(kBodyDataSize);

  // Close the session (since we can't do it from within the delegate
  // method, since it's in the stream's loop).
  session_->CloseSessionOnError(ERR_CONNECTION_CLOSED, "Closing session");
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);

  EXPECT_FALSE(HasSpdySession(spdy_session_pool_, key_));

  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate1.WaitForClose());
  EXPECT_EQ(ERR_CONNECTION_CLOSED, delegate2.WaitForClose());

  EXPECT_TRUE(delegate1.send_headers_completed());
  EXPECT_EQ(std::string(), delegate1.TakeReceivedData());

  EXPECT_TRUE(delegate2.send_headers_completed());
  EXPECT_EQ(std::string(), delegate2.TakeReceivedData());

  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_P(SpdySessionTest, GoAwayOnSessionFlowControlError) {
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> goaway(spdy_util_.ConstructSpdyGoAway(
      0, GOAWAY_FLOW_CONTROL_ERROR,
      "delta_window_size is 6 in DecreaseRecvWindowSize, which is larger than "
      "the receive window size of 1"));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*goaway, 4),
  };

  scoped_ptr<SpdyFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  scoped_ptr<SpdyFrame> body(spdy_util_.ConstructSpdyBodyFrame(1, true));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*resp, 2),
      CreateMockRead(*body, 3),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream.get() != nullptr);
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);

  // Write request.
  base::RunLoop().RunUntilIdle();

  // Put session on the edge of overflowing it's recv window.
  session_->session_recv_window_size_ = 1;

  // Read response headers & body. Body overflows the session window, and a
  // goaway is written.
  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(ERR_SPDY_FLOW_CONTROL_ERROR, delegate.WaitForClose());
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, SplitHeaders) {
  GURL kStreamUrl("http://www.example.org/foo.dat");
  SpdyHeaderBlock headers;
  spdy_util_.AddUrlToHeaderBlock(kStreamUrl.spec(), &headers);
  headers["alpha"] = "beta";

  SpdyHeaderBlock request_headers;
  SpdyHeaderBlock response_headers;

  SplitPushedHeadersToRequestAndResponse(
      headers, spdy_util_.spdy_version(), &request_headers, &response_headers);

  SpdyHeaderBlock::const_iterator it = response_headers.find("alpha");
  std::string alpha_val =
      (it == response_headers.end()) ? std::string() : it->second.as_string();
  EXPECT_EQ("beta", alpha_val);

  GURL request_url =
      GetUrlFromHeaderBlock(request_headers, spdy_util_.spdy_version());
  EXPECT_EQ(kStreamUrl, request_url);
}

// Regression. Sorta. Push streams and client streams were sharing a single
// limit for a long time.
TEST_P(SpdySessionTest, PushedStreamShouldNotCountToClientConcurrencyLimit) {
  SettingsMap new_settings;
  new_settings[SETTINGS_MAX_CONCURRENT_STREAMS] =
      SettingsFlagsAndValue(SETTINGS_FLAG_NONE, 2);
  scoped_ptr<SpdyFrame> settings_frame(
      spdy_util_.ConstructSpdySettings(new_settings));
  scoped_ptr<SpdyFrame> pushed(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, "http://www.example.org/a.dat"));
  MockRead reads[] = {
      CreateMockRead(*settings_frame, 0),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*pushed, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),
      MockRead(ASYNC, 0, 6),
  };

  scoped_ptr<SpdyFrame> settings_ack(spdy_util_.ConstructSpdySettingsAck());
  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  MockWrite writes[] = {
      CreateMockWrite(*settings_ack, 1), CreateMockWrite(*req, 2),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  // Read the settings frame.
  base::RunLoop().RunUntilIdle();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Run until pushed stream is created.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Second stream should not be stalled, although we have 2 active streams, but
  // one of them is push stream and should not be taken into account when we
  // create streams on the client.
  base::WeakPtr<SpdyStream> spdy_stream2 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  EXPECT_TRUE(spdy_stream2);
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, RejectPushedStreamExceedingConcurrencyLimit) {
  scoped_ptr<SpdyFrame> push_a(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, "http://www.example.org/a.dat"));
  scoped_ptr<SpdyFrame> push_b(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 4, 1, "http://www.example.org/b.dat"));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*push_a, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*push_b, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 6),
      MockRead(ASYNC, 0, 7),
  };

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(4, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 5),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  session_->set_max_concurrent_pushed_streams(1);

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Run until pushed stream is created.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Reset incoming pushed stream.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Tests that HTTP SPDY push streams that advertise an origin different from the
// associated stream are accepted from a trusted SPDY proxy.
TEST_P(SpdySessionTest, TrustedSpdyProxy) {
  // Origin of kDefaultURL should be different from the origin of
  // kHttpURLFromAnotherOrigin and kHttpsURLFromAnotherOrigin.
  ASSERT_NE(GURL(kDefaultURL).host(), GURL(kHttpURLFromAnotherOrigin).host());
  ASSERT_NE(GURL(kDefaultURL).host(), GURL(kHttpsURLFromAnotherOrigin).host());

  // cross_origin_push contains HTTP resource for an origin different from the
  // origin of kDefaultURL, and should be accepted.
  scoped_ptr<SpdyFrame> cross_origin_push(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, kHttpURLFromAnotherOrigin));
  // cross_origin_https_push contains HTTPS resource, and should be refused.
  scoped_ptr<SpdyFrame> cross_origin_https_push(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 4, 1, kHttpsURLFromAnotherOrigin));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*cross_origin_push, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*cross_origin_https_push, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 6),
      MockRead(ASYNC, 0, 7),
  };

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(4, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 5),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  scoped_ptr<TestProxyDelegate> proxy_delegate(new TestProxyDelegate());
  proxy_delegate->set_trusted_spdy_proxy(
      net::ProxyServer(net::ProxyServer::SCHEME_HTTPS,
                       HostPortPair(GURL(kDefaultURL).host(), 80)));
  session_deps_.proxy_delegate.reset(proxy_delegate.release());

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream->stream_id());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Run until pushed stream is created.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Reset incoming pushed stream.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

// Tests that if the SPDY trusted proxy is not set, then push streams that
// advertise an origin different from the associated stream are refused.
TEST_P(SpdySessionTest, TrustedSpdyProxyNotSet) {
  // Origin of kDefaultURL should be different from the origin of
  // kHttpURLFromAnotherOrigin.
  ASSERT_NE(GURL(kDefaultURL).host(), GURL(kHttpURLFromAnotherOrigin).host());

  // cross_origin_push contains resource for an origin different from the
  // origin of kDefaultURL, and should be refused.
  scoped_ptr<SpdyFrame> cross_origin_push(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, kHttpURLFromAnotherOrigin));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), CreateMockRead(*cross_origin_push, 2),
      MockRead(ASYNC, 0, 4),
  };

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(2, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 3),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream->stream_id());
  test::StreamDelegateDoNothing delegate(spdy_stream);
  spdy_stream->SetDelegate(&delegate);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, IgnoreReservedRemoteStreamsCount) {
  // Streams in reserved remote state exist only in HTTP/2.
  if (spdy_util_.spdy_version() < HTTP2)
    return;

  scoped_ptr<SpdyFrame> push_a(spdy_util_.ConstructSpdyPush(
      nullptr, 0, 2, 1, "http://www.example.org/a.dat"));
  scoped_ptr<SpdyHeaderBlock> push_headers(new SpdyHeaderBlock);
  spdy_util_.AddUrlToHeaderBlock("http://www.example.org/b.dat",
                                 push_headers.get());
  scoped_ptr<SpdyFrame> push_b(
      spdy_util_.ConstructInitialSpdyPushFrame(std::move(push_headers), 4, 1));
  scoped_ptr<SpdyFrame> headers_b(
      spdy_util_.ConstructSpdyPushHeaders(4, nullptr, 0));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*push_a, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*push_b, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),
      CreateMockRead(*headers_b, 6),
      MockRead(ASYNC, ERR_IO_PENDING, 8),
      MockRead(ASYNC, 0, 9),
  };

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(4, RST_STREAM_REFUSED_STREAM));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 7),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();
  session_->set_max_concurrent_pushed_streams(1);

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Run until pushed stream is created.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Accept promised stream. It should not count towards pushed stream limit.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(2u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Reset last pushed stream upon headers reception as it is going to be 2nd,
  // while we accept only one.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(1u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(session_);
}

TEST_P(SpdySessionTest, CancelReservedStreamOnHeadersReceived) {
  // Streams in reserved remote state exist only in HTTP/2.
  if (spdy_util_.spdy_version() < HTTP2)
    return;

  const char kPushedUrl[] = "http://www.example.org/a.dat";
  scoped_ptr<SpdyHeaderBlock> push_headers(new SpdyHeaderBlock);
  spdy_util_.AddUrlToHeaderBlock(kPushedUrl, push_headers.get());
  scoped_ptr<SpdyFrame> push_promise(
      spdy_util_.ConstructInitialSpdyPushFrame(std::move(push_headers), 2, 1));
  scoped_ptr<SpdyFrame> headers_frame(
      spdy_util_.ConstructSpdyPushHeaders(2, nullptr, 0));
  MockRead reads[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      CreateMockRead(*push_promise, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(*headers_frame, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 6),
      MockRead(ASYNC, 0, 7),
  };

  scoped_ptr<SpdyFrame> req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST, true));
  scoped_ptr<SpdyFrame> rst(
      spdy_util_.ConstructSpdyRstStream(2, RST_STREAM_CANCEL));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 5),
  };

  SequencedSocketData data(reads, arraysize(reads), writes, arraysize(writes));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  base::WeakPtr<SpdyStream> spdy_stream1 = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session_, test_url_, LOWEST, BoundNetLog());
  ASSERT_TRUE(spdy_stream1.get() != nullptr);
  EXPECT_EQ(0u, spdy_stream1->stream_id());
  test::StreamDelegateDoNothing delegate1(spdy_stream1);
  spdy_stream1->SetDelegate(&delegate1);

  EXPECT_EQ(0u, session_->num_active_streams());
  EXPECT_EQ(1u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  scoped_ptr<SpdyHeaderBlock> headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultURL));
  spdy_stream1->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND);
  EXPECT_TRUE(spdy_stream1->HasUrlFromHeaders());

  // Run until 1st stream is activated.
  EXPECT_EQ(0u, delegate1.stream_id());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, delegate1.stream_id());
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Run until pushed stream is created.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(1u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  base::WeakPtr<SpdyStream> pushed_stream;
  int rv =
      session_->GetPushStream(GURL(kPushedUrl), &pushed_stream, BoundNetLog());
  ASSERT_EQ(OK, rv);
  ASSERT_TRUE(pushed_stream.get() != nullptr);
  test::StreamDelegateCloseOnHeaders delegate2(pushed_stream);
  pushed_stream->SetDelegate(&delegate2);

  // Receive headers for pushed stream. Delegate will cancel the stream, ensure
  // that all our counters are in consistent state.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, session_->num_active_streams());
  EXPECT_EQ(0u, session_->num_created_streams());
  EXPECT_EQ(0u, session_->num_pushed_streams());
  EXPECT_EQ(0u, session_->num_active_pushed_streams());

  // Read EOF.
  data.Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_P(SpdySessionTest, RejectInvalidUnknownFrames) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  MockRead reads[] = {
      MockRead(SYNCHRONOUS, ERR_IO_PENDING)  // Stall forever.
  };

  StaticSocketDataProvider data(reads, arraysize(reads), nullptr, 0);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  CreateNetworkSession();
  CreateInsecureSpdySession();

  session_->stream_hi_water_mark_ = 5;
  // Low client (odd) ids are fine.
  EXPECT_TRUE(session_->OnUnknownFrame(3, 0));
  // Client id exceeding watermark.
  EXPECT_FALSE(session_->OnUnknownFrame(9, 0));

  session_->last_accepted_push_stream_id_ = 6;
  // Low server (even) ids are fine.
  EXPECT_TRUE(session_->OnUnknownFrame(2, 0));
  // Server id exceeding last accepted id.
  EXPECT_FALSE(session_->OnUnknownFrame(8, 0));
}

TEST(MapFramerErrorToProtocolError, MapsValues) {
  CHECK_EQ(
      SPDY_ERROR_INVALID_CONTROL_FRAME,
      MapFramerErrorToProtocolError(SpdyFramer::SPDY_INVALID_CONTROL_FRAME));
  CHECK_EQ(
      SPDY_ERROR_INVALID_DATA_FRAME_FLAGS,
      MapFramerErrorToProtocolError(SpdyFramer::SPDY_INVALID_DATA_FRAME_FLAGS));
  CHECK_EQ(
      SPDY_ERROR_GOAWAY_FRAME_CORRUPT,
      MapFramerErrorToProtocolError(SpdyFramer::SPDY_GOAWAY_FRAME_CORRUPT));
  CHECK_EQ(SPDY_ERROR_UNEXPECTED_FRAME,
           MapFramerErrorToProtocolError(SpdyFramer::SPDY_UNEXPECTED_FRAME));
}

TEST(MapFramerErrorToNetError, MapsValue) {
  CHECK_EQ(ERR_SPDY_PROTOCOL_ERROR,
           MapFramerErrorToNetError(SpdyFramer::SPDY_INVALID_CONTROL_FRAME));
  CHECK_EQ(ERR_SPDY_COMPRESSION_ERROR,
           MapFramerErrorToNetError(SpdyFramer::SPDY_COMPRESS_FAILURE));
  CHECK_EQ(ERR_SPDY_COMPRESSION_ERROR,
           MapFramerErrorToNetError(SpdyFramer::SPDY_DECOMPRESS_FAILURE));
  CHECK_EQ(
      ERR_SPDY_FRAME_SIZE_ERROR,
      MapFramerErrorToNetError(SpdyFramer::SPDY_CONTROL_PAYLOAD_TOO_LARGE));
}

TEST(MapRstStreamStatusToProtocolError, MapsValues) {
  CHECK_EQ(STATUS_CODE_PROTOCOL_ERROR,
           MapRstStreamStatusToProtocolError(RST_STREAM_PROTOCOL_ERROR));
  CHECK_EQ(STATUS_CODE_FRAME_SIZE_ERROR,
           MapRstStreamStatusToProtocolError(RST_STREAM_FRAME_SIZE_ERROR));
  CHECK_EQ(STATUS_CODE_ENHANCE_YOUR_CALM,
           MapRstStreamStatusToProtocolError(RST_STREAM_ENHANCE_YOUR_CALM));
  CHECK_EQ(STATUS_CODE_INADEQUATE_SECURITY,
           MapRstStreamStatusToProtocolError(RST_STREAM_INADEQUATE_SECURITY));
  CHECK_EQ(STATUS_CODE_HTTP_1_1_REQUIRED,
           MapRstStreamStatusToProtocolError(RST_STREAM_HTTP_1_1_REQUIRED));
}

TEST(MapNetErrorToGoAwayStatus, MapsValue) {
  CHECK_EQ(GOAWAY_INADEQUATE_SECURITY,
           MapNetErrorToGoAwayStatus(ERR_SPDY_INADEQUATE_TRANSPORT_SECURITY));
  CHECK_EQ(GOAWAY_FLOW_CONTROL_ERROR,
           MapNetErrorToGoAwayStatus(ERR_SPDY_FLOW_CONTROL_ERROR));
  CHECK_EQ(GOAWAY_PROTOCOL_ERROR,
           MapNetErrorToGoAwayStatus(ERR_SPDY_PROTOCOL_ERROR));
  CHECK_EQ(GOAWAY_COMPRESSION_ERROR,
           MapNetErrorToGoAwayStatus(ERR_SPDY_COMPRESSION_ERROR));
  CHECK_EQ(GOAWAY_FRAME_SIZE_ERROR,
           MapNetErrorToGoAwayStatus(ERR_SPDY_FRAME_SIZE_ERROR));
  CHECK_EQ(GOAWAY_PROTOCOL_ERROR, MapNetErrorToGoAwayStatus(ERR_UNEXPECTED));
}

TEST(CanPoolTest, CanPool) {
  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com

  TransportSecurityState tss;
  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");

  EXPECT_TRUE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "www.example.org"));
  EXPECT_TRUE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
  EXPECT_TRUE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.com"));
  EXPECT_FALSE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.google.com"));
}

TEST(CanPoolTest, CanNotPoolWithCertErrors) {
  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com

  TransportSecurityState tss;
  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");
  ssl_info.cert_status = CERT_STATUS_REVOKED;

  EXPECT_FALSE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
}

TEST(CanPoolTest, CanNotPoolWithClientCerts) {
  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com

  TransportSecurityState tss;
  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");
  ssl_info.client_cert_sent = true;

  EXPECT_FALSE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
}

TEST(CanPoolTest, CanNotPoolAcrossETLDsWithChannelID) {
  // Load a cert that is valid for:
  //   www.example.org
  //   mail.example.org
  //   www.example.com

  TransportSecurityState tss;
  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");
  ssl_info.channel_id_sent = true;

  EXPECT_TRUE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
  EXPECT_FALSE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "www.example.com"));
}

TEST(CanPoolTest, CanNotPoolWithBadPins) {
  uint8_t primary_pin = 1;
  uint8_t backup_pin = 2;
  uint8_t bad_pin = 3;
  TransportSecurityState tss;
  test::AddPin(&tss, "mail.example.org", primary_pin, backup_pin);

  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");
  ssl_info.is_issued_by_known_root = true;
  ssl_info.public_key_hashes.push_back(test::GetTestHashValue(bad_pin));

  EXPECT_FALSE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
}

TEST(CanPoolTest, CanPoolWithAcceptablePins) {
  uint8_t primary_pin = 1;
  uint8_t backup_pin = 2;
  TransportSecurityState tss;
  test::AddPin(&tss, "mail.example.org", primary_pin, backup_pin);

  SSLInfo ssl_info;
  ssl_info.cert = ImportCertFromFile(GetTestCertsDirectory(),
                                     "spdy_pooling.pem");
  ssl_info.is_issued_by_known_root = true;
  ssl_info.public_key_hashes.push_back(test::GetTestHashValue(primary_pin));

  EXPECT_TRUE(SpdySession::CanPool(
      &tss, ssl_info, "www.example.org", "mail.example.org"));
}

}  // namespace net
