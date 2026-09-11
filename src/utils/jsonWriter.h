/* Petal: An open source system for classification learning from very large data
** Copyright (C) 2012 Geoffrey I Webb
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
** Please report any bugs to Geoff Webb <geoff.webb@monash.edu>
*/
#ifndef JSONWRITER_H
#define JSONWRITER_H

#include <stdio.h>

#include <string>
#include <vector>

/**
 * Minimal streaming JSON writer (write-only; no parsing).
 *
 * Petal deliberately carries no third-party dependencies, so this is a
 * self-contained emitter rather than a wrapper around an external library.
 * It exists to give the web front-end (e.g. lab/web) a stable, structured
 * contract to consume, instead of scraping the variable human-readable text
 * that the evaluation modes print.
 *
 * Output layout: object members go one per line (easy to diff/grep), while
 * arrays of scalars stay on a single line (probability dumps would otherwise
 * explode into one number per line).
 *
 * The writer tracks the stack of open containers so that it — and not the
 * caller — decides where commas and newlines go; a call made in the wrong
 * context (e.g. value() outside an array) is silently ignored rather than
 * producing malformed JSON.
 *
 * Typical use:
 * @code
 *   JsonWriter jw(f);
 *   jw.beginObject();
 *   jw.field("dataset", "weather");
 *   jw.arrayField("metrics");
 *   jw.beginObject();
 *   jw.field("learner", "nb");
 *   jw.field("value", 0.0909);
 *   jw.endObject();
 *   jw.endArray();
 *   jw.endObject();
 * @endcode
 */
class JsonWriter {
public:
  explicit JsonWriter(FILE* f) : f_(f) {}

  /// Open `{` as an anonymous value (only valid directly inside an array).
  void beginObject();
  /// Close the current object.
  void endObject();

  /// Open `{` as the value of a named member.
  void objectField(const char* key);
  /// Open `[` as the value of a named member.
  void arrayField(const char* key);

  /// Close the current array.
  void endArray();

  // Named members (valid inside an object).
  void field(const char* key, const char* value);
  void field(const char* key, const std::string& value);
  void field(const char* key, double value);
  void field(const char* key, int value);
  void field(const char* key, unsigned int value);
  void field(const char* key, bool value);

  /// Convenience: `"key": [v0, v1, ...]` on one line.
  void doubleArrayField(const char* key, const std::vector<double>& values);

  /// Anonymous scalar (valid inside an array).
  void value(double v);
  void value(unsigned int v);
  void value(const char* s);

private:
  /// One open container; frames_.back() is the innermost.
  struct Frame {
    // NOLINT: aggregate-style container state, initialised inline.
    char kind;       ///< 'o' object, 'a' array
    bool empty;      ///< no element written yet, so no comma is needed
    bool multiline;  ///< array: holds objects, one per line, not all inline

    Frame(char kind_, bool multiline_) : kind(kind_), empty(true), multiline(multiline_) {}
  };

  /// Emit ",\n"/", " as required before a new element; mark the container used.
  void memberSep();
  /// @return false when there is no enclosing array, in which case the caller
  ///         must not write anything.
  bool valueSep();
  void indent();
  /// memberSep + indent + `"key": ` — the common prefix of every member.
  void beginMember(const char* key);
  void writeString(const char* s);

  FILE* f_;
  std::vector<Frame> frames_;
};

#endif // JSONWRITER_H
