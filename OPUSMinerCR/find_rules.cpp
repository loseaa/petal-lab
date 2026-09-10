/* find_rules.cpp - a module of OPUS Miner providing find_rules, a function to find productive non-redundant rules.
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


#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <limits>
#include <iterator>
#include <malloc.h>
#include <assert.h>
#include <map>
#include <algorithm>

#include "OMCRglobals.h"
#include "find_rules.h"
#include "OMCRutils.h"
#include "fisher.h"
#include "itemQClass.h"
#include "rule.h"

//#define NOAPRIORI


namespace OPUSMinerCR {
// the minimum leverage of an itemset in the top-k so far
// any itemset whose leverage does not exceed this value cannot enter the top-k
float minValue = -std::numeric_limits<float>::max();
int minSup = 1;

// for each itemset explored for which supersets might be in the best k, keep the count
std::map<itemset, int> TIDCount;

#ifdef SIXTEENBIT
// special map for pairs in order to save space
std::map<int, int> TIDPairCount;
#endif

// access function for TIDCount
inline bool getTIDCount(itemset is, int &count) {
  if (is.size() == 1) {
    count = tids[*is.begin()].size();
    return true;
  }
  #ifdef SIXTEENBIT
  else if (is.size() == 2) {
    std::map<int, int>::const_iterator it = TIDPairCount.find((*is.begin()<<16) + *is.rbegin());

    if (it == TIDPairCount.end()) {
      count = 0;
      return false;
    }
    else {
      count = it->second;
      return true;
    }
  }
  #endif
  else {
    std::map<itemset, int>::const_iterator it = TIDCount.find(is);

    if (it == TIDCount.end()) {
      count = 0;
      return false;
    }
    else {
      count = it->second;
      return true;
    }
  }
}

// check whether the count was stored. If not, we know that we have already determined that no superset can be in the best k
inline bool checkTIDCount(itemset is) {
  if (is.size() == 1) {
    // all single itemset counts are stored
    return true;
  }
  #ifdef SIXTEENBIT
  else if (is.size()==2) {
    std::map<int, int>::const_iterator it = TIDPairCount.find((*is.begin()<<16) + *is.rbegin());

    if (it == TIDPairCount.end()) return false;
    else return true;
  }
  #endif
  else {
    std::map<itemset, int>::const_iterator it = TIDCount.find(is);

    if (it == TIDCount.end()) return false;
    else return true;
  }
}

// array of element values used by itemgt
float *sortval;

// for sorting an array of items on sortval
int itemgt(const void *i1, const void *i2) {
  if (sortval[*static_cast<const itemID*>(i1)] > sortval[*static_cast<const itemID*>(i2)]) return -1;
  else return 1;
}

// for sorting an array of items on sortval
int itemlt(const void *i1, const void *i2) {
  if (sortval[*static_cast<const itemID*>(i1)] < sortval[*static_cast<const itemID*>(i2)]) return -1;
  else return 1;
}

// check whether the LHS passes the apriori test and whether its supersets will be redundant
void checkImmediateSubsets(itemset &is, const int isCnt, bool &redundant, bool &apriori) {
  itemset subset = is;
  itemset::const_iterator it;

  redundant = false;
  apriori = false;

  for (it = is.begin(); it != is.end(); it++) {
    int subsetCnt;

    subset.erase(*it);

    if (!getTIDCount(subset, subsetCnt)) {
      redundant = false;
#ifndef NOAPRIORI
      apriori = true;
#endif
			subset.insert(*it);
      return;
    }
    
    if (redundancyTests && subsetCnt == isCnt) {
      redundant = true;
    }

    subset.insert(*it);
  }

  return;
}


// whether the itemset is productive, whether it is redundant and whether it is possible to determine that all supersets of is will be redundant
// return true iff is is productive and not redundant, val > minValue and p <= alpha
bool checkSubsetsX(itemset &sofar, itemset &remaining, const itemID rhs, const itemID limit, const int coverCnt, const int supCnt, double &p, const double alpha, bool &apriori) {
  int sofarCnt;
  int sofarsupCnt;
  int remainingCnt;
  int remainingsupCnt;

  assert(sofar.size() > 1);
  
  if (!getTIDCount(sofar, sofarCnt)) {
#ifndef NOAPRIORI
    apriori = true;
#endif
    return false;
  }

  sofar.insert(rhs);
  if (!getTIDCount(sofar, sofarsupCnt)) {
#ifndef NOAPRIORI
    apriori = true;
#endif
    return false;
  }
  sofar.erase(rhs);

  double this_p = fisherTest(sofarCnt - sofarsupCnt - coverCnt + supCnt, sofarsupCnt - supCnt, coverCnt - supCnt, supCnt);

  if (this_p > p) {
    p = this_p;
    if (p > alpha) {
      apriori = false;
      return false;
    }
  }

  if (!getTIDCount(remaining, remainingCnt)) {
#ifndef NOAPRIORI
    apriori = true;
#endif
    return false;
  }

  if (remaining.size() == 1) {
    // do not cache sup for single item lhs
    remainingsupCnt = count_intersection(tids[*remaining.begin()], tids[rhs]);
  }
  else {
    remaining.insert(rhs);
    if (!getTIDCount(remaining, remainingsupCnt)) {
#ifndef NOAPRIORI
      apriori = true;
#endif
      return false;
    }
    remaining.erase(rhs);
  }

  this_p = fisherTest(remainingCnt - remainingsupCnt - coverCnt + supCnt, remainingsupCnt - supCnt, coverCnt - supCnt, supCnt);

  if (this_p > p) {
    p = this_p;
    if (p > alpha) {
      apriori = false;
      return false;
    }
  }

  if (remaining.size() > 1) {
    itemset new_remaining(remaining);

    itemset::const_iterator it;

    for (it = remaining.begin(); it != remaining.end() && *it < limit; it++) {
      sofar.insert(*it);
      new_remaining.erase(*it);

      if (!checkSubsetsX(sofar, new_remaining, rhs, *it, coverCnt, supCnt, p, alpha, apriori)) {
        return false;
      }

      sofar.erase(*it);
      new_remaining.insert(*it);
    }
  }

  return p <= alpha;
}

// calculates leverage and p
// return true iff val > minValue and p <= alpha
bool checkSubsets(itemID item, itemset &is, itemID rhs, const int coverCnt, const double cover, const int parentCoverCnt, const double parentCover, const int supCnt, const double sup, double &p, const double alpha, bool &apriori) {
  assert(is.size() > 1);

  // do test for the full rule
  const int itemCnt = tids[rhs].size();

  p = fisher(supCnt, itemCnt, coverCnt);

  if (p > alpha) {
    apriori = false;
    return false;
  }

  if (is.size() == 1) {
    // nothing further to do
  }
  else if (is.size() == 2) {
    itemset subset = is;
    itemset::const_iterator it;

    apriori = false;

    for (it = is.begin(); it != is.end(); it++) {
      int subsetCnt;
      int supsubsetCnt;

      subset.erase(*it);

      if (!getTIDCount(subset, subsetCnt)) {
        apriori = true;
        return false;
      }

      // We do not cache the single item antecedents with consequents
      //if (!getTIDCount(supsubset, supsubsetCnt)) {
      //  apriori = true;
      //  return false;
      //}

      supsubsetCnt = count_intersection(tids[*subset.begin()], tids[rhs]);

      double this_p = fisherTest(subsetCnt - supsubsetCnt - coverCnt + supCnt, supsubsetCnt - supCnt, coverCnt - supCnt, supCnt);

      if (this_p > p) {
        p = this_p;

        if (this_p > alpha) {
          apriori = false;
          return false;
        }
      }

      subset.insert(*it);
    }
  }
  else {
    // is.size() > 2
    int subsetCnt;
    int supsubsetCnt;

    // test against item -> rhs
    subsetCnt = tids[item].size();
    supsubsetCnt = count_intersection(tids[item], tids[rhs]);

    double this_p = fisherTest(subsetCnt - supsubsetCnt - coverCnt + supCnt, supsubsetCnt - supCnt, coverCnt - supCnt, supCnt);

    if (this_p > p) {
      p = this_p;

      if (this_p > alpha) {
        apriori = false;
        return false;
      }
    }

    itemset sofar;
    itemset remaining(is);

    // test against LHS\item -> RHS
    remaining.erase(item);

    if (!getTIDCount(remaining, subsetCnt)) {
#ifndef NOAPRIORI
      apriori = true;
#endif
      return false;
    }

    remaining.insert(rhs);
    if (!getTIDCount(remaining, supsubsetCnt)) {
#ifndef NOAPRIORI
      apriori = true;
#endif
      return false;
    }

    remaining.erase(rhs);

    sofar.insert(item);

    this_p = fisherTest(subsetCnt - supsubsetCnt - coverCnt + supCnt, supsubsetCnt - supCnt, coverCnt - supCnt, supCnt);

    if (this_p > p) {
      p = this_p;

      if (this_p > alpha) {
        apriori = false;
        return false;
      }
    }

    itemset::const_iterator it;

    for (it = is.begin(); it != is.end(); it++) {
      if (*it != item) {
        sofar.insert(*it);
        remaining.erase(*it);

        if (!checkSubsetsX(sofar, remaining, rhs, *it, coverCnt, supCnt, p, alpha, apriori)) {
          return false;
        }

        sofar.erase(*it);
        remaining.insert(*it);
      }
    }
  }

  apriori = false;

  return p <= alpha;
}

// insert r into the collection of k best rules
inline void insert_rule(const rule &r) {
  if (rules.size() >= k) {
    rules.pop();
  }
  rules.push(r);
  if (rules.size() == k) {
    const float newMin = rules.top().value;
    if (newMin > minValue) {
      minValue = newMin;
    }
  }
}

// perform OPUS search for specialisations of is (which covers cover) using the candidates in queue q
// maxItemSup is the maximum of the supports of all individual items in is
void opus(itemset &lhs, tidset &cover, itemset &rhsAvail, itemQClass &lhsAvail, const bool one_rule_per_lhs) {
  unsigned int i;
  const float parentSup = countToSup(cover.size());
  const int depth = lhs.size()+1;
  itemset newRHSAvail;

  tidset newCover;
  itemQClass newQ;

  for (i = 0; i < lhsAvail.size(); i++) {
    const itemID item = lhsAvail[i].item;
    int count;
    double ubVal = 0.0; // an upper bound on the value of any rule for a superset of the current LHS

    // determine the number of TIDs that the new itemset covers
    intersection(newCover, cover, tids[item]);
    count = newCover.size();

    if (count == 0) continue; // an empty antecedent cannot form a productive rule

    bool lhsRedundant = false;
    bool lhsApriori = false;

    lhs.insert(item);

    checkImmediateSubsets(lhs, count, lhsRedundant, lhsApriori);

    // performing OPUS pruning - if this test fails, the item will not be included in any superset of lhs
    if (!lhsApriori) {
      const float new_cover = countToSup(count);
      std::priority_queue<rule> new_rules;

      for (itemset::const_iterator rhs = rhsAvail.begin(); rhs != rhsAvail.end(); rhs++) {
        const int supCount = count_intersection(newCover, tids[*rhs]);

        if (supCount >= minSup) {
          const double sup = countToSup(supCount);

          // this is a lower bound on the p value that may be obtained for this itemset or any superset
          //const p_value lb_p = fisher(count, newMaxItemCount, count);

          // calculate an upper bound on the value that can be obtained by this itemset or any superset

          const double optSup = std::min(0.5, sup);

          const double ubRHS = searchByLift ? ((count == 0) ? 0.0 : (1.0 / countToSup(tids[*rhs].size())))
            : optSup - optSup * optSup;

          // performing OPUS pruning - if this test fails, the item will not be included in any superset of lhs for this rhs
          if (ubRHS > minValue) {
            // only continue if there is any possibility of this itemset or its supersets entering the list of best new_rules
            float val;
            double p;
            bool redundant = false;
            bool apriori = false;

            if (checkSubsets(item, lhs, *rhs, count, new_cover, cover.size(), parentSup, supCount, sup, p, getAlpha(depth), apriori)) {
              val = searchByLift ? ((count == 0) ? 0.0 : (1.0 / countToSup(tids[*rhs].size())))
                : sup - countToSup(tids[*rhs].size()) * new_cover;
              if (val > minValue) {
                if (p <= getAlpha(depth)) {
                  rule newRule;

                  newRule.lhs = lhs;
                  newRule.rhs = *rhs;
                  newRule.cover = count;
                  newRule.support = supCount;
                  newRule.value = val;
                  newRule.p = p;
                  new_rules.push(newRule);
                }
              }
            }

            // performing OPUS pruning - if this test fails, the item will not be included in any superset of lhs for this RHS
            if (!lhsRedundant) {
              newRHSAvail.insert(*rhs);
              ubVal = std::max(ubRHS, ubVal);
              
              // cache the count for the lhs U rhs
              lhs.insert(*rhs);
              TIDCount[lhs] = supCount;
              lhs.erase(*rhs);
            }
          }
        }
      }
        
      if (!new_rules.empty()) {
        if (one_rule_per_lhs) {
          insert_rule(new_rules.top());
        }
        else {
          while (!new_rules.empty()) {
            insert_rule(new_rules.top());
            new_rules.pop();
          }
        }
      }
      
      if (!newRHSAvail.empty()) {
        TIDCount[lhs] = count;

        if (!newQ.empty()) {
          // there are only more nodes to expand if there is a queue of items to consider expanding it with
          opus(lhs, newCover, newRHSAvail, newQ, one_rule_per_lhs);
        }

        newQ.insert(ubVal, item);
      }
    }
    
    lhs.erase(item);
  }
}

void find_rules(itemset &rhsAvail) {
  itemQClass q; // a queue of items, to be sorted on an upper bound on value
  itemID i;

  minValue = -std::numeric_limits<float>::max();
  noOfRHSvals = rhsAvail.size();

  // initalise q - the queue of items ordered on an upper bound on value
  for (i = 0; i < noOfItems; i++) {
    if (rhsAvail.find(i) == rhsAvail.end()) {
      const int c = tids[i].size();

      const float sup = countToSup(c);
      const float ubVal = searchByLift ? 1.0 / sup
                                       //: (sup > 0.5 ? 0.25 : sup - sup * sup);
                                       : (sup > 0.5 ? sup : sup - sup * sup);
      
      // make sure that the support is high enough for it to be possible to create a significant itemset
      if (fisher(c, c, c) <= getAlpha(2)) {
        q.append(ubVal, i); // it is faster to sort the q once it is full rather than doing an insertion sort
      }
    }
  }

  q.sort();

  itemQClass newq;  // this is the queue of items that will be available for the item currently being explored

  if (!q.empty()) {
    newq.insert(q[0].ubVal, q[0].item);

    float prevMinVal = minValue; // remember the current minValue, and output an update if it improves in this iteration of the loop

    itemset is;

    // we are stepping through all associations of i with j<i, so the first value of i that will have effect is 1
    for (i = 1; i < q.size() && q[i].ubVal > minValue; i++) {
      const itemID item = q[i].item;

      is.clear();
      is.insert(item);

      opus(is, tids[item], rhsAvail, newq, true);

      newq.append(q[i].ubVal, item);

      if (prevMinVal < minValue) {
        printf("<%f>",minValue);
        prevMinVal = minValue;
      }
      else putchar('.');
      fflush(stdout);
    }

    putchar('\n');
  }
}

}
