// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/web/webui/url_data_manager_ios_backend.h"

#include <set>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "ios/web/public/browser_state.h"
#include "ios/web/public/web_client.h"
#include "ios/web/public/web_thread.h"
#include "ios/web/webui/shared_resources_data_source_ios.h"
#include "ios/web/webui/url_data_source_ios_impl.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_job_factory.h"
#include "url/url_util.h"

using web::WebThread;

namespace web {

namespace {

// TODO(tsepez) remove unsafe-eval when bidichecker_packaged.js fixed.
const char kChromeURLContentSecurityPolicyHeaderBase[] =
    "Content-Security-Policy: script-src chrome://resources "
    "'self' 'unsafe-eval'; ";

const char kChromeURLXFrameOptionsHeader[] = "X-Frame-Options: DENY";

bool SchemeIsInSchemes(const std::string& scheme,
                       const std::vector<std::string>& schemes) {
  return std::find(schemes.begin(), schemes.end(), scheme) != schemes.end();
}

// Returns whether |url| passes some sanity checks and is a valid GURL.
bool CheckURLIsValid(const GURL& url) {
  std::vector<std::string> additional_schemes;
  DCHECK(GetWebClient()->IsAppSpecificURL(url) ||
         (GetWebClient()->GetAdditionalWebUISchemes(&additional_schemes),
          SchemeIsInSchemes(url.scheme(), additional_schemes)));

  if (!url.is_valid()) {
    NOTREACHED();
    return false;
  }

  return true;
}

// Parse |url| to get the path which will be used to resolve the request. The
// path is the remaining portion after the scheme and hostname.
void URLToRequestPath(const GURL& url, std::string* path) {
  const std::string& spec = url.possibly_invalid_spec();
  const url::Parsed& parsed = url.parsed_for_possibly_invalid_spec();
  // + 1 to skip the slash at the beginning of the path.
  int offset = parsed.CountCharactersBefore(url::Parsed::PATH, false) + 1;

  if (offset < static_cast<int>(spec.size()))
    path->assign(spec.substr(offset));
}

}  // namespace

// URLRequestChromeJob is a net::URLRequestJob that manages running
// chrome-internal resource requests asynchronously.
// It hands off URL requests to ChromeURLDataManagerIOS, which asynchronously
// calls back once the data is available.
class URLRequestChromeJob : public net::URLRequestJob {
 public:
  // |is_incognito| set when job is generated from an incognito profile.
  URLRequestChromeJob(net::URLRequest* request,
                      net::NetworkDelegate* network_delegate,
                      BrowserState* browser_state,
                      bool is_incognito);

  // net::URLRequestJob implementation.
  void Start() override;
  void Kill() override;
  int ReadRawData(net::IOBuffer* buf, int buf_size) override;
  bool GetMimeType(std::string* mime_type) const override;
  int GetResponseCode() const override;
  void GetResponseInfo(net::HttpResponseInfo* info) override;

  // Used to notify that the requested data's |mime_type| is ready.
  void MimeTypeAvailable(const std::string& mime_type);

  // Called by ChromeURLDataManagerIOS to notify us that the data blob is ready
  // for us.
  void DataAvailable(base::RefCountedMemory* bytes);

  void set_mime_type(const std::string& mime_type) { mime_type_ = mime_type; }

  void set_allow_caching(bool allow_caching) { allow_caching_ = allow_caching; }

  void set_add_content_security_policy(bool add_content_security_policy) {
    add_content_security_policy_ = add_content_security_policy;
  }

  void set_content_security_policy_object_source(const std::string& data) {
    content_security_policy_object_source_ = data;
  }

  void set_content_security_policy_frame_source(const std::string& data) {
    content_security_policy_frame_source_ = data;
  }

  void set_deny_xframe_options(bool deny_xframe_options) {
    deny_xframe_options_ = deny_xframe_options;
  }

  void set_send_content_type_header(bool send_content_type_header) {
    send_content_type_header_ = send_content_type_header;
  }

  // Returns true when job was generated from an incognito profile.
  bool is_incognito() const { return is_incognito_; }

 private:
  friend class URLDataManagerIOSBackend;

  ~URLRequestChromeJob() override;

  // Do the actual copy from data_ (the data we're serving) into |buf|.
  // Separate from ReadRawData so we can handle async I/O.
  int CompleteRead(net::IOBuffer* buf, int buf_size);

