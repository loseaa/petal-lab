#include "instanceStreamFeatureConstructor.h"
#include "globals.h"
#include "mtrand.h"
#include "utils.h"

#include "OPUSMinerCR/OMCRglobals.h"
#include "OPUSMinerCR/find_rules.h"

// discretisers
#include "eqDepthDiscretiser.h"
#include "MDLDiscretiser.h"
#include "MDLBinaryDiscretiser.h"

// RegisterFilter* r = new RegisterFilter("constructor", InstanceStreamFeatureConstructor::InstanceStreamFeatureConstructor);

InstanceStreamFeatureConstructor::InstanceStreamFeatureConstructor(char*const*& argv, char*const* end)
  : doNum_(true)
{
  // get arguments
  while (argv != end) {
    if (streq(argv[0], "+nonum", false)) {
      doNum_ = false;
      ++argv;
    }
    else if (**argv == '+' && argv[0][1] == 'n') {
      getUIntFromStr(argv[0]+2, noConstructed_, "n");
      ++argv;
    }
    else {
      break;  // do not consume the remaining arguments
    }
  }

  InstanceStream::metaData_ = &metaData_;
}

InstanceStreamFeatureConstructor::InstanceStreamFeatureConstructor(InstanceStream *src)
{
  InstanceStream::metaData_ = &metaData_;
  setSource(*src);
}

InstanceStreamFeatureConstructor::~InstanceStreamFeatureConstructor(void) {
}

