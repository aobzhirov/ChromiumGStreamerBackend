// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Namespace for utility functions.
 */
var util = {};

/**
 * @param {string} name File error name.
 * @return {string} Translated file error string.
 */
util.getFileErrorString = function(name) {
  var candidateMessageFragment;
  switch (name) {
    case 'NotFoundError':
      candidateMessageFragment = 'NOT_FOUND';
      break;
    case 'SecurityError':
      candidateMessageFragment = 'SECURITY';
      break;
    case 'NotReadableError':
      candidateMessageFragment = 'NOT_READABLE';
      break;
    case 'NoModificationAllowedError':
      candidateMessageFragment = 'NO_MODIFICATION_ALLOWED';
      break;
    case 'InvalidStateError':
      candidateMessageFragment = 'INVALID_STATE';
      break;
    case 'InvalidModificationError':
      candidateMessageFragment = 'INVALID_MODIFICATION';
      break;
    case 'PathExistsError':
      candidateMessageFragment = 'PATH_EXISTS';
      break;
    case 'QuotaExceededError':
      candidateMessageFragment = 'QUOTA_EXCEEDED';
      break;
  }

  return loadTimeData.getString('FILE_ERROR_' + candidateMessageFragment) ||
      loadTimeData.getString('FILE_ERROR_GENERIC');
};

/**
 * Mapping table for FileError.code style enum to DOMError.name string.
 *
 * @enum {string}
 * @const
 */
util.FileError = {
  ABORT_ERR: 'AbortError',
  INVALID_MODIFICATION_ERR: 'InvalidModificationError',
  INVALID_STATE_ERR: 'InvalidStateError',
  NO_MODIFICATION_ALLOWED_ERR: 'NoModificationAllowedError',
  NOT_FOUND_ERR: 'NotFoundError',
  NOT_READABLE_ERR: 'NotReadable',
  PATH_EXISTS_ERR: 'PathExistsError',
  QUOTA_EXCEEDED_ERR: 'QuotaExceededError',
  TYPE_MISMATCH_ERR: 'TypeMismatchError',
  ENCODING_ERR: 'EncodingError',
};
Object.freeze(util.FileError);

/**
 * @param {string} str String to escape.
 * @return {string} Escaped string.
 */
util.htmlEscape = function(str) {
  return str.replace(/[<>&]/g, function(entity) {
    switch (entity) {
      case '<': return '&lt;';
      case '>': return '&gt;';
      case '&': return '&amp;';
    }
  });
};

/**
 * @param {string} str String to unescape.
 * @return {string} Unescaped string.
 */
util.htmlUnescape = function(str) {
  return str.replace(/&(lt|gt|amp);/g, function(entity) {
    switch (entity) {
      case '&lt;': return '<';
      case '&gt;': return '>';
      case '&amp;': return '&';
    }
  });
};

/**
 * Renames the entry to newName.
 * @param {Entry} entry The entry to be renamed.
 * @param {string} newName The new name.
 * @param {function(Entry)} successCallback Callback invoked when the rename
 *     is successfully done.
 * @param {function(DOMError)} errorCallback Callback invoked when an error
 *     is found.
 */
util.rename = function(entry, newName, successCallback, errorCallback) {
  entry.getParent(function(parentEntry) {
    var parent = /** @type {!DirectoryEntry} */ (parentEntry);

    // Before moving, we need to check if there is an existing entry at
    // parent/newName, since moveTo will overwrite it.
    // Note that this way has some timing issue. After existing check,
    // a new entry may be create on background. However, there is no way not to
    // overwrite the existing file, unfortunately. The risk should be low,
    // assuming the unsafe period is very short.
    (entry.isFile ? parent.getFile : parent.getDirectory).call(
        parent, newName, {create: false},
        function(entry) {
          // The entry with the name already exists.
          errorCallback(util.createDOMError(util.FileError.PATH_EXISTS_ERR));
        },
        function(error) {
          if (error.name != util.FileError.NOT_FOUND_ERR) {
            // Unexpected error is found.
            errorCallback(error);
            return;
          }

          // No existing entry is found.
          entry.moveTo(parent, newName, successCallback, errorCallback);
        });
  }, errorCallback);
};

/**
 * Converts DOMError of util.rename to error message.
 * @param {!DOMError} error
 * @param {!Entry} entry
 * @param {string} newName
 * @return {string}
 */
