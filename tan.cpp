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
#include "tan.h"
#include "utils.h"
#include "correlationMeasures.h"
#include "globals.h"
#include <assert.h>
#include <math.h>
#include <set>
#include <algorithm>

#include <stdlib.h>
#include "learnerRegistry.h"

static LearnerRegistrar registrar("tan", constructor<TAN>);

TAN::TAN(char* const *& argv, char* const * end) :
		xxyDist_() {
	name_ = "TAN";

	//raw_=false;
	//directRank_=false;
	//mi_ = false;

	loo_=false;
	chisq_ = false;

	avgRel_=false;
    diff_=false;

	rank_=RK_MI;

	// get arguments
	while (argv != end) {
		if (*argv[0] != '+') {
			break;
 		} else if (streq(argv[0] + 1, "loo")) {
			loo_ = true;
		} else if (streq(argv[0] + 1, "raw")) {
		    rank_=RK_RAW;
		} else if (streq(argv[0] + 1, "mmcmi")) {
			rank_=RK_MMCMI;
		} else if (streq(argv[0] + 1, "su")) {
			rank_=RK_SU;
		} else if (streq(argv[0] + 1, "mifs")) {
			rank_=RK_MIFS;
		} else if (streq(argv[0] + 1, "avgrel")) {
			avgRel_=true;
		} else if (streq(argv[0] + 1, "mrmr")) {
			rank_=RK_MRMR;
        } else if (streq(argv[0] + 1, "diff")) {
			diff_=true;
		} else if (streq(argv[0] + 1, "jmi")) {
			rank_=RK_JMI;
		} else if (streq(argv[0] + 1, "mi")) {
			rank_=RK_MI;
		} else if (streq(argv[0] + 1, "rankcq")) {
            rank_=RK_CHISQ;
		} else if (streq(argv[0] + 1, "chisq")) {

			chisq_=true;

		} else {
			error("TAN does not support argument %s\n", argv[0]);
			break;
		}
		name_ += *argv;

		++argv;
	}



}

// copy constructor
TAN::TAN(const TAN& l)
 : xxyDist_()
{
  name_ = l.name_;
  loo_=l.loo_;
  rank_=l.rank_;
  chisq_=l.chisq_;
   pass_=l.pass_;
   avgRel_=l.avgRel_;
   diff_=l.diff_;

   /*
  directRank_=l.directRank_;
  mi_=l.mi_;

  raw_=l.raw_;
  */

}

// make a new copy
learner* TAN::clone() const {
  return new TAN(*this);
}

TAN::~TAN(void) {}

void TAN::reset(InstanceStream &is) {
	instanceStream_ = &is;
	noCatAtts_ = is.getNoCatAtts();
	noClasses_ = is.getNoClasses();
	active_.assign(noCatAtts_, true);
	pass_ = 1;
  	squaredError_.assign(noCatAtts_,0.0);
		inactiveCnt_ = 0;

	//safeAlloc(parents, noCatAtts_);
    parents_.resize(noCatAtts_);
	for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
		parents_[a] = NOPARENT;
	}

	xxyDist_.reset(is);
}

void TAN::getCapabilities(capabilities &c) {
	c.setCatAtts(true); // only categorical attributes are supported at the moment
}

void TAN::initialisePass() {


}



void TAN::train(const instance &inst) {
	if(pass_==1)
        xxyDist_.update(inst);
    else
    {
        assert(pass_ == 2);
        LOOCV(inst);
    }
}