/// set the source for the filter
void InstanceStreamFeatureConstructor::setSource(InstanceStream &src) {
  source_ = &src;
  metaData_.setSource(src.getMetaData());
  std::vector<OPUSMinerCR::itemID> catAttToItem; // the index of the first OPUS Miner CR item for each categorical attribute
  std::vector<OPUSMinerCR::itemID> numAttToItem; // the index of the first OPUS Miner CR item for each numeric attribute
  OPUSMinerCR::itemID classesToItem;

  sourceInst_.init(src);

  itemToAtt_.clear();
  itemToVal_.clear();
  metaData_.newFeatures_.clear();

  OPUSMinerCR::noOfItems = 0;

  for (CategoricalAttribute ca = 0; ca < src.getNoCatAtts(); ca++) {
    catAttToItem.push_back(OPUSMinerCR::noOfItems);
    for (CatValue v = 0; v < src.getNoValues(ca); v++) {
      itemToAtt_.push_back(ca);
      itemToVal_.push_back(v);
    }
    OPUSMinerCR::noOfItems += src.getNoValues(ca);
  }

  if (doNum_ && src.getNoNumAtts() > 0) {
    // extract the numeric values and find cutpoints to discretise them
    std::vector<std::vector<NumValue> > vals(src.getNoNumAtts());
    std::vector<CatValue> classes;
    unsigned int count = 0;
    MDLBinaryDiscretiser theDiscretiser;

    src.rewind();

    while (src.advance(sourceInst_)) {
      count++;  // keep track of the number of values seen
      classes.push_back(sourceInst_.getClass());
      for (NumericAttribute a = 0; a < src.getNoNumAtts(); a++) {
        vals[a].push_back(sourceInst_.getNumVal(a));
      }
    }

    cuts_.resize(src.getNoNumAtts());

    for (NumericAttribute na = 0; na < src.getNoNumAtts(); na++) {
      // discretise then set up the value names
      numAttToItem.push_back(OPUSMinerCR::noOfItems);
      theDiscretiser.discretise(vals[na], classes, src.getNoClasses(), cuts_[na]);
      for (CatValue v = 0; v < cuts_[na].size()+1; v++) {
        itemToAtt_.push_back(src.getNoCatAtts()+na);
        itemToVal_.push_back(v);
      }
      OPUSMinerCR::noOfItems += cuts_[na].size() + 1;
      if (src.hasNumMissing(na)) {
        // add a missing value
        OPUSMinerCR::noOfItems++;
        itemToAtt_.push_back(src.getNoCatAtts()+na);
        itemToVal_.push_back(cuts_[na].size()+1);
      }
    }
  }

  classesToItem = OPUSMinerCR::noOfItems;
  OPUSMinerCR::noOfItems += src.getNoClasses();
  OPUSMinerCR::tids.clear();
  OPUSMinerCR::tids.resize(OPUSMinerCR::noOfItems);

  // convert the data to OPUS Miner CR format
  src.rewind();

  TID tid = 0;

  while (src.advance(sourceInst_)) {
    for (CategoricalAttribute ca = 0; ca < src.getNoCatAtts(); ca++) {
      OPUSMinerCR::tids[catAttToItem[ca]+sourceInst_.getCatVal(ca)].push_back(tid);
    }
    
    if (doNum_) {
      for (NumericAttribute a = 0; a < src.getNoNumAtts(); a++) {
        if (sourceInst_.isMissing(a)) {
          OPUSMinerCR::tids[numAttToItem[a]+cuts_[a].size()+1].push_back(tid);
        }
        else {
          NumValue v = sourceInst_.getNumVal(a);
          CatValue dv;

          if (v > cuts_[a].back()) {
            // > last cut
            dv = cuts_[a].size();
          }
          else {
            // find the discretised value
            for (dv = 0; dv < cuts_[a].size() && v > cuts_[a][dv]; dv++) ;
          }

          OPUSMinerCR::tids[numAttToItem[a]+dv].push_back(tid);
        }
      }
    }
    
    OPUSMinerCR::tids[classesToItem + sourceInst_.getClass()].push_back(tid);
    
    tid++;
  }

  OPUSMinerCR::noOfTransactions = tid;

  OPUSMinerCR::itemset rhs;

  for (OPUSMinerCR::itemID i = 0; i < src.getNoClasses(); i++) {
    rhs.insert(classesToItem+i);
  }

  if (verbosity > 0) printf("CONSTRUCTING FEATURES\n");

  OPUSMinerCR::k = noConstructed_;
  //OPUSMinerCR::redundancyTests = false;

  OPUSMinerCR::find_rules(rhs);

  if (verbosity > 0) printf("CONSTRUCTED FEATURES\n");

  while (!OPUSMinerCR::rules.empty()) {
    compoundFeature feat;

    OPUSMinerCR::itemset::const_iterator item_it;
    const OPUSMinerCR::rule& r = OPUSMinerCR::rules.top();

    for (item_it = r.lhs.begin(); item_it != r.lhs.end(); item_it++) {
      //if (verbosity > 0) {
      //  if (item_it != r.lhs.begin()) {
      //    putchar(',');
      //  }
      //}

      unsigned int att = itemToAtt_[*item_it];

      if (att >= src.getNoCatAtts()) {
        // numeric attribute
        att -= src.getNoCatAtts();
        if (itemToVal_[*item_it] == cuts_[att].size()+1) {
          // missing value
          numMissing newMissing;

          newMissing.att = att;
          feat.numMissing_.push_back(newMissing);
        }
        else {
          numInterval newInterval;
          newInterval.att = att;

          if (itemToVal_[*item_it] == cuts_[att].size()) {
            newInterval.lower = cuts_[att].back();
            newInterval.upper = std::numeric_limits<NumValue>::max();
          }
          else if (itemToVal_[*item_it] == 0) {
            newInterval.lower = -std::numeric_limits<NumValue>::max();
            newInterval.upper = cuts_[att][0];
          }
          else {
            newInterval.lower = cuts_[att][itemToVal_[*item_it]-1];
            newInterval.upper = cuts_[att][itemToVal_[*item_it]];
          }

          feat.numIntervals_.push_back(newInterval);
        }
        //if (verbosity > 0) {
        //  sprintf(buf, src.getNumAttName(att));
        //  if (itemToVal_[*item_it] == cuts_[att].size()+1) {
        //    sprintf(buf+strlen(buf), " is missing");
        //  }
        //  else if (itemToVal_[*item_it] == cuts_[att].size()) {
        //    sprintf(buf+strlen(buf), ">%g", cuts_[att].back());
        //  }
        //  else {
        //    sprintf(buf+strlen(buf), "<=%g", cuts_[att][itemToVal_[*item_it]]);
        //  }
        //}
      }
      else {
        // categorical attribute
        catAttVal newCatAttVal;
        newCatAttVal.att = att;
        newCatAttVal.val = itemToVal_[*item_it];
        feat.catAttVals_.push_back(newCatAttVal);
        // categorical attribute
        //if (verbosity > 0) {
        //  printf("%s=%s", src.getCatAttName(att), src.getCatAttValName(att, itemToVal_[*item_it]));
        //}
      }
    }

    if (verbosity > 0) {
      // print the constructed feature
      std::string fstr;
      metaData_.name(feat, fstr);

      printf("%s -> %s=%s", fstr.c_str(), src.getClassAttName(), src.getClassName(r.rhs-classesToItem));

      printf(" [cov=%d,sup=%d,val=" PETAL_FLOAT_FMT, r.cover, r.support, r.value);
      printf(",p=" PETAL_FLOAT_FMT "]", r.p);

      putchar('\n');
    }

    metaData_.newFeatures_.push_back(feat);
    
    //  candidates.push_back(rules.top());
    OPUSMinerCR::rules.pop();
  }

  if (verbosity > 0) putchar('\n');

  rewind();
}

/// return to the first instance in the stream
void InstanceStreamFeatureConstructor::rewind() {
	source_->rewind();
}

/// advance, discarding the next instance in the stream.  Return true iff successful.
bool InstanceStreamFeatureConstructor::advance() {
	return source_->advance();
}