util.getRenameErrorMessage = function(error, entry, newName) {
  if (error.name == util.FileError.PATH_EXISTS_ERR ||
      error.name == util.FileError.TYPE_MISMATCH_ERR) {
    // Check the existing entry is file or not.
    // 1) If the entry is a file:
    //   a) If we get PATH_EXISTS_ERR, a file exists.
    //   b) If we get TYPE_MISMATCH_ERR, a directory exists.
    // 2) If the entry is a directory:
    //   a) If we get PATH_EXISTS_ERR, a directory exists.
    //   b) If we get TYPE_MISMATCH_ERR, a file exists.
    return strf(
        (entry.isFile && error.name ==
            util.FileError.PATH_EXISTS_ERR) ||
        (!entry.isFile && error.name ==
            util.FileError.TYPE_MISMATCH_ERR) ?
            'FILE_ALREADY_EXISTS' :
            'DIRECTORY_ALREADY_EXISTS',
        newName);
  }

  return strf('ERROR_RENAMING', entry.name,
      util.getFileErrorString(error.name));
};

/**
 * Remove a file or a directory.
 * @param {Entry} entry The entry to remove.
 * @param {function()} onSuccess The success callback.
 * @param {function(DOMError)} onError The error callback.
 */
util.removeFileOrDirectory = function(entry, onSuccess, onError) {
  if (entry.isDirectory)
    entry.removeRecursively(onSuccess, onError);
  else
    entry.remove(onSuccess, onError);
};

/**
 * Convert a number of bytes into a human friendly format, using the correct
 * number separators.
 *
 * @param {number} bytes The number of bytes.
 * @return {string} Localized string.
 */
util.bytesToString = function(bytes) {
  // Translation identifiers for size units.
  var UNITS = ['SIZE_BYTES',
               'SIZE_KB',
               'SIZE_MB',
               'SIZE_GB',
               'SIZE_TB',
               'SIZE_PB'];

  // Minimum values for the units above.
  var STEPS = [0,
               Math.pow(2, 10),
               Math.pow(2, 20),
               Math.pow(2, 30),
               Math.pow(2, 40),
               Math.pow(2, 50)];

  var str = function(n, u) {
    return strf(u, n.toLocaleString());
  };

  var fmt = function(s, u) {
    var rounded = Math.round(bytes / s * 10) / 10;
    return str(rounded, u);
  };

  // Less than 1KB is displayed like '80 bytes'.
  if (bytes < STEPS[1]) {
    return str(bytes, UNITS[0]);
  }

  // Up to 1MB is displayed as rounded up number of KBs.
  if (bytes < STEPS[2]) {
    var rounded = Math.ceil(bytes / STEPS[1]);
    return str(rounded, UNITS[1]);
  }

  // This loop index is used outside the loop if it turns out |bytes|
  // requires the largest unit.
  var i;

  for (i = 2 /* MB */; i < UNITS.length - 1; i++) {
    if (bytes < STEPS[i + 1])
      return fmt(STEPS[i], UNITS[i]);
  }

  return fmt(STEPS[i], UNITS[i]);
};

/**
 * Returns a string '[Ctrl-][Alt-][Shift-][Meta-]' depending on the event
 * modifiers. Convenient for writing out conditions in keyboard handlers.
 *
 * @param {Event} event The keyboard event.
 * @return {string} Modifiers.
 */
util.getKeyModifiers = function(event) {
  return (event.ctrlKey ? 'Ctrl-' : '') +
         (event.altKey ? 'Alt-' : '') +
         (event.shiftKey ? 'Shift-' : '') +
         (event.metaKey ? 'Meta-' : '');
};

/**
 * @typedef {?{
 *   scaleX: number,
 *   scaleY: number,
 *   rotate90: number
 * }}
 */
util.Transform;

/**
 * @param {Element} element Element to transform.
 * @param {util.Transform} transform Transform object,
 *                           contains scaleX, scaleY and rotate90 properties.
 */
util.applyTransform = function(element, transform) {
  element.style.transform =
      transform ? 'scaleX(' + transform.scaleX + ') ' +
                  'scaleY(' + transform.scaleY + ') ' +
                  'rotate(' + transform.rotate90 * 90 + 'deg)' :
      '';
};

/**
 * Extracts path from filesystem: URL.
 * @param {string} url Filesystem URL.
 * @return {?string} The path.
 */
