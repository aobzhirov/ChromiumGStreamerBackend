// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_QUIRKS_QUIRKS_MANAGER_H_
#define COMPONENTS_QUIRKS_QUIRKS_MANAGER_H_

#include <set>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "components/quirks/quirks_export.h"

class GURL;
class PrefRegistrySimple;
class PrefService;

namespace base {
class SequencedWorkerPool;
}

namespace net {
class URLFetcher;
class URLFetcherDelegate;
class URLRequestContextGetter;
}

namespace quirks {

class QuirksClient;

// Callback when Quirks path request is complete.
// First parameter - path found, or empty if no file.
// Second parameter - true if file was just downloaded.
using RequestFinishedCallback =
    base::Callback<void(const base::FilePath&, bool)>;

// Format int as hex string for filename.
QUIRKS_EXPORT std::string IdToHexString(int64_t product_id);

// Append ".icc" to hex string in filename.
QUIRKS_EXPORT std::string IdToFileName(int64_t product_id);

// Manages downloads of and requests for hardware calibration and configuration
// files ("Quirks").  The manager presents an external Quirks API, handles
// needed components from browser (local preferences, url context getter,
// blocking pool, etc), and owns clients and manages their life cycles.
class QUIRKS_EXPORT QuirksManager {
 public:
  // Passed function to create a URLFetcher for tests.
  // Same parameters as URLFetcher::Create().
  using FakeQuirksFetcherCreator =
      base::Callback<scoped_ptr<net::URLFetcher>(const GURL&,
                                                 net::URLFetcherDelegate*)>;

  // Callback after getting days since OOBE on blocking pool.
  // Parameter is returned number of days.
  using DaysSinceOobeCallback = base::Callback<void(int)>;

  // Delegate class, so implementation can access browser functionality.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Provides Chrome API key for quirks server.
    virtual std::string GetApiKey() const = 0;

    // Returns the read-only directory where icc files were added before the
    // Quirks Client provided them.
    virtual base::FilePath GetBuiltInDisplayProfileDirectory() const = 0;

    // Returns the path to the writable display profile directory.
    // This directory must already exist.
    virtual base::FilePath GetDownloadDisplayProfileDirectory() const = 0;

    // Gets days since first login, returned via callback.
    virtual void GetDaysSinceOobe(DaysSinceOobeCallback callback) const = 0;

   private:
    DISALLOW_ASSIGN(Delegate);
  };

  static void Initialize(
      scoped_ptr<Delegate> delegate,
      scoped_refptr<base::SequencedWorkerPool> blocking_pool,
      PrefService* local_state,
      scoped_refptr<net::URLRequestContextGetter> url_context_getter);
  static void Shutdown();
  static QuirksManager* Get();

  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Signal to start queued downloads after login.
  void OnLoginCompleted();

  // Entry point into manager.  Finds or downloads icc file.
  void RequestIccProfilePath(
      int64_t product_id,
      const RequestFinishedCallback& on_request_finished);

  void ClientFinished(QuirksClient* client);

  // Creates a real URLFetcher for OS, and a fake one for tests.
  scoped_ptr<net::URLFetcher> CreateURLFetcher(
      const GURL& url,
      net::URLFetcherDelegate* delegate);

  Delegate* delegate() { return delegate_.get(); }
  base::SequencedWorkerPool* blocking_pool() { return blocking_pool_.get(); }
  net::URLRequestContextGetter* url_context_getter() {
    return url_context_getter_.get();
  }

 protected:
  friend class QuirksBrowserTest;

  void SetFakeQuirksFetcherCreatorForTests(
      const FakeQuirksFetcherCreator& creator) {
    fake_quirks_fetcher_creator_ = creator;
  }

 private:
  QuirksManager(scoped_ptr<Delegate> delegate,
                scoped_refptr<base::SequencedWorkerPool> blocking_pool,
                PrefService* local_state,
                scoped_refptr<net::URLRequestContextGetter> url_context_getter);
  ~QuirksManager();

  // Callback after checking for existing icc file; proceed if not found.
  void OnIccFilePathRequestCompleted(
      int64_t product_id,
      const RequestFinishedCallback& on_request_finished,
      base::FilePath path);

  // Callback after checking OOBE date; launch client if appropriate.
  void OnDaysSinceOobeReceived(
      int64_t product_id,
      const RequestFinishedCallback& on_request_finished,
      int days_since_oobe);

  // Create and start a client to download file.
  void CreateClient(int64_t product_id,
                    const RequestFinishedCallback& on_request_finished);

  // Records time of most recent server check.
  void SetLastServerCheck(int64_t product_id, const base::Time& last_check);

  // Set of active clients, each created to download a different Quirks file.
  std::set<scoped_ptr<QuirksClient>> clients_;

  // Don't start downloads before first session login.
  bool waiting_for_login_;

  // Ensure this class runs on a single thread.
  base::ThreadChecker thread_checker_;

  // These objects provide resources from the browser.
  scoped_ptr<Delegate> delegate_;  // Impl runs from chrome/browser.
  scoped_refptr<base::SequencedWorkerPool> blocking_pool_;
  PrefService* local_state_;  // For local prefs.
  scoped_refptr<net::URLRequestContextGetter> url_context_getter_;

  FakeQuirksFetcherCreator fake_quirks_fetcher_creator_;  // For tests.

  // Factory for callbacks.
  base::WeakPtrFactory<QuirksManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuirksManager);
};

}  // namespace quirks

#endif  // COMPONENTS_QUIRKS_QUIRKS_MANAGER_H_