  // The actual data we're serving.  NULL until it's been fetched.
  scoped_refptr<base::RefCountedMemory> data_;
  // The current offset into the data that we're handing off to our
  // callers via the Read interfaces.
  int data_offset_;

  // For async reads, we keep around a pointer to the buffer that
  // we're reading into.
  scoped_refptr<net::IOBuffer> pending_buf_;
  int pending_buf_size_;
  std::string mime_type_;

  // If true, set a header in the response to prevent it from being cached.
  bool allow_caching_;

  // If true, set the Content Security Policy (CSP) header.
  bool add_content_security_policy_;

  // These are used with the CSP.
  std::string content_security_policy_object_source_;
  std::string content_security_policy_frame_source_;

  // If true, sets  the "X-Frame-Options: DENY" header.
  bool deny_xframe_options_;

  // If true, sets the "Content-Type: <mime-type>" header.
  bool send_content_type_header_;

  // True when job is generated from an incognito profile.
  const bool is_incognito_;

  // The BrowserState with which this job is associated.
  BrowserState* browser_state_;

  // The backend is owned by the BrowserState and always outlives us. It is
  // obtained from the BrowserState on the IO thread.
  URLDataManagerIOSBackend* backend_;

  base::WeakPtrFactory<URLRequestChromeJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestChromeJob);
};

URLRequestChromeJob::URLRequestChromeJob(net::URLRequest* request,
                                         net::NetworkDelegate* network_delegate,
                                         BrowserState* browser_state,
                                         bool is_incognito)
    : net::URLRequestJob(request, network_delegate),
      data_offset_(0),
      pending_buf_size_(0),
      allow_caching_(true),
      add_content_security_policy_(true),
      content_security_policy_object_source_("object-src 'none';"),
      content_security_policy_frame_source_("frame-src 'none';"),
      deny_xframe_options_(true),
      send_content_type_header_(false),
      is_incognito_(is_incognito),
      browser_state_(browser_state),
      backend_(NULL),
      weak_factory_(this) {
  DCHECK(browser_state_);
}

URLRequestChromeJob::~URLRequestChromeJob() {
  if (backend_) {
    CHECK(!backend_->HasPendingJob(this));
  }
}

void URLRequestChromeJob::Start() {
  TRACE_EVENT_ASYNC_BEGIN1("browser", "DataManager:Request", this, "URL",
                           request_->url().possibly_invalid_spec());

  if (!request_)
    return;
  DCHECK(browser_state_);

  // Obtain the URLDataManagerIOSBackend instance that is associated with
  // |browser_state_|. Note that this *must* be done on the IO thread.
  backend_ = browser_state_->GetURLDataManagerIOSBackendOnIOThread();
  DCHECK(backend_);

  if (!backend_->StartRequest(request_, this)) {
    NotifyStartError(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                           net::ERR_INVALID_URL));
  }
}

void URLRequestChromeJob::Kill() {
  weak_factory_.InvalidateWeakPtrs();
  if (backend_)
    backend_->RemoveRequest(this);
  URLRequestJob::Kill();
}

bool URLRequestChromeJob::GetMimeType(std::string* mime_type) const {
  *mime_type = mime_type_;
  return !mime_type_.empty();
}

int URLRequestChromeJob::GetResponseCode() const {
  return net::HTTP_OK;
}

void URLRequestChromeJob::GetResponseInfo(net::HttpResponseInfo* info) {
  DCHECK(!info->headers.get());
  // Set the headers so that requests serviced by ChromeURLDataManagerIOS
  // return a status code of 200. Without this they return a 0, which makes the
  // status indistiguishable from other error types. Instant relies on getting
  // a 200.
  info->headers = new net::HttpResponseHeaders("HTTP/1.1 200 OK");

  // Determine the least-privileged content security policy header, if any,
  // that is compatible with a given WebUI URL, and append it to the existing
  // response headers.
  if (add_content_security_policy_) {
    std::string base = kChromeURLContentSecurityPolicyHeaderBase;
    base.append(content_security_policy_object_source_);
    base.append(content_security_policy_frame_source_);
    info->headers->AddHeader(base);
  }

  if (deny_xframe_options_)
    info->headers->AddHeader(kChromeURLXFrameOptionsHeader);

  if (!allow_caching_)
    info->headers->AddHeader("Cache-Control: no-cache");

  if (send_content_type_header_ && !mime_type_.empty()) {
    std::string content_type = base::StringPrintf(
        "%s:%s", net::HttpRequestHeaders::kContentType, mime_type_.c_str());
    info->headers->AddHeader(content_type);
  }
}