util.extractFilePath = function(url) {
  var match =
      /^filesystem:[\w-]*:\/\/[\w]*\/(external|persistent|temporary)(\/.*)$/.
      exec(url);
  var path = match && match[2];
  if (!path) return null;
  return decodeURIComponent(path);
};

/**
 * A shortcut function to create a child element with given tag and class.
 *
 * @param {!HTMLElement} parent Parent element.
 * @param {string=} opt_className Class name.
 * @param {string=} opt_tag Element tag, DIV is omitted.
 * @return {!HTMLElement} Newly created element.
 */
util.createChild = function(parent, opt_className, opt_tag) {
  var child = parent.ownerDocument.createElement(opt_tag || 'div');
  if (opt_className)
    child.className = opt_className;
  parent.appendChild(child);
  return /** @type {!HTMLElement} */ (child);
};

/**
 * Obtains the element that should exist, decorates it with given type, and
 * returns it.
 * @param {string} query Query for the element.
 * @param {function(new: T, ...)} type Type used to decorate.
 * @private
 * @template T
 * @return {!T} Decorated element.
 */
util.queryDecoratedElement = function(query, type) {
  var element = queryRequiredElement(query);
  cr.ui.decorate(element, type);
  return element;
};

/**
 * Updates the app state.
 *
 * @param {?string} currentDirectoryURL Currently opened directory as an URL.
 *     If null the value is left unchanged.
 * @param {?string} selectionURL Currently selected entry as an URL. If null the
 *     value is left unchanged.
 * @param {string|Object=} opt_param Additional parameters, to be stored. If
 *     null, then left unchanged.
 */
util.updateAppState = function(currentDirectoryURL, selectionURL, opt_param) {
  window.appState = window.appState || {};
  if (opt_param !== undefined && opt_param !== null)
    window.appState.params = opt_param;
  if (currentDirectoryURL !== null)
    window.appState.currentDirectoryURL = currentDirectoryURL;
  if (selectionURL !== null)
    window.appState.selectionURL = selectionURL;
  util.saveAppState();
};

/**
 * Returns a translated string.
 *
 * Wrapper function to make dealing with translated strings more concise.
 * Equivalent to loadTimeData.getString(id).
 *
 * @param {string} id The id of the string to return.
 * @return {string} The translated string.
 */
function str(id) {
  return loadTimeData.getString(id);
}

/**
 * Returns a translated string with arguments replaced.
 *
 * Wrapper function to make dealing with translated strings more concise.
 * Equivalent to loadTimeData.getStringF(id, ...).
 *
 * @param {string} id The id of the string to return.
 * @param {...*} var_args The values to replace into the string.
 * @return {string} The translated string with replaced values.
 */
function strf(id, var_args) {
  return loadTimeData.getStringF.apply(loadTimeData, arguments);
}

/**
 * @return {boolean} True if Files.app is running as an open files or a select
 *     folder dialog. False otherwise.
 */
util.runningInBrowser = function() {
  return !window.appID;
};

/**
 * Attach page load handler.
 * @param {function()} handler Application-specific load handler.
 */
util.addPageLoadHandler = function(handler) {
  document.addEventListener('DOMContentLoaded', function() {
    handler();
  });
};

/**
 * Save app launch data to the local storage.
 */
util.saveAppState = function() {
  if (!window.appState)
    return;
  var items = {};

  items[window.appID] = JSON.stringify(window.appState);
  chrome.storage.local.set(items);
};

/**
 *  AppCache is a persistent timestamped key-value storage backed by
 *  HTML5 local storage.
 *
 *  It is not designed for frequent access. In order to avoid costly
 *  localStorage iteration all data is kept in a single localStorage item.
 *  There is no in-memory caching, so concurrent access is _almost_ safe.
 *
 *  TODO(kaznacheev) Reimplement this based on Indexed DB.
 */
util.AppCache = function() {};

/**
 * Local storage key.
 */
util.AppCache.KEY = 'AppCache';

/**
 * Max number of items.
 */
util.AppCache.CAPACITY = 100;

/**
 * Default lifetime.
 */
util.AppCache.LIFETIME = 30 * 24 * 60 * 60 * 1000;  // 30 days.

/**
 * @param {string} key Key.
 * @param {function(number)} callback Callback accepting a value.
 */
