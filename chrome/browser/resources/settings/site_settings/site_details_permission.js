// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'site-details-permission' handles showing the state of one permission, such
 * as Geolocation, for a given origin.
 */
Polymer({
  is: 'site-details-permission',

  behaviors: [SiteSettingsBehavior, WebUIListenerBehavior],

  properties: {
    /**
     * The site that this widget is showing details for.
     * @type {SiteException}
     */
    site: {
      type: Object,
      observer: 'siteChanged_',
    },

    i18n_: {
      readOnly: true,
      type: Object,
      value: function() {
        return {
          allowAction: loadTimeData.getString('siteSettingsActionAllow'),
          blockAction: loadTimeData.getString('siteSettingsActionBlock'),
        };
      },
    },
  },

  /** @override */
  attached: function() {
    this.addWebUIListener('contentSettingSitePermissionChanged',
        this.sitePermissionChanged_.bind(this));
  },

  /**
   * Sets the site to display.
   * @param {!SiteException} site The site to display.
   * @private
   */
  siteChanged_: function(site) {
    this.$.details.hidden = true;

    var prefsProxy = settings.SiteSettingsPrefsBrowserProxyImpl.getInstance();
    prefsProxy.getExceptionList(this.category).then(function(exceptionList) {
      for (var i = 0; i < exceptionList.length; ++i) {
        if (exceptionList[i].origin == site.origin) {
          // TODO(finnur): Convert to use attrForSelected.
          this.$.permission.selected =
              exceptionList[i].setting == 'allow' ? 0 : 1;
          this.$.details.hidden = false;
        }
      }
    }.bind(this));
  },

  /**
   * Called when a site within a category has been changed.
   * @param {number} category The category that changed.
   * @param {string} site The site that changed.
   * @private
   */
  sitePermissionChanged_: function(category, site) {
    if (category == this.category && (site == '' || site == this.site.origin)) {
      // TODO(finnur): Send down the full SiteException, not just a string.
      this.siteChanged_({
        origin: site,
        embeddingOrigin: '',
        setting: '',
        source: '',
      });
    }
  },

  /**
   * Resets the category permission for this origin.
   */
  resetPermission: function() {
    this.resetCategoryPermissionForOrigin(this.site.origin, '', this.category);
    this.$.details.hidden = true;
  },

  /**
   * Handles the category permission changing for this origin.
   * @param {!{detail: !{item: !{innerText: string}}}} event
   */
  onPermissionMenuIronActivate_: function(event) {
    // TODO(finnur): Compare with event.detail.item.dataset.permission directly
    //     once attrForSelected is in use.
    var action = event.detail.item.innerText;
    var value = (action == this.i18n_.allowAction) ?
        settings.PermissionValues.ALLOW :
        settings.PermissionValues.BLOCK;
    this.setCategoryPermissionForOrigin(
        this.site.origin, '', value, this.category);
  },
});
