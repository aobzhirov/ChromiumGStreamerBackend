// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_

#include <string>

#include "base/macros.h"
#include "base/observer_list.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/extensions/extension_action.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/extension_event_histogram_value.h"
#include "third_party/skia/include/core/SkColor.h"

namespace base {
class DictionaryValue;
}

namespace content {
class BrowserContext;
class WebContents;
}

namespace extensions {
class ExtensionPrefs;

class ExtensionActionAPI : public BrowserContextKeyedAPI {
 public:
  class Observer {
   public:
    // Called when there is a change to the given |extension_action|.
    // |web_contents| is the web contents that was affected, and
    // |browser_context| is the associated BrowserContext. (The latter is
    // included because ExtensionActionAPI is shared between normal and
    // incognito contexts, so |browser_context| may not equal
    // |browser_context_|.)
    virtual void OnExtensionActionUpdated(
        ExtensionAction* extension_action,
        content::WebContents* web_contents,
        content::BrowserContext* browser_context);

    // Called when there is a change to the extension action's visibility.
    virtual void OnExtensionActionVisibilityChanged(
        const std::string& extension_id,
        bool is_now_visible);

    // Called when the page actions have been refreshed do to a possible change
    // in count or visibility.
    virtual void OnPageActionsUpdated(content::WebContents* web_contents);

    // Called when the ExtensionActionAPI is shutting down, giving observers a
    // chance to unregister themselves if there is not a definitive lifecycle.
    virtual void OnExtensionActionAPIShuttingDown();

   protected:
    virtual ~Observer();
  };

  explicit ExtensionActionAPI(content::BrowserContext* context);
  ~ExtensionActionAPI() override;

  // Convenience method to get the instance for a profile.
  static ExtensionActionAPI* Get(content::BrowserContext* context);

  static BrowserContextKeyedAPIFactory<ExtensionActionAPI>*
      GetFactoryInstance();

  // Add or remove observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  bool GetBrowserActionVisibility(const std::string& extension_id);
  void SetBrowserActionVisibility(const std::string& extension_id,
                                  bool visible);

  // Opens the popup for the given |extension| in the given |browser|'s window.
  // If |grant_active_tab_permissions| is true, this grants the extension
  // activeTab (so this should only be done if this is through a direct user
  // action).
  bool ShowExtensionActionPopup(const Extension* extension,
                                Browser* browser,
                                bool grant_active_tab_permissions);

  // Notifies that there has been a change in the given |extension_action|.
  void NotifyChange(ExtensionAction* extension_action,
                    content::WebContents* web_contents,
                    content::BrowserContext* browser_context);

  // Dispatches the onClicked event for extension that owns the given action.
  void DispatchExtensionActionClicked(const ExtensionAction& extension_action,
                                      content::WebContents* web_contents);

  // Clears the values for all ExtensionActions for the tab associated with the
  // given |web_contents| (and signals that page actions changed).
  void ClearAllValuesForTab(content::WebContents* web_contents);

  // Notifies that the current set of page actions for |web_contents| has
  // changed, and signals the browser to update.
  void NotifyPageActionsChanged(content::WebContents* web_contents);

  void set_prefs_for_testing(ExtensionPrefs* prefs) {
    extension_prefs_ = prefs;
  }

 private:
  friend class BrowserContextKeyedAPIFactory<ExtensionActionAPI>;

  // Returns the associated extension prefs.
  ExtensionPrefs* GetExtensionPrefs();

  // The DispatchEvent methods forward events to the |context|'s event router.
  void DispatchEventToExtension(content::BrowserContext* context,
                                const std::string& extension_id,
                                events::HistogramValue histogram_value,
                                const std::string& event_name,
                                scoped_ptr<base::ListValue> event_args);

  // BrowserContextKeyedAPI implementation.
  void Shutdown() override;
  static const char* service_name() { return "ExtensionActionAPI"; }
  static const bool kServiceRedirectedInIncognito = true;

  base::ObserverList<Observer> observers_;

  content::BrowserContext* browser_context_;

  ExtensionPrefs* extension_prefs_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionActionAPI);
};

// Implementation of the browserAction and pageAction APIs.
//
// Divergent behaviour between the two is minimal (pageAction has required
// tabIds while browserAction's are optional, they have different internal
// browser notification requirements, and not all functions are defined for all
// APIs).
class ExtensionActionFunction : public ChromeSyncExtensionFunction {
 public:
  static bool ParseCSSColorString(const std::string& color_string,
                                  SkColor* result);

 protected:
  ExtensionActionFunction();
  ~ExtensionActionFunction() override;
  bool RunSync() override;
  virtual bool RunExtensionAction() = 0;

  bool ExtractDataFromArguments();
  void NotifyChange();
  bool SetVisible(bool visible);

  // All the extension action APIs take a single argument called details that
  // is a dictionary.
  base::DictionaryValue* details_;

  // The tab id the extension action function should apply to, if any, or
  // kDefaultTabId if none was specified.
  int tab_id_;

  // WebContents for |tab_id_| if one exists.
  content::WebContents* contents_;

  // The extension action for the current extension.
  ExtensionAction* extension_action_;
};

//
// Implementations of each extension action API.
//
// pageAction and browserAction bindings are created for these by extending them
// then declaring an EXTENSION_FUNCTION_NAME.
//

// show
class ExtensionActionShowFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionShowFunction() override {}
  bool RunExtensionAction() override;
};

// hide
class ExtensionActionHideFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionHideFunction() override {}
  bool RunExtensionAction() override;
};

// setIcon
class ExtensionActionSetIconFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionSetIconFunction() override {}
  bool RunExtensionAction() override;
};

// setTitle
class ExtensionActionSetTitleFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionSetTitleFunction() override {}
  bool RunExtensionAction() override;
};

