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
#ifdef _MSC_VER
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
//#ifndef DBG_NEW
//#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
//#define new DBG_NEW
//#endif
#include <stdlib.h>
#include <crtdbg.h>
#endif
#endif
#include <assert.h>
#include <float.h>
#include <stdlib.h>


#include "globals.h"
#include "utils.h"
#include "nb.h"
#include "learnerRegistry.h"
#include "correlationMeasures.h"


static LearnerRegistrar registrar("nb", constructor<nb>);

nb::nb(char*const*& argv, char*const* end):xyDist_(),xxyDist_()
{

    name_ = "Naive Bayes";

	rank_=RK_MI;   // this is the default of rank_
	loo_=false;
    weighting_=WT_UNIFORM;
	unbiased_=false;
	reliable_=false;


	// get arguments
	while (argv != end) {
		if (*argv[0] != '+') {
			 break;/**<  */
		} else if (streq(argv[0] + 1, "wgr")) {
			weighting_ = WT_GR;
		} else if (streq(argv[0] + 1, "wkl")) {
			weighting_ = WT_KL;
		} else if (streq(argv[0] + 1, "unbias")) {
			unbiased_=true;
		} else if (streq(argv[0] + 1, "reliable")) {
			reliable_=true;
		} else if (streq(argv[0] + 1, "wkl2d")) {
			weighting_ = WT_KL2D;
 	     } else if (streq(argv[0] + 1, "loo")) {
			loo_ = true;
         } else if (streq(argv[0] + 1, "mi")) {
			rank_=RK_MI;
         } else if (streq(argv[0] + 1, "mmcmi")) {
			rank_=RK_MAX_MIN_CMI;
         } else if (streq(argv[0] + 1, "raw")) {
			rank_=RK_RAW;
		  } else {
			error("NB does not support argument %s\n", argv[0]);
			break;
		}
		name_ += *argv;
		++argv;
		}
}

// copy constructor
nb::nb(const nb& l):xyDist_(),xxyDist_()
{
    name_ = l.name_;
    loo_=l.loo_;
	rank_=l.rank_;

    weighting_=l.weighting_;

}

// make a new copy
learner* nb::clone() const {
    return new nb(*this);
}

nb::~nb(void)
{
}

void  nb::getCapabilities(capabilities &c){
  c.setCatAtts(true);  // only categorical attributes are supported at the moment
}

void nb::reset(InstanceStream &is) {

	noCatAtts_ = is.getNoCatAtts();
	noClasses_ = is.getNoClasses();

	instanceStream_ = &is;
    xyDist_.reset(&is);
	xxyDist_.reset(is);

  	pass_ = 1;
  	squaredError_.assign(noCatAtts_,0.0);
    wgt_.assign(noCatAtts_,1.0);


    wgt2d_.resize(noCatAtts_);
    for (CategoricalAttribute x =0; x <noCatAtts_; x++)
        wgt2d_[x].resize(is.getNoValues(x),1.0);


}


void nb::train(const instance &inst) {

  	if (pass_ == 1){
        xyDist_.update(inst);
		if(rank_==RK_MAX_MIN_CMI)
			xxyDist_.update(inst);
  	}
  	else {
		assert(pass_ == 2);
		LOOCV(inst);
	}
}


