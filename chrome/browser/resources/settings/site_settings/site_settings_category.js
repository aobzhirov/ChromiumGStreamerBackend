// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Define a global boolean for notifications (only enabled in the test class).
cr.exportPath('settings_test');

/** @type {boolean} */
settings_test.siteCategoryNotifyForTest;

/**
 * @fileoverview
 * 'site-settings-category' is the polymer element for showing a certain
 * category under Site Settings.
 */
Polymer({
  is: 'site-settings-category',

  behaviors: [SiteSettingsBehavior, WebUIListenerBehavior],

  properties: {
    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    /**
     * Represents the state of the main toggle shown for the category. For
     * example, the Location category can be set to Block/Ask so false, in that
     * case, represents Block and true represents Ask.
     */
    categoryEnabled: {
      type: Boolean,
      notify: true,  // !!settings_test.siteCategoryNotifyForTest,
    },

    /**
     * The site that was selected by the user in the dropdown list.
     * @type {SiteException}
     */
    selectedSite: {
      type: Object,
      notify: true,
    },

    /**
     * Whether to show the '(recommended)' label prefix for permissions.
     */
    showRecommendation: {
      type: Boolean,
      value: true,
    },
  },

  observers: [
    'onCategoryChanged_(category)',
  ],

  ready: function() {
    this.$.blockList.categorySubtype = settings.PermissionValues.BLOCK;
    this.$.allowList.categorySubtype = settings.PermissionValues.ALLOW;

    this.prefsProxy_ = settings.SiteSettingsPrefsBrowserProxyImpl.getInstance();
    this.addWebUIListener('contentSettingCategoryChanged',
        this.defaultValueForCategoryChanged_.bind(this));
  },

  /**
   * Called when the default value for a category has been changed.
   * @param {number} category The category that changed.
   * @private
   */
  defaultValueForCategoryChanged_: function(category) {
    if (category == this.category)
      this.onCategoryChanged_();
  },

  /**
   * A handler for flipping the toggle value.
   * @private
   */
  onToggleChange_: function(event) {
    switch (this.category) {
      case settings.ContentSettingsTypes.COOKIES:
      case settings.ContentSettingsTypes.IMAGES:
      case settings.ContentSettingsTypes.JAVASCRIPT:
      case settings.ContentSettingsTypes.POPUPS:
        // "Allowed" vs "Blocked".
        this.prefsProxy_.setDefaultValueForContentType(
            this.category,
            this.categoryEnabled ?
                settings.PermissionValues.ALLOW :
                settings.PermissionValues.BLOCK);
        break;
      case settings.ContentSettingsTypes.NOTIFICATIONS:
      case settings.ContentSettingsTypes.GEOLOCATION:
      case settings.ContentSettingsTypes.CAMERA:
      case settings.ContentSettingsTypes.MIC:
        // "Ask" vs "Blocked".
        this.prefsProxy_.setDefaultValueForContentType(
            this.category,
            this.categoryEnabled ?
                settings.PermissionValues.ASK :
                settings.PermissionValues.BLOCK);
        break;
      case settings.ContentSettingsTypes.FULLSCREEN:
        // "Allowed" vs. "Ask first".
        this.prefsProxy_.setDefaultValueForContentType(
            this.category,
            this.categoryEnabled ?
                settings.PermissionValues.ALLOW :
                settings.PermissionValues.ASK);
        break;
      default:
        assertNotReached();
    }
  },

  /**
   * Handles changes to the category pref and the |category| member variable.
   * @private
   */
  onCategoryChanged_: function() {
    settings.SiteSettingsPrefsBrowserProxyImpl.getInstance()
        .getDefaultValueForContentType(
            this.category).then(function(enabled) {
              this.categoryEnabled = enabled;
            }.bind(this));
  },
});
