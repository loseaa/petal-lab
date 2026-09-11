/* Open source system for classification learning from very large data
** Class for a discretisation filter for instance streams
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
#include "filteredInstanceStream.h"
#include "globals.h"
#include "discretiser.h"
#include <vector>
#include <string>
#include <stdio.h>

// creates a stream with additional attributes which are the product of each of the original attributes

class InstanceStreamFeatureConstructor :
  public ClassSafeInstanceStreamFilter
{
public:
  InstanceStreamFeatureConstructor(char*const*& argv, char*const* end);
  InstanceStreamFeatureConstructor(InstanceStream *src);
  ~InstanceStreamFeatureConstructor(void);

  void rewind();                                              ///< return to the first instance in the stream
  bool advance();                                             ///< advance, discarding the next instance in the stream.  Return true iff successful.
  bool advance(instance &inst);                               ///< advance to the next instance in the stream.  Return true iff successful. @param inst the instance record to receive the new instance.
  bool advanceNumeric(instance &inst);                        ///< advance to the next instance in the stream without discretizing numeric values.  Return true iff successful. @param inst the instance record to receive the new instance.
  bool isAtEnd() const;                                             ///< true if we have advanced past the last instance
  InstanceCount size();                                       /// the number of instances in the stream. This may require a pass through the stream to determine so should be used only if absolutely necessary.  The stream state is undefined after a call to size(), so a rewind shouldbe performed before the next advance.

  void setSource(InstanceStream &source);                     ///< set the source for the filter

  virtual void convert(const instance &inst, instance &instDisc);                      ///< return the discretised version of the instance. @param inst the instance to discretise.

  // an attribute value pair
  class catAttVal {
  public:
    CategoricalAttribute att;
    CatValue val;
  };

  class numInterval {
  public:
    NumericAttribute att;
    NumValue lower;       // value is greater than this
    NumValue upper;       // value is <= this
  };

  class numMissing {
  public:
    NumericAttribute att;
  };

  // a compound feature is specied by the set of attribute vlaue pairs and numeric intervals that consistute it
  // its value is true iff all the clauses are satisfied
  class compoundFeature {
  public:
    compoundFeature() {}
    ~compoundFeature() {}

    bool isTrue(instance &inst) {
      for (std::vector<catAttVal>::const_iterator it = catAttVals_.begin(); it != catAttVals_.end(); it++) {
        if (inst.getCatVal(it->att) != it->val) return false;
      }
      for (std::vector<numInterval>::const_iterator it = numIntervals_.begin(); it != numIntervals_.end(); it++) {
        const NumValue v = inst.getNumVal(it->att);
        if (v <= it->lower) return false;
        if (v > it->upper) return false;
      }
      return true;
    };

    std::vector<catAttVal> catAttVals_;
    std::vector<numInterval> numIntervals_;
    std::vector<numMissing> numMissing_;
  };

  class FCMetaData : public InstanceStream::MetaDataFilter {
  public:
    FCMetaData();
    virtual unsigned int getNoCatAtts() const;                          ///< return the number of categorical attributes
    virtual bool hasCatMissing(const CategoricalAttribute att) const;          ///< return whether a categorical attribute contains missing values
    virtual bool hasNumMissing(const NumericAttribute att) const;              ///< return whether a numeric attribute contains missing values
    virtual unsigned int getNoValues(const CategoricalAttribute att) const;   ///< return the number of values for a categorical attribute
    virtual const char* getCatAttName(const CategoricalAttribute att) const;  ///< return the name for a categorical Attribute
    virtual const char* getCatAttValName(const CategoricalAttribute att, const CatValue val) const; ///< return the name for a categorical attribute value
    virtual unsigned int getNoNumAtts() const;                          ///< return the number of numeric attributes
    virtual const char* getNumAttName(const NumericAttribute att) const;      ///< return the name for a numeric attribute
    virtual unsigned int getPrecision(const NumericAttribute att) const;      ///< return the precision to which values of a numeric attribute should be output
    virtual const char* getName() const ;                                      ///< return a string that gives a meaningful name for the stream


    void const name(const compoundFeature& f, std::string& s) const {
      for (std::vector<catAttVal>::const_iterator it = f.catAttVals_.begin(); it != f.catAttVals_.end(); it++) {
        // categorical attribute
        if (!s.empty()) {
          s += '&';
        }

        s += getCatAttName(it->att);
        s += '=';
        s += getCatAttValName(it->att, it->val);
      }
      for (std::vector<numInterval>::const_iterator it = f.numIntervals_.begin(); it != f.numIntervals_.end(); it++) {
        char buf[100];

        if (!s.empty()) {
          s += '&';
        }

        if (it->upper == std::numeric_limits<NumValue>::max()) {
          s += getNumAttName(it->att);
          sprintf(buf, ">" PETAL_FLOAT_FMT, it->lower);
          s += buf;
        }
        else if (it->lower == -std::numeric_limits<NumValue>::max()) {
          s += getNumAttName(it->att);
          sprintf(buf, "<=" PETAL_FLOAT_FMT, it->upper);
          s += buf;
        }
        else {
          sprintf(buf, PETAL_FLOAT_FMT "<", it->lower);
          s += buf;
          s += getNumAttName(it->att);
          sprintf(buf, "<=" PETAL_FLOAT_FMT, it->upper);
          s += buf;
        }
      }
      for (std::vector<numMissing>::const_iterator it = f.numMissing_.begin(); it != f.numMissing_.end(); it++) {
        if (!s.empty()) {
          s += '&';
        }

        s += getNumAttName(it->att);
        s += "=MISSING";
      }
    };

  public:
    std::vector<compoundFeature> newFeatures_;  ///< the constructed features
  };

  inline MetaData* getMetaData() { return &metaData_; }

private:
  bool doNum_;                                ///< true iff features should also be constructed for numeric variables
  unsigned int noConstructed_;                ///< the number of features to construct
  std::vector<CategoricalAttribute> itemToAtt_;///< for each OPUSMinerCR item give the petal attribute
  std::vector<CatValue> itemToVal_;            ///< or each OPUSMinerCR item give the petal value
  std::vector<std::vector<NumValue> > cuts_;  ///< the cut points that are used for each numeric value
  instance sourceInst_;                       ///< the current instance from the source stream. Maintain one instance record to save repeated construction/destruction.
  FCMetaData metaData_;                       ///< the revised metadata
};
