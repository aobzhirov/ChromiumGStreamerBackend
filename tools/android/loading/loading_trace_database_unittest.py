# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from loading_trace_database import LoadingTraceDatabase


class LoadingTraceDatabaseUnittest(unittest.TestCase):
  _JSON_DATABASE = {
    "traces/trace1.json" : { "url" : "http://bar.html", },
    "traces/trace2.json" : { "url" : "http://bar.html", },
    "traces/trace3.json" : { "url" : "http://qux.html", },
  }

  def setUp(self):
    self.database = LoadingTraceDatabase.FromJsonDict(self._JSON_DATABASE)

  def testGetTraceFilesForURL(self):
    # Test a URL with no matching traces.
    self.assertEqual(
        self.database.GetTraceFilesForURL("http://foo.html"),
        [])

    # Test a URL with matching traces.
    self.assertEqual(
        set(self.database.GetTraceFilesForURL("http://bar.html")),
        set(["traces/trace1.json", "traces/trace2.json"]))

  def testSerialization(self):
    self.assertEqual(
        self._JSON_DATABASE, self.database.ToJsonDict())


if __name__ == '__main__':
  unittest.main()
