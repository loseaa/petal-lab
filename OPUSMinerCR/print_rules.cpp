/* print_rules.cpp - a module of OPUS Miner providing print_rules, a procedure to print the top-k rules to a file.
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
#include <vector>
#include <algorithm>
#include "OMCRglobals.h"
#include "itemset.h"
#include "OMCRutils.h"

namespace OPUSMinerCR {

bool valgt(rule i1, rule i2) {
  return i1.value > i2.value;
}

void print_rule(FILE *f, const rule &r) {
  itemset::const_iterator item_it;

  for (item_it = r.lhs.begin(); item_it != r.lhs.end(); item_it++) {
    if (item_it != r.lhs.begin()) {
      fputc(',', f);
    }

    fprintf(f, "%s", itemNames[*item_it].c_str());
  }
  fprintf(f, " -> %s", itemNames[r.rhs].c_str());

  fprintf(f, " [cov=%d,sup=%d,val=%f", r.cover, r.support, r.value);
  fprintf(f, ",p=%g]", r.p);

  putc('\n', f);
}

void print_rules(FILE *f, std::vector<rule> &rules) {
  int i;
  
  for (i = 2; i < alpha.size(); i++) {
      fprintf(f, "Alpha for size %d = %g\n", i, alpha[i]);
  }

  fprintf(f, "\nSELF-SUFFICIENT RULES:\n");

  std::sort(rules.begin(), rules.end(), valgt);

  std::vector<rule>::const_iterator it;

  int failed_count = 0;

  for (it = rules.begin(); it != rules.end(); it++) {
    if (!it->self_sufficient) {
      failed_count++;
    }
    else {
      print_rule(f, *it);
    }
  }

  if (failed_count) {
    fprintf(f, "\n%d rules failed test for self sufficiency\n", failed_count);
    for (it = rules.begin(); it != rules.end(); it++) {
      if (!it->self_sufficient) {
        print_rule(f, *it);
      }
    }
  }
}

}