void TAN::LOOCV(const instance &inst)
{
    xyDist &xyDist_=xxyDist_.xyCounts;
    const InstanceCount totalCount = xyDist_.count-1;
    std::vector<double> classDist;
	std::vector<InstanceCount> classCount;


    const CatValue trueClass = inst.getClass();

    // scale up by maximum possible factor to reduce risk of numeric underflow
    double scaleFactor = std::numeric_limits<double>::max() / noCatAtts_;

	std::vector<std::vector<double> >  model;

	model.resize(noCatAtts_);
    classDist.resize(noClasses_);
    classCount.resize(noClasses_);

    for (CatValue y = 0; y < noClasses_; y++) {
        classCount[y]=xyDist_.getClassCount(y);

        if(y==trueClass)
        {
            classCount[y]--;
        }

        classDist[y]= mEstimate(classCount[y],totalCount,noClasses_)* scaleFactor;
    }


    for (CategoricalAttribute xIndex = 0; xIndex < noCatAtts_; xIndex++) {

        const CategoricalAttribute child=orderedAtts_[xIndex];
        const CatValue childVal= inst.getCatVal(child);

        const CategoricalAttribute parent = parents_[child];
        model[xIndex].resize(noClasses_);


        if (parent == NOPARENT) {
             for (CatValue y = 0; y < noClasses_; y++) {

                InstanceCount xyCount;
                xyCount=xyDist_.getCount(child,childVal, y);
                if (y == trueClass) {
                    xyCount--;
                }
                classDist[y] *= mEstimate(xyCount,classCount[y],instanceStream_->getNoValues(child));
                model[xIndex][y]= classDist[y];
             }

        } else {
             for (CatValue y = 0; y < noClasses_; y++) {

                InstanceCount parentyCount;
                InstanceCount x1x2yCount;

                const CatValue parentVal = inst.getCatVal(parent);



                parentyCount=xyDist_.getCount(parent,parentVal,y);
                x1x2yCount = xxyDist_.getCount(parent,
                            parentVal, child, childVal, y);


                if (y == trueClass) {
                    x1x2yCount --;
                    parentyCount--;
                }
                classDist[y] *= mEstimate(
                            x1x2yCount, parentyCount,
                            instanceStream_->getNoValues(child));
                model[xIndex][y]= classDist[y];
             }
        }
    }


     for (CategoricalAttribute aIndex = 0; aIndex< noCatAtts_; aIndex++) {

        if(sum(model[aIndex])!=0)
        {
            normalise(model[aIndex]);
            const double error = 1.0 - model[aIndex][trueClass];
            squaredError_[aIndex] += error * error;
        }
     }
}

void TAN::classify(const instance &inst, std::vector<double> &classDist) {

	for (CatValue y = 0; y < noClasses_; y++) {
		classDist[y] = xxyDist_.xyCounts.p(y)* (std::numeric_limits<double>::max() / 2.0);
	}

	if(loo_==true)
    {
        for (CategoricalAttribute xIndex = 0; xIndex < optAttIndex_; xIndex++) {

            const CategoricalAttribute x1=orderedAtts_[xIndex];
            const CategoricalAttribute parent = parents_[x1];

            if (parent == NOPARENT) {
                for (CatValue y = 0; y < noClasses_; y++) {
                    classDist[y] *= xxyDist_.xyCounts.p(x1, inst.getCatVal(x1), y);
                }
            } else {
                for (CatValue y = 0; y < noClasses_; y++) {
                    classDist[y] *= xxyDist_.p(x1, inst.getCatVal(x1), parent,
                            inst.getCatVal(parent), y);
                }
            }
        }

    }
    else
    {
        for (unsigned int x1 = 0; x1 < noCatAtts_; x1++) {
            const CategoricalAttribute parent = parents_[x1];

			if(active_[x1]==false)
				continue;

            if (parent == NOPARENT) {
                for (CatValue y = 0; y < noClasses_; y++) {
                    classDist[y] *= xxyDist_.xyCounts.p(x1, inst.getCatVal(x1), y);
                }
            } else {
                for (CatValue y = 0; y < noClasses_; y++) {
                    classDist[y] *= xxyDist_.p(x1, inst.getCatVal(x1), parent,
                            inst.getCatVal(parent), y);
                }
            }
        }
    }


	normalise(classDist);
}

// creates a comparator for two attributes based on their
//relative value with the class,such as mutual information, symmetrical uncertainty

class valCmpClass {
public:
	valCmpClass(std::vector<float> *s) {
		val = s;
	}

	bool operator()(CategoricalAttribute a, CategoricalAttribute b) {
		return (*val)[a] > (*val)[b];
	}

private:
	std::vector<float> *val;
};
// creates an ascending comparator for two attributes based on their
//relative value with the class,such as mutual information, symmetrical uncertainty

class valAscCmpClass {
public:
	valAscCmpClass(std::vector<float> *s) {
		val = s;
	}