util.AppCache.getValue = function(key, callback) {
  util.AppCache.read_(function(map) {
    var entry = map[key];
    callback(entry && entry.value);
  });
};

/**
 * Updates the cache.
 *
 * @param {string} key Key.
 * @param {?(string|number)} value Value. Remove the key if value is null.
 * @param {number=} opt_lifetime Maximum time to keep an item (in milliseconds).
 */
util.AppCache.update = function(key, value, opt_lifetime) {
  util.AppCache.read_(function(map) {
    if (value != null) {
      map[key] = {
        value: value,
        expire: Date.now() + (opt_lifetime || util.AppCache.LIFETIME)
      };
    } else if (key in map) {
      delete map[key];
    } else {
      return;  // Nothing to do.
    }
    util.AppCache.cleanup_(map);
    util.AppCache.write_(map);
  });
};

/**
 * @param {function(Object)} callback Callback accepting a map of timestamped
 *   key-value pairs.
 * @private
 */
util.AppCache.read_ = function(callback) {
  chrome.storage.local.get(util.AppCache.KEY, function(values) {
    var json = values[util.AppCache.KEY];
    if (json) {
      try {
        callback(/** @type {Object} */ (JSON.parse(json)));
      } catch (e) {
        // The local storage item somehow got messed up, start fresh.
      }
    }
    callback({});
  });
};

/**
 * @param {Object} map A map of timestamped key-value pairs.
 * @private
 */
util.AppCache.write_ = function(map) {
  var items = {};
  items[util.AppCache.KEY] = JSON.stringify(map);
  chrome.storage.local.set(items);
};

/**
 * Remove over-capacity and obsolete items.
 *
 * @param {Object} map A map of timestamped key-value pairs.
 * @private
 */
util.AppCache.cleanup_ = function(map) {
  // Sort keys by ascending timestamps.
  var keys = [];
  for (var key in map) {
    if (map.hasOwnProperty(key))
      keys.push(key);
  }
  keys.sort(function(a, b) { return map[a].expire - map[b].expire; });

  var cutoff = Date.now();

  var obsolete = 0;
  while (obsolete < keys.length &&
         map[keys[obsolete]].expire < cutoff) {
    obsolete++;
  }

  var overCapacity = Math.max(0, keys.length - util.AppCache.CAPACITY);

  var itemsToDelete = Math.max(obsolete, overCapacity);
  for (var i = 0; i != itemsToDelete; i++) {
    delete map[keys[i]];
  }
};

/**
 * Returns true if the board of the device matches the given prefix.
 * @param {string} boardPrefix The board prefix to match against.
 *     (ex. "x86-mario". Prefix is used as the actual board name comes with
 *     suffix like "x86-mario-something".
 * @return {boolean} True if the board of the device matches the given prefix.
 */
util.boardIs = function(boardPrefix) {
  // The board name should be lower-cased, but making it case-insensitive for
  // backward compatibility just in case.
  var board = str('CHROMEOS_RELEASE_BOARD');
  var pattern = new RegExp('^' + boardPrefix, 'i');
  return board.match(pattern) != null;
};

/**
 * Adds an isFocused method to the current window object.
 */
util.addIsFocusedMethod = function() {
  var focused = true;

  window.addEventListener('focus', function() {
    focused = true;
  });

  window.addEventListener('blur', function() {
    focused = false;
  });

  /**
   * @return {boolean} True if focused.
   */
  window.isFocused = function() {
    return focused;
  };
};

/**
 * Checks, if the Files.app's window is in a full screen mode.
 *
 * @param {chrome.app.window.AppWindow} appWindow App window to be maximized.
 * @return {boolean} True if the full screen mode is enabled.
 */
util.isFullScreen = function(appWindow) {
  if (appWindow) {
    return appWindow.isFullscreen();
  } else {
    console.error('App window not passed. Unable to check status of ' +
                  'the full screen mode.');
    return false;
  }
};

/**
 * Toggles the full screen mode.
 *
 * @param {chrome.app.window.AppWindow} appWindow App window to be maximized.
 * @param {boolean} enabled True for enabling, false for disabling.
 */
util.toggleFullScreen = function(appWindow, enabled) {
  if (appWindow) {
    if (enabled)
      appWindow.fullscreen();
    else
      appWindow.restore();
    return;
  }

  console.error(
      'App window not passed. Unable to toggle the full screen mode.');
};

