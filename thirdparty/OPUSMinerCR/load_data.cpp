/* load_data.cpp - a module of OPUS Miner providing load_data, a procedure to read transaction data from a file.
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
*/

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "OMCRglobals.h"
#include "load_data.h"


namespace OPUSMinerCR {


void load_data(const char *filename) {
  int c;  // next character
  std::string s;
  std::map<std::string, itemID> itemstrs;

  FILE *f = fopen(filename, "r");

  if (f == NULL) {
    fprintf(stderr, "Cannot open input file '%s'\n", filename);
    exit(1);
  }

  c = fgetc(f);

  while (c != EOF) {
    if (c == '\n') {
      // new transaction
      noOfTransactions++;
      c = fgetc(f);
    }
    else {
      // get the next item name
      s.clear();

      while (c > ' ' && c != ',' && c != EOF) {
        s.push_back(c);
        c = fgetc(f);
      }

      if (!s.empty()) {
        // find its id
        itemID thisid;

        std::map<std::string, itemID>::const_iterator it = itemstrs.find(s);

        if (it == itemstrs.end()) {
          // if it doesn't have an id, assign one
          thisid = itemNames.size();
          itemstrs[s] = thisid;
          itemNames.push_back(s);
          noOfItems = itemNames.size();
          tids.resize(noOfItems);
        }
        else {
          thisid = it->second;
        }

        // insert the current TID into the tids for thisval, unless it has already been inserted
        if (tids[thisid].empty() || *tids[thisid].rbegin() != noOfTransactions) {
          tids[thisid].push_back(noOfTransactions);
        }
      }

      // skip delimiters
      while (c != EOF && c != '\n' && (c <= ' ' || c == '\t' || c == ',')) {
        c = fgetc(f);
      }
    }
  }
}

}