void InstanceStreamFeatureConstructor::convert(const instance &in, instance &out) {
  out.setClass(in.getClass());

  // copy numeric atts
  for (NumericAttribute i = 0; i < source_->getMetaData()->getNoNumAtts(); i++) {
    out.setNumVal(i, in.getNumVal(i));
  }

  // copy categorical atts
  for (CategoricalAttribute i = 0; i < source_->getMetaData()->getNoCatAtts(); i++) {
    out.setCatVal(i, in.getCatVal(i));
  }

  for (CategoricalAttribute i = 0; i < metaData_.newFeatures_.size(); i++) {
    CatValue v = 1;
    compoundFeature& f = metaData_.newFeatures_[i];

    for (std::vector<catAttVal>::const_iterator it = f.catAttVals_.begin(); it != f.catAttVals_.end(); it++) {
      // categorical attribute
      if (in.getCatVal(it->att) != it->val) {
        v = 0;
        break;
      }
    }
    if (v != 0) {
      for (std::vector<numInterval>::const_iterator it = f.numIntervals_.begin(); it != f.numIntervals_.end(); it++) {
        NumValue nv = in.getNumVal(it->att);
        if (nv >= it->upper || nv < it->lower) {
          v = 0;
          break;
        }
      }
    }
    if (v != 0) {
      for (std::vector<numMissing>::const_iterator it = f.numMissing_.begin(); it != f.numMissing_.end(); it++) {
        if (!in.isMissing(it->att)) {
          v = 0;
          break;
        }
      }
    }

    out.setCatVal(source_->getMetaData()->getNoCatAtts()+i, v);
  }

}


/// advance to the next instance in the stream. Return true iff successful. @param inst the instance record to receive the new instance. 
bool InstanceStreamFeatureConstructor::advance(instance &inst) {
	if (!source_->advance(sourceInst_))
		return false;

	convert(sourceInst_, inst);

	return true;
}


/// true if we have advanced past the last instance
bool InstanceStreamFeatureConstructor::isAtEnd() const {
	return source_->isAtEnd();
}

/// the number of instances in the stream. This may require a pass through the stream to determine so should be used only if absolutely necessary.
InstanceCount InstanceStreamFeatureConstructor::size() {
	return source_->size();
}


InstanceStreamFeatureConstructor::FCMetaData::FCMetaData() {
}

// return the number of categorical attributes
unsigned int InstanceStreamFeatureConstructor::FCMetaData::getNoCatAtts() const {
  return source_->getNoCatAtts() + newFeatures_.size();
}

// return whether a categorical attribute contains missing values
bool InstanceStreamFeatureConstructor::FCMetaData::hasCatMissing(const CategoricalAttribute att) const {
  if (att < source_->getNoCatAtts()) return source_->hasCatMissing(att);
  else return false;
}

// return whether a numeric attribute contains missing values
bool InstanceStreamFeatureConstructor::FCMetaData::hasNumMissing(const NumericAttribute att) const {
  return source_->hasNumMissing(att);
}

// return the number of values for a categorical attribute
unsigned int InstanceStreamFeatureConstructor::FCMetaData::getNoValues(const CategoricalAttribute att) const {
  if (att < source_->getNoCatAtts()) return source_->getNoValues(att);
  else return 2;
}

// return the name for a categorical Attribute
const char* InstanceStreamFeatureConstructor::FCMetaData::getCatAttName(const CategoricalAttribute att) const {
// string buffer used for returning names
static char* scratch = NULL;
static int scratchSize = 0;

  if (att < source_->getNoCatAtts()) return source_->getCatAttName(att);
  else {
    std::string n;
    name(newFeatures_[att-source_->getNoCatAtts()], n);
    if (scratch == NULL) {
      scratchSize = n.size();
      scratch = new char[scratchSize];
    }
    else if (scratchSize < n.size()) {
      delete [] scratch;
      scratchSize = n.size();
      scratch = new char[scratchSize];
    }
    strcpy(scratch, n.c_str());
    return scratch;
  }
}

// return the name for a categorical attribute value
const char* InstanceStreamFeatureConstructor::FCMetaData::getCatAttValName(const CategoricalAttribute att, const CatValue val) const {
  if (att < source_->getNoCatAtts()) return source_->getCatAttValName(att, val);
  else {
    if (val == 0) return "f";
    else return "t";
  }
}

// return the number of numeric attributes
unsigned int InstanceStreamFeatureConstructor::FCMetaData::getNoNumAtts() const {
  return source_->getNoNumAtts();
}

// return the name for a numeric attribute
const char* InstanceStreamFeatureConstructor::FCMetaData::getNumAttName(const NumericAttribute att) const {
  return source_->getNumAttName(att);
}

// return the precision to which values of a numeric attribute should be output
unsigned int InstanceStreamFeatureConstructor::FCMetaData::getPrecision(const NumericAttribute att) const {
  return source_->getPrecision(att);
}

// return a string that gives a meaningful name for the stream
const char* InstanceStreamFeatureConstructor::FCMetaData::getName() const {
  return "Feature Constructor";
}