/**
 * The type of a file operation.
 * @enum {string}
 * @const
 */
util.FileOperationType = {
  COPY: 'COPY',
  MOVE: 'MOVE',
  ZIP: 'ZIP',
};
Object.freeze(util.FileOperationType);

/**
 * The type of a file operation error.
 * @enum {number}
 * @const
 */
util.FileOperationErrorType = {
  UNEXPECTED_SOURCE_FILE: 0,
  TARGET_EXISTS: 1,
  FILESYSTEM_ERROR: 2,
};
Object.freeze(util.FileOperationErrorType);

/**
 * The kind of an entry changed event.
 * @enum {number}
 * @const
 */
util.EntryChangedKind = {
  CREATED: 0,
  DELETED: 1,
};
Object.freeze(util.EntryChangedKind);

/**
 * Obtains whether an entry is fake or not.
 * @param {(!Entry|!FakeEntry)} entry Entry or a fake entry.
 * @return {boolean} True if the given entry is fake.
 */
util.isFakeEntry = function(entry) {
  return !('getParent' in entry);
};

/**
 * Creates an instance of UserDOMError with given error name that looks like a
 * FileError except that it does not have the deprecated FileError.code member.
 *
 * @param {string} name Error name for the file error.
 * @return {DOMError} DOMError instance
 */
util.createDOMError = function(name) {
  return new util.UserDOMError(name);
};

/**
 * Creates a DOMError-like object to be used in place of returning file errors.
 *
 * @param {string} name Error name for the file error.
 * @extends {DOMError}
 * @constructor
 */
util.UserDOMError = function(name) {
  /**
   * @type {string}
   * @private
   */
  this.name_ = name;
  Object.freeze(this);
};

util.UserDOMError.prototype = {
  /**
   * @return {string} File error name.
   */
  get name() { return this.name_;
  }
};

/**
 * Compares two entries.
 * @param {Entry|FakeEntry} entry1 The entry to be compared. Can be a fake.
 * @param {Entry|FakeEntry} entry2 The entry to be compared. Can be a fake.
 * @return {boolean} True if the both entry represents a same file or
 *     directory. Returns true if both entries are null.
 */
util.isSameEntry = function(entry1, entry2) {
  if (!entry1 && !entry2)
    return true;
  if (!entry1 || !entry2)
    return false;
  return entry1.toURL() === entry2.toURL();
};

/**
 * Compares two file systems.
 * @param {FileSystem} fileSystem1 The file system to be compared.
 * @param {FileSystem} fileSystem2 The file system to be compared.
 * @return {boolean} True if the both file systems are equal. Also, returns true
 *     if both file systems are null.
 */
util.isSameFileSystem = function(fileSystem1, fileSystem2) {
  if (!fileSystem1 && !fileSystem2)
    return true;
  if (!fileSystem1 || !fileSystem2)
    return false;
  return util.isSameEntry(fileSystem1.root, fileSystem2.root);
};

/**
 * Collator for sorting.
 * @type {Intl.Collator}
 */
util.collator = new Intl.Collator(
    [], {usage: 'sort', numeric: true, sensitivity: 'base'});

/**
 * Compare by name. The 2 entries must be in same directory.
 * @param {Entry} entry1 First entry.
 * @param {Entry} entry2 Second entry.
 * @return {number} Compare result.
 */
util.compareName = function(entry1, entry2) {
  return util.collator.compare(entry1.name, entry2.name);
};

/**
 * Compare by path.
 * @param {Entry} entry1 First entry.
 * @param {Entry} entry2 Second entry.
 * @return {number} Compare result.
 */
util.comparePath = function(entry1, entry2) {
  return util.collator.compare(entry1.fullPath, entry2.fullPath);
};

/**
 * Checks if {@code entry} is an immediate child of {@code directory}.
 *
 * @param {Entry} entry The presumptive child.
 * @param {DirectoryEntry|FakeEntry} directory The presumptive parent.
 * @return {!Promise.<boolean>} Resolves with true if {@code directory} is
 *     parent of {@code entry}.
 */
util.isChildEntry = function(entry, directory) {
  return new Promise(
      function(resolve, reject) {
        if (!entry || !directory) {
          resolve(false);
        }

        entry.getParent(
            function(parent) {
              resolve(util.isSameEntry(parent, directory));
            },
            reject);
    });
};

