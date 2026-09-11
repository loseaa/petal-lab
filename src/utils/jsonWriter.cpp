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
#include "jsonWriter.h"
#include "globals.h"

namespace {

const char* const kIndent = "  ";  ///< two spaces per nesting level

}  // namespace

void JsonWriter::indent() {
  for (size_t i = 0; i < frames_.size(); ++i) {
    fputs(kIndent, f_);
  }
}

void JsonWriter::memberSep() {
  if (frames_.empty()) return;
  if (!frames_.back().empty) fputs(",\n", f_);
  frames_.back().empty = false;
}

bool JsonWriter::valueSep() {
  if (frames_.empty() || frames_.back().kind != 'a') return false;
  if (frames_.back().empty) {
    frames_.back().empty = false;
  } else {
    fputs(", ", f_);
  }
  return true;
}

void JsonWriter::beginMember(const char* key) {
  memberSep();
  indent();
  writeString(key);
  fputs(": ", f_);
}

void JsonWriter::writeString(const char* s) {
  fputc('"', f_);
  for (const char* p = s; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
      case '"':  fputs("\\\"", f_); break;
      case '\\': fputs("\\\\", f_); break;
      case '\b': fputs("\\b", f_);  break;
      case '\f': fputs("\\f", f_);  break;
      case '\n': fputs("\\n", f_);  break;
      case '\r': fputs("\\r", f_);  break;
      case '\t': fputs("\\t", f_);  break;
      default:
        // Control characters have no JSON escape and must be \u-encoded.
        if (c < 0x20) fprintf(f_, "\\u%04x", c);
        else fputc(static_cast<int>(c), f_);
    }
  }
  fputc('"', f_);
}

void JsonWriter::beginObject() {
  if (!frames_.empty() && frames_.back().kind == 'a') {
    // An element of an array: switch the array to one-object-per-line mode.
    if (frames_.back().empty) {
      frames_.back().empty = false;
      fputc('\n', f_);  // the opening "[" sits on the previous line
    } else {
      fputs(",\n", f_);
    }
    indent();
    frames_.back().multiline = true;
  }
  fputs("{\n", f_);
  frames_.push_back(Frame('o', true));
}

void JsonWriter::objectField(const char* key) {
  beginMember(key);
  fputs("{\n", f_);
  frames_.push_back(Frame('o', true));
}

void JsonWriter::arrayField(const char* key) {
  beginMember(key);
  fputs("[", f_);
  frames_.push_back(Frame('a', false));
}

void JsonWriter::endObject() {
  if (frames_.empty() || frames_.back().kind != 'o') return;
  frames_.pop_back();
  fputc('\n', f_);
  indent();
  fputc('}', f_);
}

void JsonWriter::endArray() {
  if (frames_.empty() || frames_.back().kind != 'a') return;
  const bool multiline = frames_.back().multiline;
  const bool wasEmpty = frames_.back().empty;
  frames_.pop_back();
  if (multiline && !wasEmpty) {
    fputc('\n', f_);
    indent();
  }
  fputc(']', f_);
}

void JsonWriter::value(double v) {
  if (!valueSep()) return;
  // Same four-decimal rule as the text reports: the front-end reads these
  // numbers and must not show more precision than stdout does.
  fprintf(f_, PETAL_FLOAT_FMT, v);
}

void JsonWriter::value(unsigned int v) {
  if (!valueSep()) return;
  fprintf(f_, "%u", v);
}

void JsonWriter::value(const char* s) {
  if (!valueSep()) return;
  writeString(s);
}

void JsonWriter::field(const char* key, const char* value) {
  beginMember(key);
  writeString(value);
}

void JsonWriter::field(const char* key, const std::string& value) {
  field(key, value.c_str());
}

void JsonWriter::field(const char* key, double value) {
  beginMember(key);
  // Summary metrics round to four decimals like everywhere else; the
  // front-end reads them verbatim and must agree with the printed report.
  fprintf(f_, PETAL_FLOAT_FMT, value);
}

void JsonWriter::field(const char* key, int value) {
  beginMember(key);
  fprintf(f_, "%d", value);
}

void JsonWriter::field(const char* key, unsigned int value) {
  beginMember(key);
  fprintf(f_, "%u", value);
}

void JsonWriter::field(const char* key, bool value) {
  beginMember(key);
  fputs(value ? "true" : "false", f_);
}

void JsonWriter::doubleArrayField(const char* key, const std::vector<double>& values) {
  arrayField(key);
  for (size_t i = 0; i < values.size(); ++i) {
    value(values[i]);
  }
  endArray();
}