void URLRequestChromeJob::MimeTypeAvailable(const std::string& mime_type) {
  set_mime_type(mime_type);
  NotifyHeadersComplete();
}

void URLRequestChromeJob::DataAvailable(base::RefCountedMemory* bytes) {
  TRACE_EVENT_ASYNC_END0("browser", "DataManager:Request", this);
  if (bytes) {
    data_ = bytes;
    if (pending_buf_.get()) {
      CHECK(pending_buf_->data());
      int rv = CompleteRead(pending_buf_.get(), pending_buf_size_);
      pending_buf_ = NULL;
      ReadRawDataComplete(rv);
    }
  } else {
    ReadRawDataComplete(net::ERR_FAILED);
  }
}

int URLRequestChromeJob::ReadRawData(net::IOBuffer* buf, int buf_size) {
  if (!data_.get()) {
    DCHECK(!pending_buf_.get());
    CHECK(buf->data());
    pending_buf_ = buf;
    pending_buf_size_ = buf_size;
    return net::ERR_IO_PENDING;  // Tell the caller we're still waiting for
                                 // data.
  }

  // Otherwise, the data is available.
  return CompleteRead(buf, buf_size);
}

int URLRequestChromeJob::CompleteRead(net::IOBuffer* buf, int buf_size) {
  // http://crbug.com/373841
  char url_buf[128];
  base::strlcpy(url_buf, request_->url().spec().c_str(), arraysize(url_buf));
  base::debug::Alias(url_buf);

  int remaining = data_->size() - data_offset_;
  if (buf_size > remaining)
    buf_size = remaining;
  if (buf_size > 0) {
    memcpy(buf->data(), data_->front() + data_offset_, buf_size);
    data_offset_ += buf_size;
  }
  return buf_size;
}

namespace {

// Gets mime type for data that is available from |source| by |path|.
// After that, notifies |job| that mime type is available. This method
// should be called on the UI thread, but notification is performed on
// the IO thread.
void GetMimeTypeOnUI(URLDataSourceIOSImpl* source,
                     const std::string& path,
                     const base::WeakPtr<URLRequestChromeJob>& job) {
  DCHECK_CURRENTLY_ON(WebThread::UI);
  std::string mime_type = source->source()->GetMimeType(path);
  WebThread::PostTask(
      WebThread::IO, FROM_HERE,
      base::Bind(&URLRequestChromeJob::MimeTypeAvailable, job, mime_type));
}

}  // namespace