/**
 * Checks if the child entry is a descendant of another entry. If the entries
 * point to the same file or directory, then returns false.
 *
 * @param {!DirectoryEntry|!FakeEntry} ancestorEntry The ancestor directory
 *     entry. Can be a fake.
 * @param {!Entry|!FakeEntry} childEntry The child entry. Can be a fake.
 * @return {boolean} True if the child entry is contained in the ancestor path.
 */
util.isDescendantEntry = function(ancestorEntry, childEntry) {
  if (!ancestorEntry.isDirectory)
    return false;
  if (!util.isSameFileSystem(ancestorEntry.filesystem, childEntry.filesystem))
    return false;
  if (util.isSameEntry(ancestorEntry, childEntry))
    return false;
  if (util.isFakeEntry(ancestorEntry) || util.isFakeEntry(childEntry))
    return false;

  // Check if the ancestor's path with trailing slash is a prefix of child's
  // path.
  var ancestorPath = ancestorEntry.fullPath;
  if (ancestorPath.slice(-1) !== '/')
    ancestorPath += '/';
  return childEntry.fullPath.indexOf(ancestorPath) === 0;
};

/**
 * Visit the URL.
 *
 * If the browser is opening, the url is opened in a new tag, otherwise the url
 * is opened in a new window.
 *
 * @param {string} url URL to visit.
 */
util.visitURL = function(url) {
  window.open(url);
};

/**
 * Returns normalized current locale, or default locale - 'en'.
 * @return {string} Current locale
 */
util.getCurrentLocaleOrDefault = function() {
  // chrome.i18n.getMessage('@@ui_locale') can't be used in packed app.
  // Instead, we pass it from C++-side with strings.
  return str('UI_LOCALE') || 'en';
};

/**
 * Converts array of entries to an array of corresponding URLs.
 * @param {Array<Entry>} entries Input array of entries.
 * @return {!Array<string>} Output array of URLs.
 */
util.entriesToURLs = function(entries) {
  return entries.map(function(entry) {
    // When building background.js, cachedUrl is not refered other than here.
    // Thus closure compiler raises an error if we refer the property like
    // entry.cachedUrl.
    return entry['cachedUrl'] || entry.toURL();
  });
};

/**
 * Converts array of URLs to an array of corresponding Entries.
 *
 * @param {Array<string>} urls Input array of URLs.
 * @param {function(!Array<!Entry>, !Array<!URL>)=} opt_callback Completion
 *     callback with array of success Entries and failure URLs.
 * @return {Promise} Promise fulfilled with the object that has entries property
 *     and failureUrls property. The promise is never rejected.
 */
util.URLsToEntries = function(urls, opt_callback) {
  var promises = urls.map(function(url) {
    return new Promise(window.webkitResolveLocalFileSystemURL.bind(null, url)).
        then(function(entry) {
          return {entry: entry};
        }, function(failureUrl) {
          // Not an error. Possibly, the file is not accessible anymore.
          console.warn('Failed to resolve the file with url: ' + url + '.');
          return {failureUrl: url};
        });
  });
  var resultPromise = Promise.all(promises).then(function(results) {
    var entries = [];
    var failureUrls = [];
    for (var i = 0; i < results.length; i++) {
      if ('entry' in results[i])
        entries.push(results[i].entry);
      if ('failureUrl' in results[i]) {
        failureUrls.push(results[i].failureUrl);
      }
    }
    return {
      entries: entries,
      failureUrls: failureUrls
    };
  });

  // Invoke the callback. If opt_callback is specified, resultPromise is still
  // returned and fulfilled with a result.
  if (opt_callback) {
    resultPromise.then(function(result) {
      opt_callback(result.entries, result.failureUrls);
    }).catch(function(error) {
      console.error(
          'util.URLsToEntries is failed.',
          error.stack ? error.stack : error);
    });
  }

  return resultPromise;
};

/**
 * Converts a url into an {!Entry}, if possible.
 *
 * @param {string} url
 *
 * @return {!Promise.<!Entry>} Promise Resolves with the corresponding
 *     {!Entry} if possible, else rejects.
 */
util.urlToEntry = function(url) {
  return new Promise(
      window.webkitResolveLocalFileSystemURL.bind(null, url));
};

/**
 * Returns whether the window is teleported or not.
 * @param {Window} window Window.
 * @return {Promise.<boolean>} Whether the window is teleported or not.
 */