void nb::initialisePass() {
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

void nb::finalisePass() {

	if(pass_==1){

        if(weighting_==WT_GR)
        {

            //H. Zhang and S. Sheng, ¡°Learning weighted naive bayes with accurate ranking,¡±
            //in Proceedings of the Fourth IEEE International Conference on Data Mining, ser.
            //ICDM ¡¯04.Washington, DC, USA: IEEE Computer Society, 2004, pp. 567¨C570.
            double sum_gainRatio=0;

            for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                wgt_[a]=getGainRatio(xyDist_,a);
                sum_gainRatio+=wgt_[a];
            }
            for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                wgt_[a]=noCatAtts_*wgt_[a]/sum_gainRatio;
                //try to use inverse gain ratio
                //wgt_[a]=sum_gainRatio/(noCatAtts_*wgt_[a]);
            }
            if (verbosity >= 3) {
                printf("The gain-ratio weights are:\n");
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    printf("%d:\t%f\n", a,wgt_[a]);
                }
            }
        }
        else if(weighting_==WT_KL)
        {
//                C. H. Lee, F. Gutierrez, and D. Dou, ¡°Calculating feature weights in naive bayes
//                with kullback-leibler measure,¡± in IEEE International Conference on Data Mining,
//                2012, pp. 1146¨C1151.

            //assert(kullback_leibler_==true);

            double sum_wgt=0;
            for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                double split_info=0;
                double kl_avg=0;
                double kl;
                for(CatValue value=0;value<instanceStream_->getNoValues(a);value++){
                    const double p_a_v=xyDist_.p(a,value);
                    split_info-=p_a_v*log2(p_a_v);
                    kl=0;
                    for (CatValue y = 0; y < noClasses_; y++) {
                        const double p_c_a=xyDist_.pYAtt(a,value,y);
                        kl+=p_c_a*log2(p_c_a/xyDist_.p(y));
                    }
                    kl_avg+= p_a_v*kl;
                }

                if(unbiased_==true)
                    wgt_[a]=kl_avg/split_info;
                else
                    wgt_[a]=kl_avg;

                sum_wgt+=wgt_[a];
            }
            for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                wgt_[a]=noCatAtts_*wgt_[a]/sum_wgt;
            }

            if (verbosity >= 3) {
                printf("The kullback-leibler weights are:\n");
                for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                    printf("%d:\t%f\n", a,wgt_[a]);
                }
            }
        }else if(weighting_ == WT_KL2D)
        {

            //[6]	C. H. Lee, “An information-theoretic filter approach for value weighted classification learning in naive bayes,”
            //Data and Knowledge Engineering, vol. 113, pp. 116 – 128, 2018.

            const double alpha=1;
            const double delta=0.05;
            double r_a_v=1.0;


            double kld=0;
            for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
                for(CatValue value=0; value < instanceStream_->getNoValues(a); value++)
                {
                    kld=0;
                    for (CatValue y = 0; y < noClasses_; y++) {
                        const double p_c_a=xyDist_.pYAtt(a,value,y);
                        kld+=p_c_a*log2(p_c_a/xyDist_.p(y));
                    }
                    if(reliable_==true)
                    {
                        InstanceCount m=xyDist_.getCount(a,value);

                        r_a_v=exp(log2(delta/2)/(2*alpha*m));
                    }
                    wgt2d_[a][value]=r_a_v*kld;
                    if(verbosity>=2)
                    {
                        printf("a=%d,v=%d,kld=%f,rij=%f\n",a,value,kld,r_a_v);

                    }
                }

                if(verbosity>=3)
                {
                    if(instanceStream_->hasCatMissing(a))
                        printf("att %d has missing value.\n",a);
                    else
                        printf("att %d has no missing value.\n",a);
                }

            }

        }



		if (loo_==true) {

			if(rank_==RK_MI){
				orderedAtts.clear();
				for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
					orderedAtts.push_back(a);
				}
				//get the mutual information to rank the attrbiutes
				std::vector<float> measure;
				getMutualInformation(xyDist_, measure);

				// sort the attributes on mutual information with the class

				if (!orderedAtts.empty()) {
					valCmpClass cmp(&measure);
					std::sort(orderedAtts.begin(), orderedAtts.end(), cmp);

					if (verbosity >= 3) {
						printf("The order of attributes ordered by the measure:\n");
						for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
							printf("%d:\t%f\t%u\n",  orderedAtts[a],measure[orderedAtts[a]],instanceStream_->getNoValues(orderedAtts[a]));
						}
					}
				}

			}
			else if(rank_==RK_MAX_MIN_CMI){

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

				orderedAtts.clear();

				orderedAtts.push_back(first);
				seletedAttributes[first]=true;

				while(orderedAtts.size()<noCatAtts_)
				{
					maxFirst=true;
					for(CategoricalAttribute a = 0; a < noCatAtts_; a++) {

						if(seletedAttributes[a]==false)
						{
							minimalCMI[a]=cmiac[a][orderedAtts[0]];
							for (CategoricalAttribute i = 1; i < orderedAtts.size(); i++) {
								if( cmiac[a][orderedAtts[i]]< minimalCMI[a] )
								{
									minimalCMI[a] =cmiac[a][orderedAtts[i]];
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
					orderedAtts.push_back(maxIndex);
					seletedAttributes[maxIndex]=true;
				}

				if (verbosity >= 2) {
					const char * sep = "";
					printf("The order of attributes:\n");
					for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
						printf("%s%d",sep, orderedAtts[a] );
						sep = ", ";
					}
					printf("\n");
				}
			}
            else
			{
				assert(rank_==RK_RAW);
				orderedAtts.clear();
				for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
					orderedAtts.push_back(a);
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

void nb::LOOCV(const instance &inst)
{
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


        // scale up by maximum possible factor to reduce risk of numeric underflow

    for (CategoricalAttribute aIndex = 0; aIndex < noCatAtts_; aIndex++) {
        const CategoricalAttribute att=orderedAtts[aIndex];
        model[aIndex].resize(noClasses_);
        for (CatValue y = 0; y < noClasses_; y++) {

            InstanceCount xyCount;
            xyCount=xyDist_.getCount(att,inst.getCatVal(att), y);
            if(y==trueClass)
            {
                xyCount--;
            }

            classDist[y]*= mEstimate(xyCount,classCount[y],instanceStream_->getNoValues(att));
            model[aIndex][y]= classDist[y];
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

bool nb::trainingIsFinished() {
  	if (loo_==true)
		return pass_ > 2;
	else
		return pass_ > 1;
}



void nb::classify(const instance &inst, std::vector<double> &classDist) {


    if(loo_==true){
/*
        double scaleFactor = std::numeric_limits<double>::max() / noCatAtts_;
        for (CatValue y = 0; y < noClasses_; y++) {
            classDist[y]= xyDist_.p(y) * scaleFactor;
        }
            // scale up by maximum possible factor to reduce risk of numeric underflow
        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
               for (CatValue y = 0; y < noClasses_; y++) {
                    classDist[y]*= xyDist_.p(a, inst.getCatVal(a), y);
                }
         }
        normalise(classDist);
*/

      for (CatValue y = 0; y < noClasses_; y++) {
        double p = xyDist_.p(y) * (std::numeric_limits<double>::max() / 2.0);
        // scale up by maximum possible factor to reduce risk of numeric underflow

        for (CategoricalAttribute xIndex = 0; xIndex < optAttIndex_; xIndex++) {
            const CategoricalAttribute att=orderedAtts[xIndex];
            p *= xyDist_.p(att, inst.getCatVal(att), y);
        }

        assert(p >= 0.0);
        classDist[y] = p;
      }

      normalise(classDist);

    }
    else{

      for (CatValue y = 0; y < noClasses_; y++) {
        double p = xyDist_.p(y) * (std::numeric_limits<double>::max() / 2.0);
        // scale up by maximum possible factor to reduce risk of numeric underflow

        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
            if(weighting_ == WT_KL2D)
                p *= pow(xyDist_.p(a, inst.getCatVal(a), y),wgt2d_[a][inst.getCatVal(a)]);
            else
                p *= pow(xyDist_.p(a, inst.getCatVal(a), y),wgt_[a]);

        }

        assert(p >= 0.0);
        classDist[y] = p;
      }


      normalise(classDist);

    }

}