namespace {

class ChromeProtocolHandler
    : public net::URLRequestJobFactory::ProtocolHandler {
 public:
  // |is_incognito| should be set for incognito profiles.
  ChromeProtocolHandler(BrowserState* browser_state, bool is_incognito)
      : browser_state_(browser_state), is_incognito_(is_incognito) {}
  ~ChromeProtocolHandler() override {}

  net::URLRequestJob* MaybeCreateJob(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const override {
    DCHECK(request);

    return new URLRequestChromeJob(request, network_delegate, browser_state_,
                                   is_incognito_);
  }

  bool IsSafeRedirectTarget(const GURL& location) const override {
    return false;
  }

 private:
  BrowserState* browser_state_;

  // True when generated from an incognito profile.
  const bool is_incognito_;

  DISALLOW_COPY_AND_ASSIGN(ChromeProtocolHandler);
};

}  // namespace

URLDataManagerIOSBackend::URLDataManagerIOSBackend() : next_request_id_(0) {
  URLDataSourceIOS* shared_source = new SharedResourcesDataSourceIOS();
  URLDataSourceIOSImpl* source_impl =
      new URLDataSourceIOSImpl(shared_source->GetSource(), shared_source);
  AddDataSource(source_impl);
}

URLDataManagerIOSBackend::~URLDataManagerIOSBackend() {
  for (DataSourceMap::iterator i = data_sources_.begin();
       i != data_sources_.end(); ++i) {
    i->second->backend_ = NULL;
  }
  data_sources_.clear();
}

// static
scoped_ptr<net::URLRequestJobFactory::ProtocolHandler>
URLDataManagerIOSBackend::CreateProtocolHandler(BrowserState* browser_state) {
  DCHECK(browser_state);
  return make_scoped_ptr(new ChromeProtocolHandler(
      browser_state, browser_state->IsOffTheRecord()));
}

void URLDataManagerIOSBackend::AddDataSource(URLDataSourceIOSImpl* source) {
  DCHECK_CURRENTLY_ON(WebThread::IO);
  DataSourceMap::iterator i = data_sources_.find(source->source_name());
  if (i != data_sources_.end()) {
    if (!source->source()->ShouldReplaceExistingSource())
      return;
    i->second->backend_ = NULL;
  }
  data_sources_[source->source_name()] = source;
  source->backend_ = this;
}

bool URLDataManagerIOSBackend::HasPendingJob(URLRequestChromeJob* job) const {
  for (PendingRequestMap::const_iterator i = pending_requests_.begin();
       i != pending_requests_.end(); ++i) {
    if (i->second == job)
      return true;
  }
  return false;
}

bool URLDataManagerIOSBackend::StartRequest(const net::URLRequest* request,
                                            URLRequestChromeJob* job) {
  if (!CheckURLIsValid(request->url()))
    return false;

  URLDataSourceIOSImpl* source = GetDataSourceFromURL(request->url());
  if (!source)
    return false;

  if (!source->source()->ShouldServiceRequest(request))
    return false;

  std::string path;
  URLToRequestPath(request->url(), &path);
  source->source()->WillServiceRequest(request, &path);

  // Save this request so we know where to send the data.
  RequestID request_id = next_request_id_++;
  pending_requests_.insert(std::make_pair(request_id, job));

  job->set_allow_caching(source->source()->AllowCaching());
  job->set_add_content_security_policy(true);
  job->set_content_security_policy_object_source(
      source->source()->GetContentSecurityPolicyObjectSrc());
  job->set_content_security_policy_frame_source("frame-src 'none';");
  job->set_deny_xframe_options(source->source()->ShouldDenyXFrameOptions());
  job->set_send_content_type_header(false);

  // Forward along the request to the data source.
  // URLRequestChromeJob should receive mime type before data. This
  // is guaranteed because request for mime type is placed in the
  // message loop before request for data. And correspondingly their
  // replies are put on the IO thread in the same order.
  base::MessageLoop* target_message_loop =
      web::WebThread::UnsafeGetMessageLoopForThread(web::WebThread::UI);
  target_message_loop->PostTask(
      FROM_HERE, base::Bind(&GetMimeTypeOnUI, base::RetainedRef(source), path,
                            job->weak_factory_.GetWeakPtr()));

  target_message_loop->PostTask(
      FROM_HERE, base::Bind(&URLDataManagerIOSBackend::CallStartRequest,
                            make_scoped_refptr(source), path, request_id));
  return true;
}

URLDataSourceIOSImpl* URLDataManagerIOSBackend::GetDataSourceFromURL(
    const GURL& url) {
  // The input usually looks like: chrome://source_name/extra_bits?foo
  // so do a lookup using the host of the URL.
  DataSourceMap::iterator i = data_sources_.find(url.host());
  if (i != data_sources_.end())
    return i->second.get();

  // No match using the host of the URL, so do a lookup using the scheme for
  // URLs on the form source_name://extra_bits/foo .
  i = data_sources_.find(url.scheme() + "://");
  if (i != data_sources_.end())
    return i->second.get();

  // No matches found, so give up.
  return NULL;
}

void URLDataManagerIOSBackend::CallStartRequest(
    scoped_refptr<URLDataSourceIOSImpl> source,
    const std::string& path,
    int request_id) {
  source->source()->StartDataRequest(
      path,
      base::Bind(&URLDataSourceIOSImpl::SendResponse, source, request_id));
}

void URLDataManagerIOSBackend::RemoveRequest(URLRequestChromeJob* job) {
  // Remove the request from our list of pending requests.
  // If/when the source sends the data that was requested, the data will just
  // be thrown away.
  for (PendingRequestMap::iterator i = pending_requests_.begin();
       i != pending_requests_.end(); ++i) {
    if (i->second == job) {
      pending_requests_.erase(i);
      return;
    }
  }
}

void URLDataManagerIOSBackend::DataAvailable(RequestID request_id,
                                             base::RefCountedMemory* bytes) {
  // Forward this data on to the pending net::URLRequest, if it exists.
  PendingRequestMap::iterator i = pending_requests_.find(request_id);
  if (i != pending_requests_.end()) {
    URLRequestChromeJob* job(i->second);
    pending_requests_.erase(i);
    job->DataAvailable(bytes);
  }
}

}  // namespace web
