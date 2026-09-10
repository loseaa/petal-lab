/* Open source system for classification learning from very large data
** Abstract class for a filter for an instance stream
** Copyright (C) 2012 Geoffrey I Webb
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program. If not, see <http://www.gnu.org/licenses/>.
**
** Please report any bugs to Geoff Webb <geoff.webb@monash.edu>
*/
#pragma once

#include "derivedInstanceStream.h"


/**
<!-- globalinfo-start -->
 * Class for a filtered instance stream.<p>
 * The filter can be used in either of two modes<br>
 * - directly as an instance stream, in which case it extracts the instances from the source stream and converts them to serve up to the consumer, or<br>
 * - the convert() method can directly translate an instance from the base stream into an instance for the output stream.<p>
 * The default methods pass all calls through to the source stream.<p>
 * The ClassSafeInstanceStreamFilter subclass is for stream filters that do not change the number of classes<p>
 *
 * THIS IS A DEPRECIATED FORM - USE FilteredInstanceStream BY PREFERENCE
 *
 * @author Geoff Webb (geoff.webb@monash.edu)
 */

class FilteredInstanceStream: public DerivedInstanceStream
{
public:
  FilteredInstanceStream();
  virtual ~FilteredInstanceStream(void);

  virtual void convert(const instance &in, instance &out);  ///< create a new instance for the output stream by converting an instance from the source stream
};

// An FilteredInstanceStream that does not change the number of classes
class ClassSafeInstanceStreamFilter: public FilteredInstanceStream
{
};

//class FilterRegistry {
//public:
//  inline void addFilter(char* n, FilteredInstanceStream* (*c)()) { filters_.push_back(*(new FilterRec(n,c))); }
//
//private:
//  class FilterRec {
//  public:
//    FilterRec(char* n, FilteredInstanceStream* (*c)()) : name(n), constructor(c) {};
//
//  private:
//    char* name;
//    FilteredInstanceStream* (*constructor)();
//  };
//
//  std::vector<FilterRec> filters_;
//};
//
//// must encapsulate registry in a function to ensure initialisation before first use (as it is called during static initialisation)
//FilterRegistry& theFilterRegistry();
//
//class RegisterFilter {
//  RegisterFilter(char* n, FilteredInstanceStream* (*c)()) { theFilterRegistry().addFilter(n, c); }
//}