util.isTeleported = function(window) {
  return new Promise(function(onFulfilled) {
    window.chrome.fileManagerPrivate.getProfiles(
        function(profiles, currentId, displayedId) {
          onFulfilled(currentId !== displayedId);
        });
  });
};

/**
 * Sets up and shows the alert to inform a user the task is opened in the
 * desktop of the running profile.
 *
 * TODO(hirono): Move the function from the util namespace.
 * @param {cr.ui.dialogs.AlertDialog} alertDialog Alert dialog to be shown.
 * @param {Array<Entry>} entries List of opened entries.
 */
util.showOpenInOtherDesktopAlert = function(alertDialog, entries) {
  if (!entries.length)
    return;
  chrome.fileManagerPrivate.getProfiles(
      function(profiles, currentId, displayedId) {
        // Find strings.
        var displayName;
        for (var i = 0; i < profiles.length; i++) {
          if (profiles[i].profileId === currentId) {
            displayName = profiles[i].displayName;
            break;
          }
        }
        if (!displayName) {
          console.warn('Display name is not found.');
          return;
        }

        var title = entries.length > 1 ?
            entries[0].name + '\u2026' /* ellipsis */ : entries[0].name;
        var message = strf(entries.length > 1 ?
                           'OPEN_IN_OTHER_DESKTOP_MESSAGE_PLURAL' :
                           'OPEN_IN_OTHER_DESKTOP_MESSAGE',
                           displayName,
                           currentId);

        // Show the dialog.
        alertDialog.showWithTitle(title, message, null, null, null);
      }.bind(this));
};

/**
 * Runs chrome.test.sendMessage in test environment. Does nothing if running
 * in production environment.
 *
 * @param {string} message Test message to send.
 */
util.testSendMessage = function(message) {
  var test = chrome.test || window.top.chrome.test;
  if (test)
    test.sendMessage(message);
};

/**
 * Extracts the extension of the path.
 *
 * Examples:
 * util.splitExtension('abc.ext') -> ['abc', '.ext']
 * util.splitExtension('a/b/abc.ext') -> ['a/b/abc', '.ext']
 * util.splitExtension('a/b') -> ['a/b', '']
 * util.splitExtension('.cshrc') -> ['', '.cshrc']
 * util.splitExtension('a/b.backup/hoge') -> ['a/b.backup/hoge', '']
 *
 * @param {string} path Path to be extracted.
 * @return {Array<string>} Filename and extension of the given path.
 */
util.splitExtension = function(path) {
  var dotPosition = path.lastIndexOf('.');
  if (dotPosition <= path.lastIndexOf('/'))
    dotPosition = -1;

  var filename = dotPosition != -1 ? path.substr(0, dotPosition) : path;
  var extension = dotPosition != -1 ? path.substr(dotPosition) : '';
  return [filename, extension];
};

/**
 * Returns the localized name of the root type.
 * @param {!EntryLocation} locationInfo Location info.
 * @return {string} The localized name.
 */
util.getRootTypeLabel = function(locationInfo) {
  switch (locationInfo.rootType) {
    case VolumeManagerCommon.RootType.DOWNLOADS:
      return str('DOWNLOADS_DIRECTORY_LABEL');
    case VolumeManagerCommon.RootType.DRIVE:
      return str('DRIVE_MY_DRIVE_LABEL');
    case VolumeManagerCommon.RootType.DRIVE_OFFLINE:
      return str('DRIVE_OFFLINE_COLLECTION_LABEL');
    case VolumeManagerCommon.RootType.DRIVE_SHARED_WITH_ME:
      return str('DRIVE_SHARED_WITH_ME_COLLECTION_LABEL');
    case VolumeManagerCommon.RootType.DRIVE_RECENT:
      return str('DRIVE_RECENT_COLLECTION_LABEL');
    case VolumeManagerCommon.RootType.DRIVE_OTHER:
    case VolumeManagerCommon.RootType.ARCHIVE:
    case VolumeManagerCommon.RootType.REMOVABLE:
    case VolumeManagerCommon.RootType.MTP:
    case VolumeManagerCommon.RootType.PROVIDED:
      return locationInfo.volumeInfo.label;
    default:
      console.error('Unsupported root type: ' + locationInfo.rootType);
      return locationInfo.volumeInfo.label;
  }
}

