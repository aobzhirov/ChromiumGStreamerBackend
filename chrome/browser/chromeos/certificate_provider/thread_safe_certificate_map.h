// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_THREAD_SAFE_CERTIFICATE_MAP_H_
#define CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_THREAD_SAFE_CERTIFICATE_MAP_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/chromeos/certificate_provider/certificate_info.h"

namespace net {
class SHA256HashValueLessThan;
class X509Certificate;
struct SHA256HashValue;
}

namespace chromeos {
namespace certificate_provider {

class ThreadSafeCertificateMap {
 public:
  struct MapValue {
    MapValue(const CertificateInfo& cert_info, const std::string& extension_id);
    ~MapValue();

    CertificateInfo cert_info;
    std::string extension_id;
  };
  using FingerprintToCertAndExtensionMap =
      std::map<net::SHA256HashValue,
               scoped_ptr<MapValue>,
               net::SHA256HashValueLessThan>;

  ThreadSafeCertificateMap();
  ~ThreadSafeCertificateMap();

  // Updates the stored certificates with the given mapping from extension ids
  // to certificates.
  void Update(const std::map<std::string, CertificateInfoList>&
                  extension_to_certificates);

  // Looks up the given certificate. If the certificate was added by any
  // previous Update() call, returns true.
  // If this certificate was provided in the most recent Update() call,
  // |is_currently_provided| will be set to true, |extension_id| be set to that
  // extension's id and |info| will be set to the stored info. Otherwise, if
  // this certificate was not provided in the the most recent Update() call,
  // sets |is_currently_provided| to false and doesn't modify |info| and
  // |extension_id|.
  bool LookUpCertificate(const net::X509Certificate& cert,
                         bool* is_currently_provided,
                         CertificateInfo* info,
                         std::string* extension_id);

  // Remove every association of stored certificates to the given extension.
  // The certificates themselves will be remembered.
  void RemoveExtension(const std::string& extension_id);

 private:
  base::Lock lock_;
  FingerprintToCertAndExtensionMap fingerprint_to_cert_and_extension_;

  DISALLOW_COPY_AND_ASSIGN(ThreadSafeCertificateMap);
};

}  // namespace certificate_provider
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_THREAD_SAFE_CERTIFICATE_MAP_H_
