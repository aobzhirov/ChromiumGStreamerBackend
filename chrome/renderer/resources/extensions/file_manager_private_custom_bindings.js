// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom binding for the fileManagerPrivate API.

// Bindings
var binding = require('binding').Binding.create('fileManagerPrivate');
var eventBindings = require('event_bindings');

// Natives
var fileManagerPrivateNatives = requireNative('file_manager_private');
var fileBrowserHandlerNatives = requireNative('file_browser_handler');

// Internals
var fileManagerPrivateInternal =
    require('binding').Binding.create('fileManagerPrivateInternal').generate();

// Shorthands
var GetFileSystem = fileManagerPrivateNatives.GetFileSystem;
var GetExternalFileEntry = fileBrowserHandlerNatives.GetExternalFileEntry;

binding.registerCustomHook(function(bindingsAPI) {
  var apiFunctions = bindingsAPI.apiFunctions;

  apiFunctions.setCustomCallback('searchDrive',
      function(name, request, callback, response) {
    if (response && !response.error && response.entries) {
      response.entries = response.entries.map(function(entry) {
        return GetExternalFileEntry(entry);
      });
    }

    // So |callback| doesn't break if response is not defined.
    if (!response)
      response = {};

    if (callback)
      callback(response.entries, response.nextFeed);
  });

  apiFunctions.setCustomCallback('searchDriveMetadata',
      function(name, request, callback, response) {
    if (response && !response.error) {
      for (var i = 0; i < response.length; i++) {
        response[i].entry =
            GetExternalFileEntry(response[i].entry);
      }
    }

    // So |callback| doesn't break if response is not defined.
    if (!response)
      response = {};

    if (callback)
      callback(response);
  });

  apiFunctions.setHandleRequest('resolveIsolatedEntries',
                                function(entries, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.resolveIsolatedEntries(urls, function(
        entryDescriptions) {
      callback(entryDescriptions.map(function(description) {
        return GetExternalFileEntry(description);
      }));
    });
  });

  apiFunctions.setHandleRequest('getEntryProperties',
                                function(entries, names, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.getEntryProperties(urls, names, callback);
  });

  apiFunctions.setHandleRequest('addFileWatch', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.addFileWatch(url, callback);
  });

  apiFunctions.setHandleRequest('removeFileWatch', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.removeFileWatch(url, callback);
  });

  apiFunctions.setHandleRequest('getCustomActions', function(
        entries, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.getCustomActions(urls, callback);
  });

  apiFunctions.setHandleRequest('executeCustomAction', function(
        entries, actionId, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.executeCustomAction(urls, actionId, callback);
  });

  apiFunctions.setHandleRequest('computeChecksum', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.computeChecksum(url, callback);
  });

  apiFunctions.setHandleRequest('getMimeType', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.getMimeType(url, callback);
  });

  apiFunctions.setHandleRequest('pinDriveFile', function(entry, pin, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.pinDriveFile(url, pin, callback);
  });

  apiFunctions.setHandleRequest('executeTask',
      function(taskId, entries, callback) {
        var urls = entries.map(function(entry) {
          return fileBrowserHandlerNatives.GetEntryURL(entry);
        });
        fileManagerPrivateInternal.executeTask(taskId, urls, callback);
      });

  apiFunctions.setHandleRequest('setDefaultTask',
      function(taskId, entries, mimeTypes, callback) {
        var urls = entries.map(function(entry) {
          return fileBrowserHandlerNatives.GetEntryURL(entry);
        });
        fileManagerPrivateInternal.setDefaultTask(
            taskId, urls, mimeTypes, callback);
      });

  apiFunctions.setHandleRequest('getFileTasks', function(entries, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.getFileTasks(urls, callback);
  });

  apiFunctions.setHandleRequest('getShareUrl', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.getShareUrl(url, callback);
  });

  apiFunctions.setHandleRequest('getDownloadUrl', function(entry, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.getDownloadUrl(url, callback);
  });

  apiFunctions.setHandleRequest('requestDriveShare', function(
        entry, shareType, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.requestDriveShare(url, shareType, callback);
  });

  apiFunctions.setHandleRequest('setEntryTag', function(
        entry, visibility, key, value, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.setEntryTag(
        url, visibility, key, value, callback);
  });

  apiFunctions.setHandleRequest('cancelFileTransfers', function(
        entries, callback) {
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.cancelFileTransfers(urls, callback);
  });

  apiFunctions.setHandleRequest('startCopy', function(
        entry, parentEntry, newName, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    var parentUrl = fileBrowserHandlerNatives.GetEntryURL(parentEntry);
    fileManagerPrivateInternal.startCopy(
        url, parentUrl, newName, callback);
  });

  apiFunctions.setHandleRequest('zipSelection', function(
        parentEntry, entries, destName, callback) {
    var parentUrl = fileBrowserHandlerNatives.GetEntryURL(parentEntry);
    var urls = entries.map(function(entry) {
      return fileBrowserHandlerNatives.GetEntryURL(entry);
    });
    fileManagerPrivateInternal.zipSelection(
        parentUrl, urls, destName, callback);
  });

  apiFunctions.setHandleRequest('validatePathNameLength', function(
        entry, name, callback) {
    var url = fileBrowserHandlerNatives.GetEntryURL(entry);
    fileManagerPrivateInternal.validatePathNameLength(url, name, callback);
  });
});

eventBindings.registerArgumentMassager(
    'fileManagerPrivate.onDirectoryChanged', function(args, dispatch) {
  // Convert the entry arguments into a real Entry object.
  args[0].entry = GetExternalFileEntry(args[0].entry);
  dispatch(args);
});

exports.$set('binding', binding.generate());