/**
 * Returns the localized name of the entry.
 *
 * @param {EntryLocation} locationInfo
 * @param {!Entry} entry The entry to be retrieve the name of.
 * @return {?string} The localized name.
 */
util.getEntryLabel = function(locationInfo, entry) {
  if (locationInfo && locationInfo.isRootEntry)
    return util.getRootTypeLabel(locationInfo);
  else
    return entry.name;
};

/**
 * Checks if the specified set of allowed effects contains the given effect.
 * See: http://www.w3.org/TR/html5/editing.html#the-datatransfer-interface
 *
 * @param {string} effectAllowed The string denoting the set of allowed effects.
 * @param {string} dropEffect The effect to be checked.
 * @return {boolean} True if |dropEffect| is included in |effectAllowed|.
 */
util.isDropEffectAllowed = function(effectAllowed, dropEffect) {
  return effectAllowed === 'all' ||
      effectAllowed.toLowerCase().indexOf(dropEffect) !== -1;
};

/**
 * Verifies the user entered name for file or folder to be created or
 * renamed to. Name restrictions must correspond to File API restrictions
 * (see DOMFilePath::isValidPath). Curernt WebKit implementation is
 * out of date (spec is
 * http://dev.w3.org/2009/dap/file-system/file-dir-sys.html, 8.3) and going to
 * be fixed. Shows message box if the name is invalid.
 *
 * It also verifies if the name length is in the limit of the filesystem.
 *
 * @param {!DirectoryEntry} parentEntry The entry of the parent directory.
 * @param {string} name New file or folder name.
 * @param {boolean} filterHiddenOn Whether to report the hidden file name error
 *     or not.
 * @return {Promise} Promise fulfilled on success, or rejected with the error
 *     message.
 */
util.validateFileName = function(parentEntry, name, filterHiddenOn) {
  var testResult = /[\/\\\<\>\:\?\*\"\|]/.exec(name);
  var msg;
  if (testResult)
    return Promise.reject(strf('ERROR_INVALID_CHARACTER', testResult[0]));
  else if (/^\s*$/i.test(name))
    return Promise.reject(str('ERROR_WHITESPACE_NAME'));
  else if (/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(name))
    return Promise.reject(str('ERROR_RESERVED_NAME'));
  else if (filterHiddenOn && /\.crdownload$/i.test(name))
    return Promise.reject(str('ERROR_RESERVED_NAME'));
  else if (filterHiddenOn && name[0] == '.')
    return Promise.reject(str('ERROR_HIDDEN_NAME'));

  return new Promise(function(fulfill, reject) {
    chrome.fileManagerPrivate.validatePathNameLength(
        parentEntry,
        name,
        function(valid) {
          if (valid)
            fulfill(null);
          else
            reject(str('ERROR_LONG_NAME'));
        });
  });
};

/**
 * Adds a foregorund listener to the background page components.
 * The lisner will be removed when the foreground window is closed.
 * @param {!cr.EventTarget} target
 * @param {string} type
 * @param {Function} handler
 */
util.addEventListenerToBackgroundComponent = function(target, type, handler) {
  target.addEventListener(type, handler);
  window.addEventListener('pagehide', function() {
    target.removeEventListener(type, handler);
  });
};

/**
 * Checks if an API call returned an error, and if yes then prints it.
 */
util.checkAPIError = function() {
  if (chrome.runtime.lastError)
    console.error(chrome.runtime.lastError.message);
};

/**
 * Makes a promise which will be fulfilled |ms| milliseconds later.
 * @param {number} ms The delay in milliseconds.
 * @return {!Promise}
 */
util.delay = function(ms) {
  return new Promise(function(resolve) {
    setTimeout(resolve, ms);
  });
};

/**
 * Makes a promise which will be rejected if the given |promise| is not resolved
 * or rejected for |ms| milliseconds.
 * @param {!Promise} promise A promise which needs to be timed out.
 * @param {number} ms Delay for the timeout in milliseconds.
 * @param {string=} opt_message Error message for the timeout.
 * @return {!Promise} A promise which can be rejected by timeout.
 */
util.timeoutPromise = function(promise, ms, opt_message) {
  return Promise.race([
    promise,
    util.delay(ms).then(function() {
      throw new Error(opt_message || 'Operation timed out.');
    })
  ]);
};
