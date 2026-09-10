/* Open source system for classification learning from very large data
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

#include "incrementalLearner.h"
#include "xxyDist.h"
#include <limits>

/**
* A type for indicating how to rank the attributes in CV risk minimization
* RK_RAW indicates to use the original (raw) sequence
* RK_MI indicates mutual information ranking
* RK_MMCMI indicates maximin conditional mutual information ranking
* RK_CHISQ indicates chisq ranking
* RK_SU  indicates symmetrical uncertainty ranking
* RK_JMI indicates joint mutual information
* RK_MIFS indicates mutual information feature selection
*/

enum RankType { RK_RAW,RK_MI,RK_MMCMI,RK_CHISQ,RK_SU,RK_JMI,RK_MIFS,RK_MRMR};



class TAN: public IncrementalLearner {
public:
  TAN(char* const *& argv, char* const * end);
  TAN(const TAN& l);      ///< copy constructor
  learner* clone() const;           ///< create a copy of the learner
  ~TAN(void);

  void reset(InstanceStream &is);   ///< reset the learner prior to training
  void initialisePass(); ///< must be called to initialise a pass through an instance stream before calling train(const instance). should not be used with train(InstanceStream)
  void train(const instance &inst); ///< primary training method. train from a single instance. used in conjunction with initialisePass and finalisePass
  void finalisePass(); ///< must be called to finalise a pass through an instance stream using train(const instance). should not be used with train(InstanceStream)
  bool trainingIsFinished(); ///< true iff no more passes are required. updated by finalisePass()
  void getCapabilities(capabilities &c);
  virtual void classify(const instance &inst, std::vector<double> &classDist);

private:

	std::vector<bool> active_; ///< true for active[att] if att is selected -- flags: chisq, selective, selectiveTest
	unsigned int inactiveCnt_; ///< number of attributes not selected -- flags: chisq, selective, selectiveTest

    unsigned int noCatAtts_;          ///< the number of categorical attributes.
    unsigned int noClasses_;                          ///< the number of classes

	unsigned int pass_;     ///< the number of passes for the learner
    bool loo_;				///< true if performing the second pass to do leave-one-out cross validation


    RankType rank_; /// modes of ranking the attributes
    bool avgRel_; /// true iff using averaged redundancy in MIFS
    bool diff_; ///true iff using difference between relevance and redundancy in MRMR
  //  bool raw_;
  //  bool directRank_;       ///<true iff directly rank the attributes
  //  bool mi_;
	bool chisq_; /// true iff selecting the attributes by chi squared test.
	unsigned int optChildIndex_; ///< indicate the how many attributes have been selected as children
	void LOOCV(const instance &inst);


    InstanceStream* instanceStream_;
    std::vector<CategoricalAttribute> parents_;
    xxyDist xxyDist_;

    //this is not used because pass_ can work
    //bool trainingIsFinished_; ///< true iff the learner is trained

    const static CategoricalAttribute NOPARENT = 0xFFFFFFFFUL; // cannot use std::numeric_limits<categoricalAttribute>::max() because some compilers will not allow it here

    std::vector<CategoricalAttribute> orderedAtts_;   ///<the rank of the attributes for loocv selection
	std::vector<double>  squaredError_;
	CategoricalAttribute optAttIndex_; ///< indicate the how many attributes have been selected
};