// setPopup
class ExtensionActionSetPopupFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionSetPopupFunction() override {}
  bool RunExtensionAction() override;
};

// setBadgeText
class ExtensionActionSetBadgeTextFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionSetBadgeTextFunction() override {}
  bool RunExtensionAction() override;
};

// setBadgeBackgroundColor
class ExtensionActionSetBadgeBackgroundColorFunction
    : public ExtensionActionFunction {
 protected:
  ~ExtensionActionSetBadgeBackgroundColorFunction() override {}
  bool RunExtensionAction() override;
};

// getTitle
class ExtensionActionGetTitleFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionGetTitleFunction() override {}
  bool RunExtensionAction() override;
};

// getPopup
class ExtensionActionGetPopupFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionGetPopupFunction() override {}
  bool RunExtensionAction() override;
};

// getBadgeText
class ExtensionActionGetBadgeTextFunction : public ExtensionActionFunction {
 protected:
  ~ExtensionActionGetBadgeTextFunction() override {}
  bool RunExtensionAction() override;
};

// getBadgeBackgroundColor
class ExtensionActionGetBadgeBackgroundColorFunction
    : public ExtensionActionFunction {
 protected:
  ~ExtensionActionGetBadgeBackgroundColorFunction() override {}
  bool RunExtensionAction() override;
};

//
// browserAction.* aliases for supported browserAction APIs.
//

class BrowserActionSetIconFunction : public ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setIcon", BROWSERACTION_SETICON)

 protected:
  ~BrowserActionSetIconFunction() override {}
};

class BrowserActionSetTitleFunction : public ExtensionActionSetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setTitle", BROWSERACTION_SETTITLE)

 protected:
  ~BrowserActionSetTitleFunction() override {}
};

class BrowserActionSetPopupFunction : public ExtensionActionSetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setPopup", BROWSERACTION_SETPOPUP)

 protected:
  ~BrowserActionSetPopupFunction() override {}
};

class BrowserActionGetTitleFunction : public ExtensionActionGetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getTitle", BROWSERACTION_GETTITLE)

 protected:
  ~BrowserActionGetTitleFunction() override {}
};

class BrowserActionGetPopupFunction : public ExtensionActionGetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getPopup", BROWSERACTION_GETPOPUP)

 protected:
  ~BrowserActionGetPopupFunction() override {}
};

class BrowserActionSetBadgeTextFunction
    : public ExtensionActionSetBadgeTextFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setBadgeText",
                             BROWSERACTION_SETBADGETEXT)

 protected:
  ~BrowserActionSetBadgeTextFunction() override {}
};

class BrowserActionSetBadgeBackgroundColorFunction
    : public ExtensionActionSetBadgeBackgroundColorFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setBadgeBackgroundColor",
                             BROWSERACTION_SETBADGEBACKGROUNDCOLOR)

 protected:
  ~BrowserActionSetBadgeBackgroundColorFunction() override {}
};

class BrowserActionGetBadgeTextFunction
    : public ExtensionActionGetBadgeTextFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getBadgeText",
                             BROWSERACTION_GETBADGETEXT)

 protected:
  ~BrowserActionGetBadgeTextFunction() override {}
};

class BrowserActionGetBadgeBackgroundColorFunction
    : public ExtensionActionGetBadgeBackgroundColorFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getBadgeBackgroundColor",
                             BROWSERACTION_GETBADGEBACKGROUNDCOLOR)

 protected:
  ~BrowserActionGetBadgeBackgroundColorFunction() override {}
};

class BrowserActionEnableFunction : public ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.enable", BROWSERACTION_ENABLE)

 protected:
  ~BrowserActionEnableFunction() override {}
};

class BrowserActionDisableFunction : public ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.disable", BROWSERACTION_DISABLE)

 protected:
  ~BrowserActionDisableFunction() override {}
};

class BrowserActionOpenPopupFunction : public ChromeAsyncExtensionFunction,
                                       public content::NotificationObserver {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.openPopup",
                             BROWSERACTION_OPEN_POPUP)
  BrowserActionOpenPopupFunction();

 private:
  ~BrowserActionOpenPopupFunction() override {}

  // ExtensionFunction:
  bool RunAsync() override;

  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;
  void OpenPopupTimedOut();

  content::NotificationRegistrar registrar_;
  bool response_sent_;

  DISALLOW_COPY_AND_ASSIGN(BrowserActionOpenPopupFunction);
};

}  // namespace extensions

//
// pageAction.* aliases for supported pageAction APIs.
//

class PageActionShowFunction : public extensions::ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.show", PAGEACTION_SHOW)

 protected:
  ~PageActionShowFunction() override {}
};

class PageActionHideFunction : public extensions::ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.hide", PAGEACTION_HIDE)

 protected:
  ~PageActionHideFunction() override {}
};

class PageActionSetIconFunction
    : public extensions::ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setIcon", PAGEACTION_SETICON)

 protected:
  ~PageActionSetIconFunction() override {}
};

class PageActionSetTitleFunction
    : public extensions::ExtensionActionSetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setTitle", PAGEACTION_SETTITLE)

 protected:
  ~PageActionSetTitleFunction() override {}
};

class PageActionSetPopupFunction
    : public extensions::ExtensionActionSetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setPopup", PAGEACTION_SETPOPUP)

 protected:
  ~PageActionSetPopupFunction() override {}
};

class PageActionGetTitleFunction
    : public extensions::ExtensionActionGetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.getTitle", PAGEACTION_GETTITLE)

 protected:
  ~PageActionGetTitleFunction() override {}
};

class PageActionGetPopupFunction
    : public extensions::ExtensionActionGetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.getPopup", PAGEACTION_GETPOPUP)

 protected:
  ~PageActionGetPopupFunction() override {}
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_
