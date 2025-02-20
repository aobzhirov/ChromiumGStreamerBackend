// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview An element that represents an SSL certificate entry.
 */
Polymer({
  is: 'settings-certificate-entry',

  properties: {
    /** @type {!Certificate} */
    model: Object,

    /** @type {!settings.CertificateType} */
    certificateType: String,
  },
});