	bool operator()(CategoricalAttribute a, CategoricalAttribute b) {
		return (*val)[a]< (*val)[b];
	}

private:
	std::vector<float> *val;
};



void TAN::finalisePass() {


	if(pass_==1)
	{
        crosstab<float> cmi = crosstab<float>(noCatAtts_);
        getCondMutualInf(xxyDist_, cmi);

        // find the maximum spanning tree

        CategoricalAttribute firstAtt = 0;

        parents_[firstAtt] = NOPARENT;

        float *maxWeight;
        CategoricalAttribute *bestSoFar;
        CategoricalAttribute topCandidate = firstAtt;
        std::set<CategoricalAttribute> available;

        safeAlloc(maxWeight, noCatAtts_);
        safeAlloc(bestSoFar, noCatAtts_);

        maxWeight[firstAtt] = -std::numeric_limits<float>::max();

        for (CategoricalAttribute a = firstAtt + 1; a < noCatAtts_; a++) {
            maxWeight[a] = cmi[firstAtt][a];
            if (cmi[firstAtt][a] > maxWeight[topCandidate])
                topCandidate = a;
            bestSoFar[a] = firstAtt;
            available.insert(a);
        }

        while (!available.empty()) {
            const CategoricalAttribute current = topCandidate;
            parents_[current] = bestSoFar[current];
            available.erase(current);

            if (!available.empty()) {
                topCandidate = *available.begin();
                for (std::set<CategoricalAttribute>::const_iterator it =
                        available.begin(); it != available.end(); it++) {
                    if (maxWeight[*it] < cmi[current][*it]) {
                        maxWeight[*it] = cmi[current][*it];
                        bestSoFar[*it] = current;
                    }

                    if (maxWeight[*it] > maxWeight[topCandidate])
                        topCandidate = *it;
                }
            }
        }

        //for (attribute a = 0; a < meta->noAttributes; a++) {
        //  delete []mi[a];
        //}
        //delete []mi;
        delete[] bestSoFar;
        delete[] maxWeight;

		//chisq test to select dependent attributes
		if (chisq_ == true) {

			bool flag = true;
			double lowest;
			CategoricalAttribute attLowest;

			std::vector<CategoricalAttribute> order;

			for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
				order.push_back(a);
			}
			for (std::vector<CategoricalAttribute>::const_iterator it =
					order.begin(); it != order.end(); it++) {

				CategoricalAttribute a = *it;
				const unsigned int rows = instanceStream_->getNoValues(a);

				if (rows < 2) {
					active_[a] = false;
					inactiveCnt_++;
				} else {
					const unsigned int cols = noClasses_;
					InstanceCount *tab;
					allocAndClear(tab, rows * cols);

					for (CatValue r = 0; r < rows; r++) {

						for (CatValue c = 0; c < cols; c++) {
							tab[r * cols + c] += xxyDist_.xyCounts.getCount(a,
									r, c);

							if (verbosity >= 2) {

								if (a == 2) {
									printf("%d ", tab[r * cols + c]);
								}

							}

						}
						if (verbosity >= 2) {
							if (a == 2) {
								printf("\n");
							}
						}
					}

					//double critVal = 0.05 / noCatAtts_;
					//double critVal = 0.0000000005 / noCatAtts_;

					double critVal =0.05 / noCatAtts_;
					double chisqVal = chiSquare(tab, rows, cols);

					if (verbosity >= 2){

						printf("the chi-square value of attribute %s: %40.40f\n",instanceStream_->getCatAttName(a), chisqVal);

					}

					//select the attribute with lowest chisq value as parent if there is attribute satisfying the
					//significance level of 5%
					if (flag == true) {
						lowest = chisqVal;
						attLowest = a;

						flag = false;
					} else {
						if (lowest > chisqVal) {
							lowest = chisqVal;
							attLowest = a;
						}
					}

					if (chisqVal > critVal) {

						if (verbosity >= 2)
							printf(
									"%s suppressed by chisq test against class\n",
									instanceStream_->getCatAttName(a));
						active_[a] = false;
						inactiveCnt_++;
					}
					delete[] tab;
				}
			}
			if (inactiveCnt_ == noCatAtts_) {
				active_[attLowest] = true;
				if (verbosity >= 2)
					printf("Only the attribute %u is active.\n", attLowest);

			}
			if (verbosity >= 2)
				printf(
						"The number of active parent and total attributes are: %u,%u\n",
						noCatAtts_ - inactiveCnt_, noCatAtts_);
		}




        //the following code is to rank the attributes
        if (loo_==true) {

            if(rank_==RK_MI)
            {
                orderedAtts_.clear();
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    orderedAtts_.push_back(a);
                }
                //get the mutual information to rank the attrbiutes
                std::vector<float> measure;
                getMutualInformation(xxyDist_.xyCounts, measure);

                // sort the attributes on mutual information with the class

                if (!orderedAtts_.empty()) {
                    valCmpClass cmp(&measure);
                    std::sort(orderedAtts_.begin(), orderedAtts_.end(), cmp);

                    if (verbosity >= 3) {
                        printf("The order of attributes ordered by mutual information:\n");
                        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                            printf("%d:\t%f\t%u\n",  orderedAtts_[a],measure[orderedAtts_[a]],instanceStream_->getNoValues(orderedAtts_[a]));
                        }
                    }
                }
            }
            else if(rank_==RK_SU)
            {
                orderedAtts_.clear();
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    orderedAtts_.push_back(a);
                }

				std::vector<float> measure;

                getSymmetricalUncert(xxyDist_.xyCounts, measure);

				// sort the attributes on symmetrical uncertainty with the class

                if (!orderedAtts_.empty()) {
                    valCmpClass cmp(&measure);
                    std::sort(orderedAtts_.begin(), orderedAtts_.end(), cmp);

                    if (verbosity >= 3) {
                        printf("The order of attributes ordered by symmetrical uncertainty:\n");
                        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                            printf("%d:\t%f\t%u\n",  orderedAtts_[a],measure[orderedAtts_[a]],instanceStream_->getNoValues(orderedAtts_[a]));
                        }
                    }
                }

            }
            else if(rank_==RK_MRMR){
				//Peng's mRMR to select the attribute set by maximal relivance and minimal redundency
				std::vector<float> MI;
				crosstab<float> PMI(noCatAtts_);
				std::vector<float> maxRMR;
				std::vector<bool> leftAtts;
				float sumSelectedAtts;

				int maxIndex=0,maxAtts=1;

				leftAtts.assign(noCatAtts_, true);

				getMutualInformation(xxyDist_.xyCounts, MI);

				getAttMutualInf(xxyDist_, PMI);


				orderedAtts_.clear();
				//find maximal mutual information
				for(int i=1;i<noCatAtts_;i++)
					if(MI[i]>MI[maxIndex])
						maxIndex=i;

				if (verbosity >= 2) {

						printf("maximal mutual information of atts:%d\n",maxIndex);
				}

				leftAtts[maxIndex]=false;
				orderedAtts_.push_back(maxIndex);
				maxRMR.push_back(MI[maxIndex]);

				while(orderedAtts_.size()!=noCatAtts_)
				{

					bool first=true;
					float currentValue,maxValue;

					for(int i=0;i<noCatAtts_;i++)
					{
						if(leftAtts[i]==false)
							continue;

						sumSelectedAtts=0;
						for(int j=0;j<orderedAtts_.size();j++)
								sumSelectedAtts+=PMI[orderedAtts_[j]][i];

                        if(diff_==true)
                           currentValue=MI[i]-(sumSelectedAtts/orderedAtts_.size());
                         else
                            currentValue=MI[i]/(sumSelectedAtts/orderedAtts_.size()+0.01);


						if(first==true)
						{
							maxValue=currentValue;
							maxIndex=i;
							first=false;
						}
						else
						{
								if(currentValue>maxValue)
								{
									maxValue=currentValue;
									maxIndex=i;
								}
						}
					}

					leftAtts[maxIndex]=false;
					orderedAtts_.push_back(maxIndex);
					maxRMR.push_back(maxValue);
				}

			}
            else if(rank_==RK_MIFS)
            {
                // this is the same MRMR by PENG, except the way to average the sum of relevance
                orderedAtts_.clear();
				std::vector<float> measure;
                getMutualInformation(xxyDist_.xyCounts, measure);

                crosstab<float> PMI(noCatAtts_);
                getAttMutualInf(xxyDist_, PMI);

                int max_ind=indexOfMaxVal(measure);
                orderedAtts_.push_back(max_ind);
                if(verbosity>=2)
                {
                    printf("%f,%d\n",measure[max_ind],max_ind);
                }

                float J,Jmax;
                bool first;


                while(orderedAtts_.size()<noCatAtts_)
                {

                    first=true;

                    for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {

                        unsigned k;
                        //search if a has been added in orderedAtts_
                        for( k=0;k<orderedAtts_.size();k++)
                        {
                            if(a==orderedAtts_[k])
                                break;
                        }
                        //a has already been added, skip
                        if(k<orderedAtts_.size())
                            continue;

                        J=0;
                        for(CategoricalAttribute s=0;s<orderedAtts_.size();s++)
                        {
                            J+=PMI[orderedAtts_[s]][a];
                        }

                        if(avgRel_==true)
                                J=measure[a] - J/orderedAtts_.size();

                        else
                                J=measure[a] - J;

                        if(first==true)
                        {
                            first=false;
                            Jmax=J;
                            max_ind=a;
                            if(verbosity>=3)
                            {
                                printf("==%f,%d\n",Jmax,max_ind);
                            }

                        }
                        else{

                            if(J>Jmax){
                                Jmax=J;
                                max_ind=a;
                            }
                            if(verbosity>=3)
                            {
                                printf("==%f,%d\n",Jmax,max_ind);
                            }
                        }
                    }
                    if(verbosity>=2)
                    {
                        printf("%f,%d\n",Jmax,max_ind);
                    }
                    orderedAtts_.push_back(max_ind);
                }
            }
            else if(rank_==RK_JMI)
            {
                //Joint mutual information  by Meyer(2008) and Gavin Brown (2012)
                orderedAtts_.clear();
				std::vector<float> measure;
                getMutualInformation(xxyDist_.xyCounts, measure);

                crosstab<float> PMI(noCatAtts_);
                getPairMutualInf(xxyDist_, PMI);

                int max_ind=indexOfMaxVal(measure);
                orderedAtts_.push_back(max_ind);
                if(verbosity>=2)
                {
                    printf("%f,%d\n",measure[max_ind],max_ind);
                }

                float J,Jmax;
                bool first;


                while(orderedAtts_.size()<noCatAtts_)
                {
                    first=true;
                    unsigned k;
                    for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {

                        //search if a has been added in orderedAtts_
                        for( k=0;k<orderedAtts_.size();k++)
                        {
                            if(a==orderedAtts_[k])
                                break;
                        }
                        //a has already been added, skip
                        if(k<orderedAtts_.size())
                            continue;

                        J=0;
                        for(CategoricalAttribute s=0;s<orderedAtts_.size();s++)
                        {
                            J+=PMI[orderedAtts_[s]][a];
                        }

                        if(first==true)
                        {
                            first=false;
                            Jmax=J;
                            max_ind=a;
                            if(verbosity>=3)
                            {
                                printf("==%f,%d\n",Jmax,max_ind);
                            }

                        }
                        else{

                            if(J>Jmax){
                                Jmax=J;
                                max_ind=a;
                            }
                            if(verbosity>=3)
                            {
                                printf("==%f,%d\n",Jmax,max_ind);
                            }
                        }
                    }
                    if(verbosity>=2)
                    {
                        printf("%f,%d\n",Jmax,max_ind);
                    }
                    orderedAtts_.push_back(max_ind);
                }
            }
            else if(rank_==RK_MMCMI){

                //calculate the symmetrical uncertainty between each attribute and class
                std::vector<float> mi;
                crosstab<float> cmiac(noCatAtts_);
                getAttClassCondMutualInf(xxyDist_, cmiac);
                getMutualInformation(xxyDist_.xyCounts, mi);

                if(verbosity>=3)
                {
                    printf("The mutual information:\n");
                    print(mi);
                    printf("\n");
                }

                if(verbosity>=3)
                {
                    printf("Conditional mutual information:\n");
                    for(unsigned int i=0;i<noCatAtts_;i++)
                    {
                        print(cmiac[i]);
                        printf("\n");
                    }
                }
                std::vector<bool> seletedAttributes(noCatAtts_,false);
                std::vector<float> minimalCMI(noCatAtts_,0);
                float maxCMI;
                unsigned int maxIndex;
                bool maxFirst=true;

                unsigned int first=indexOfMaxVal(mi);

				//add this to fix the bug
				orderedAtts_.clear();


                orderedAtts_.push_back(first);
                seletedAttributes[first]=true;

                while(orderedAtts_.size()<noCatAtts_)
                {
                    maxFirst=true;
                    for(CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                        if(seletedAttributes[a]==false)
                        {
                            minimalCMI[a]=cmiac[a][orderedAtts_[0]];
                            for (CategoricalAttribute i = 1; i < orderedAtts_.size(); i++) {
                                if( cmiac[a][orderedAtts_[i]]< minimalCMI[a] )
                                {
                                    minimalCMI[a] =cmiac[a][orderedAtts_[i]];
                                }
                            }
                            if(maxFirst==true)
                            {
                                maxFirst=false;
                                maxCMI=minimalCMI[a];
                                maxIndex=a;
                            }else if(minimalCMI[a]>maxCMI)
                            {
                                maxCMI=minimalCMI[a];
                                maxIndex=a;
                            }

                        }
                    }
                    orderedAtts_.push_back(maxIndex);
                    seletedAttributes[maxIndex]=true;
                }

                if (verbosity >= 2) {
                    const char * sep = "";
                    printf("The order of attributes by direct rank:\n");
                    for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                        printf("%s%d",sep, orderedAtts_[a] );
                        sep = ", ";
                    }
                    printf("\n");
                }
            }else if (rank_==RK_CHISQ) {
				//chisq test to rank the attributes in loocv

				orderedAtts_.clear();
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    orderedAtts_.push_back(a);
                }
                //measure to rank the attrbiutes
                std::vector<float> measure;

                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {

					const unsigned int rows = instanceStream_->getNoValues(a);


						const unsigned int cols = noClasses_;
						InstanceCount *tab;
						allocAndClear(tab, rows * cols);

						for (CatValue r = 0; r < rows; r++) {

							for (CatValue c = 0; c < cols; c++) {
								tab[r * cols + c] += xxyDist_.xyCounts.getCount(a,
										r, c);
							}
						}

						double critVal = 0.05 / noCatAtts_;
						//double critVal = 0.0000000005 / noCatAtts_;
						double chisqVal = chiSquare(tab, rows, cols);

						if (verbosity >=4){
							printf("the chi-square value of attribute %s: %40.40f\n",instanceStream_->getCatAttName(a), chisqVal);
						}

						measure.push_back(chisqVal);



						delete[] tab;
					}
				// sort the attributes on chisq test

                if (!orderedAtts_.empty()) {
                    valAscCmpClass cmp(&measure);
                    std::sort(orderedAtts_.begin(), orderedAtts_.end(), cmp);

                    if (verbosity >=3) {
                        printf("The order of attributes ordered by chisq test:\n");
                        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                            printf("%d:\t%f\t%u\n",  orderedAtts_[a],measure[orderedAtts_[a]],instanceStream_->getNoValues(orderedAtts_[a]));
                        }
                    }
                }

			}
			else
            {
                assert(rank_==RK_RAW);
                orderedAtts_.clear();
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    orderedAtts_.push_back(a);
                }
                if (verbosity >= 3) {
                    printf("The original order of attributes:\n");
                    for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                        printf("%d:\t%d\t%u\n",  a,orderedAtts_[a],instanceStream_->getNoValues(orderedAtts_[a]));
                    }
                }
            }
        }

	}
	else if(pass_==2)
	{
        if(loo_==true)
        {
            optAttIndex_=indexOfMinVal(squaredError_);
            optAttIndex_++;
            printf("The best model is: %u in %u.\n",optAttIndex_,noCatAtts_);
        }
    }

	++pass_;
}

/// true iff no more passes are required. updated by finalisePass()
bool TAN::trainingIsFinished() {
	if (loo_==true)
		return pass_ > 2;
	else
		return pass_ > 1;
}
