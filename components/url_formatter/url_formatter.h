// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// url_formatter contains routines for formatting URLs in a way that can be
// safely and securely displayed to users. For example, it is responsible
// for determining when to convert an IDN A-Label (e.g. "xn--[something]")
// into the IDN U-Label.
//
// Note that this formatting is only intended for display purposes; it would
// be insecure and insufficient to make comparisons solely on formatted URLs
// (that is, it should not be used for normalizing URLs for comparison for
// security decisions).

#ifndef COMPONENTS_URL_FORMATTER_URL_FORMATTER_H_
#define COMPONENTS_URL_FORMATTER_URL_FORMATTER_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "base/strings/utf_offset_string_conversions.h"
#include "net/base/escape.h"

class GURL;

namespace url {
struct Parsed;
}

namespace url_formatter {

// Used by FormatUrl to specify handling of certain parts of the url.
typedef uint32_t FormatUrlType;
typedef uint32_t FormatUrlTypes;

// Nothing is ommitted.
extern const FormatUrlType kFormatUrlOmitNothing;

// If set, any username and password are removed.
extern const FormatUrlType kFormatUrlOmitUsernamePassword;

// If the scheme is 'http://', it's removed.
extern const FormatUrlType kFormatUrlOmitHTTP;

// Omits the path if it is just a slash and there is no query or ref.  This is
// meaningful for non-file "standard" URLs.
extern const FormatUrlType kFormatUrlOmitTrailingSlashOnBareHostname;

// Convenience for omitting all unecessary types.
extern const FormatUrlType kFormatUrlOmitAll;

// Creates a string representation of |url|. The IDN host name is turned to
// Unicode if the Unicode representation is deemed safe. |languages| is not
// used any more and will be removed. |format_type| is a bitmask
// of FormatUrlTypes, see it for details. |unescape_rules| defines how to clean
// the URL for human readability. You will generally want |UnescapeRule::SPACES|
// for display to the user if you can handle spaces, or |UnescapeRule::NORMAL|
// if not. If the path part and the query part seem to be encoded in %-encoded
// UTF-8, decodes %-encoding and UTF-8.
//
// The last three parameters may be NULL.
//
// |new_parsed| will be set to the parsing parameters of the resultant URL.
//
// |prefix_end| will be the length before the hostname of the resultant URL.
//
// |offset[s]_for_adjustment| specifies one or more offsets into the original
// URL, representing insertion or selection points between characters: if the
// input is "http://foo.com/", offset 0 is before the entire URL, offset 7 is
// between the scheme and the host, and offset 15 is after the end of the URL.
// Valid input offsets range from 0 to the length of the input URL string.  On
// exit, each offset will have been modified to reflect any changes made to the
// output string.  For example, if |url| is "http://a:b@c.com/",
// |omit_username_password| is true, and an offset is 12 (pointing between 'c'
// and '.'), then on return the output string will be "http://c.com/" and the
// offset will be 8.  If an offset cannot be successfully adjusted (e.g. because
// it points into the middle of a component that was entirely removed or into
// the middle of an encoding sequence), it will be set to base::string16::npos.
// For consistency, if an input offset points between the scheme and the
// username/password, and both are removed, on output this offset will be 0
// rather than npos; this means that offsets at the starts and ends of removed
// components are always transformed the same way regardless of what other
// components are adjacent.
base::string16 FormatUrl(const GURL& url,
                         const std::string& languages,
                         FormatUrlTypes format_types,
                         net::UnescapeRule::Type unescape_rules,
                         url::Parsed* new_parsed,
                         size_t* prefix_end,
                         size_t* offset_for_adjustment);

base::string16 FormatUrlWithOffsets(
    const GURL& url,
    const std::string& languages,
    FormatUrlTypes format_types,
    net::UnescapeRule::Type unescape_rules,
    url::Parsed* new_parsed,
    size_t* prefix_end,
    std::vector<size_t>* offsets_for_adjustment);

// This function is like those above except it takes |adjustments| rather
// than |offset[s]_for_adjustment|.  |adjustments| will be set to reflect all
// the transformations that happened to |url| to convert it into the returned
// value.
base::string16 FormatUrlWithAdjustments(
    const GURL& url,
    const std::string& languages,
    FormatUrlTypes format_types,
    net::UnescapeRule::Type unescape_rules,
    url::Parsed* new_parsed,
    size_t* prefix_end,
    base::OffsetAdjuster::Adjustments* adjustments);

// This is a convenience function for FormatUrl() with
// format_types = kFormatUrlOmitAll and unescape = SPACES.  This is the typical
// set of flags for "URLs to display to the user".  You should be cautious about
// using this for URLs which will be parsed or sent to other applications.
inline base::string16 FormatUrl(const GURL& url, const std::string& languages) {
  return FormatUrl(url, languages, kFormatUrlOmitAll, net::UnescapeRule::SPACES,
                   nullptr, nullptr, nullptr);
}

// Returns whether FormatUrl() would strip a trailing slash from |url|, given a
// format flag including kFormatUrlOmitTrailingSlashOnBareHostname.
bool CanStripTrailingSlash(const GURL& url);

// Formats the host in |url| and appends it to |output|.  The host formatter
// takes the same accept languages component as ElideURL(), but it does not
// affect the result. It'll be removed.
void AppendFormattedHost(const GURL& url,
                         const std::string& languages,
                         base::string16* output);

// Converts the given host name to unicode characters. This can be called for
// any host name, if the input is not IDN or is invalid in some way, we'll just
// return the ASCII source so it is still usable.
//
// The input should be the canonicalized ASCII host name from GURL. This
// function does NOT accept UTF-8!
// |languages| is not used any more and will be removed.
base::string16 IDNToUnicode(const std::string& host,
                            const std::string& languages);

// If |text| starts with "www." it is removed, otherwise |text| is returned
// unmodified.
base::string16 StripWWW(const base::string16& text);

// Runs |url|'s host through StripWWW().  |url| must be valid.
base::string16 StripWWWFromHost(const GURL& url);

}  // namespace url_formatter

#endif  // COMPONENTS_URL_FORMATTER_URL_FORMATTER_H_
