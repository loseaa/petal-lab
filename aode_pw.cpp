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
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program. If not, see <http://www.gnu.org/licenses/>.
 **
 ** Please report any bugs to Shenglei Chen <tristan_chen@126.com>
 */

#include "aode_pw.h"
#include <assert.h>
#include "utils.h"
#include <algorithm>
#include "correlationMeasures.h"
#include "globals.h"
#include "utils.h"
#include "crosstab.h"
#include <math.h>
#include <iostream>
#include "learnerRegistry.h"

/*float sigmoid(float x) {
    return (1 / (1 + exp(-x)));
}*/

static LearnerRegistrar registrar("aode_pw", constructor<aode_pw>);


aode_pw::aode_pw(char* const *& argv, char* const * end) {
    name_ = "aode_pw";
    UsedAttrRatio = 0;
    weighted = false;
    minCount = 100;
    subsumptionResolution = false;
    selected = false;
    su_ = false;
    mi_ = false;
    ig_ = false;
    acmi_ = false;
    chisq_ = false;
    empiricalMEst_ = false;
    empiricalMEst2_ = false;

    correlationFilter_ = false;
    useThreshold_ = false;
    threshold_ = 0;
    factor_ = 1.0;

    useAttribSelec_ = false;

    chilect_ = false;

    attribSelected_ = 0;

    modelNum = std::numeric_limits<int>::max();

    for (int i = 0; i < 100; i++) {
        fathercount[i] = 0;
    }
    while (argv != end) {
        if (*argv[0] != '-') {
            break;
        } else if (streq(argv[0] + 1, "modelnum")) {
            ++argv;
            modelNum = atoi(argv[0] + 1);
        } else if (streq(argv[0] + 1, "empirical")) {
            empiricalMEst_ = true;
        } else if (streq(argv[0] + 1, "empirical2")) {
            empiricalMEst2_ = true;
        } else if (streq(argv[0] + 1, "sub")) {
            subsumptionResolution = true;
        } else if (argv[0][1] == 'n') { //-n200 argv[0]相当于数组名
            getUIntFromStr(argv[0] + 2, minCount, "n");
        } else if (streq(argv[0] + 1, "w")) {
            weighted = true;
        } else if (streq(argv[0] + 1, "chilect")) {
            chilect_ = true;
        } else if (streq(argv[0] + 1, "selective")) {
            selected = true;
        } else if (streq(argv[0] + 1, "ig")) {
            selected = true;
            ig_ = true;
        } else if (streq(argv[0] + 1, "acmi")) {
            selected = true;
            acmi_ = true;
        } else if (streq(argv[0] + 1, "mi")) {
            selected = true;
            mi_ = true;
        } else if (argv[0][1] == 'a') {
            getUIntFromStr(argv[0] + 2, attribSelected_, "a");
            useAttribSelec_ = true;
        } else if (argv[0][1] == 'f') {
            unsigned int factor;
            getUIntFromStr(argv[0] + 2, factor, "f");
            factor_ = factor / 10.0;
            while (factor_ >= 1)
                factor_ /= 10;
        } else if (streq(argv[0] + 1, "su")) {
            selected = true;
            su_ = true;
        } else if (streq(argv[0] + 1, "cf")) {
            correlationFilter_ = true;
        } else if (argv[0][1] == 't') {
            unsigned int thres;
            getUIntFromStr(argv[0] + 2, thres, "threshold");
            threshold_ = thres / 10.0;
            while (threshold_ >= 1)
                threshold_ /= 10;
            useThreshold_ = true;
        } else if (streq(argv[0] + 1, "chisq")) {
            selected = true;
            chisq_ = true;
        } else {
            error("aode_pw does not support argument %s\n", argv[0]);
            break;
        }

        name_ += *argv;

        ++argv;
    }
    if (selected == true) {
        if (mi_ == false && su_ == false && chisq_ == false)
            chisq_ = true;
    }


    trainingIsFinished_ = false;
}

aode_pw::~aode_pw(void) {
}

learner* aode_pw::clone() const {
  return new aode_pw(*this);
}

void aode_pw::getCapabilities(capabilities &c) {
    c.setCatAtts(true); // only categorical attributes are supported at the moment
}

void aode_pw::reset(InstanceStream &is) {
    xxyDist_.reset(is);
    trainingIsFinished_ = false;

    inactiveCnt_ = 0;

    noCatAtts_ = is.getNoCatAtts();
    noClasses_ = is.getNoClasses();

    instanceStream_ = &is;

    active_.assign(noCatAtts_, true);
    chiactive_.assign(noCatAtts_, true);
    infoWeightGeneral.assign(noCatAtts_,0);
}

void aode_pw::initialisePass() {

}

void aode_pw::train(const instance &inst) {
    xxyDist_.update(inst);
}

/// true iff no more passes are required. updated by finalisePass()

bool aode_pw::trainingIsFinished() {
    return trainingIsFinished_;
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


void aode_pw::getWeightVector(std::vector<float> &weightVector,std::vector<bool> &active) {
    std::vector<float> entropyOfXi(noCatAtts_,0);
    for(CategoricalAttribute Xi=0; Xi<noCatAtts_; Xi++) {
        if(active[Xi]) {
            for(CatValue value = 0; value<xxyDist_.getNoValues(Xi); value++) {
                float pxi = xxyDist_.xyCounts.p(Xi,value);
                entropyOfXi[Xi] += -pxi*log(pxi);
            }
        }
    }
    for(CategoricalAttribute Xi=0; Xi<noCatAtts_; Xi++) {
        if(active[Xi]) {
            for(CategoricalAttribute Xj=0; Xj<Xi; Xj++) {
                for(CatValue xiValue=0; xiValue<xxyDist_.getNoValues(Xi); xiValue++) {
                    for(CatValue xjValue=0; xjValue<xxyDist_.getNoValues(Xj); xjValue++) {

                    }
                }
            }
        }
    }


}

int aode_pw::getNextElement(std::vector<CategoricalAttribute> &order, CategoricalAttribute ca, unsigned int noSelected) {
    CategoricalAttribute c = ca + 1;
    while (active_[order[c]] == false && c < noSelected)
        c++;
    if (c < noSelected)
        return c;
    else
        return -1;
}

void aode_pw::finalisePass() {

//
//    ///infoWeight加权begin
//
//    for(int i=0; i<noCatAtts_; i++) {
//        infoWeightGeneral[i] = 0;
//    }
//    const double totalCount = xxyDist_.xyCounts.count;
//    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//
//        /*
//        float sumI = 0.0;
//        for (CatValue v1 = 0; v1 < xxyDist_.getNoValues(x1); v1++) {
//            const double x1Count = xxyDist_.xyCounts.getCount(x1,v1);
//            for (CatValue y = 0; y < noClasses_; y++) {
//                const double yCount = xxyDist_.xyCounts.getClassCount(y);
//                const double x1y = xxyDist_.xyCounts.getCount(x1, v1, y);
//                if(x1y && x1Count && yCount) {
//                    sumI +=(x1y/totalCount)*log(totalCount*x1y/(x1Count*yCount));
//                }
//            }
//        }
//
//        infoWeightGeneral[x1] -= sumI;
//        */
//
//
//        for (CategoricalAttribute x2 = 0; x2 < noCatAtts_; x2++) {
//            if(x1 == x2) {
//                continue;
//            }
//            float m = 0.0;
//            for (CatValue v1 = 0; v1 < xxyDist_.getNoValues(x1); v1++) {
//                for (CatValue v2 = 0; v2 < xxyDist_.getNoValues(x2); v2++) {
//                    for (CatValue y = 0; y < noClasses_; y++) {
//                        const double x1x2y = xxyDist_.getCount(x1, v1, x2, v2, y);
//                        if (x1x2y) {
//                            m +=  (x1x2y/totalCount )* log2(totalCount * x1x2y /
//                                                            (static_cast<double> (xxyDist_.xyCounts.getCount(x1, v1)) *
//                                                             xxyDist_.xyCounts.getCount(x2, v2,y)));
//
//                        }
//                    }
//                }
//            }
//            infoWeightGeneral[x1] += m;
//        }
//    }
//    std::cout<<"####";
//    for(int i=0; i<noCatAtts_; i++) {
//        std::cout<<infoWeightGeneral[i]<<" ";
//    }
//    std::cout<<std::endl;


    trainingIsFinished_ = true;
}



void aode_pw::classify(const instance &inst, std::vector<double> &classDist) {
    //printf("实例为：");
    //for(int i=0;i<noCatAtts_;i++)
    //printf("%d ",inst.getCatVal(i));
    //printf("\n");
    std::vector<bool> generalizationSet;

    generalizationSet.assign(noCatAtts_, false);

    //compute the generalisation set and substitution set for
    //lazy subsumption resolution
    if (subsumptionResolution == true) {

        for (CategoricalAttribute i = 1; i < noCatAtts_; i++) {
            const CatValue iVal = inst.getCatVal(i);
            const InstanceCount countOfxi = xxyDist_.xyCounts.getCount(i, iVal);

            for (CategoricalAttribute j = 0; j < i; j++) {
                if (!generalizationSet[j]) {
                    const CatValue jVal = inst.getCatVal(j);
                    const InstanceCount countOfxixj = xxyDist_.getCount(i, iVal,j, jVal);
                    const InstanceCount countOfxj = xxyDist_.xyCounts.getCount(j, jVal);

                    if (countOfxj == countOfxixj && countOfxj >= minCount) { //minCount=100
                        //xj->xi
                        //xi is a generalisation or substitution of xj
                        //once one xj has been found for xi, stop for rest j
                        generalizationSet[i] = true;
                        fathercount[j]++;
                        break;//因为i已经是general 已经没有希望了，所以不必对它继续考察其它的属性，直接就删掉了
                    } else if (countOfxi == countOfxixj&& countOfxi >= minCount)
                        //xi->xj
                    {
                        fathercount[i]++;
                        generalizationSet[j] = true;
                    }
                }
            }
        }
    }


    for (CatValue y = 0; y < noClasses_; y++) {
        classDist[y] = 0;
    }

    CatValue delta = 0;


// fdarray a two-dimensional fixed size array whose dimensions are not known at compile time
    fdarray<double> spodeProbs(noCatAtts_, noClasses_);
//spodeProbs里就是每个SPODE在C=i的时候的概率值，概率计算的时候去掉了general和频率为0的情况

    std::vector<bool> active(noCatAtts_, false);
    //计算P(Xi,Y)
    for (CatValue parent = 0; parent < noCatAtts_; parent++) {

        //discard the attribute that is not active or in generalization set
        if (!generalizationSet[parent]) { //如果是general那么就是要被扔掉
            const CatValue parentVal = inst.getCatVal(parent);

            if (xxyDist_.xyCounts.getCount(parent, parentVal) > 0) { //有资格做父节点
                delta++;
                active[parent] = true;
                for (CatValue y = 0; y < noClasses_; y++) {
                    spodeProbs[parent][y] = xxyDist_.xyCounts.jointP(parent, inst.getCatVal(parent), y);
                }
            }

        }
    }


    if (delta == 0) { //如果一个可以做父节点的都没有，就用NB分类,这样的情况很罕见，但是严谨总没错
        nbClassify(inst, classDist, xxyDist_.xyCounts);
        return;
    }
    //计算P(Xj|Xi,Y) 效果就相当于去掉了general 和不让频率为0的做父节点
    for (CategoricalAttribute x1 = 1; x1 < noCatAtts_; x1++) {
        if (!generalizationSet[x1]) {
            const bool x1Active = active[x1];

            for (CategoricalAttribute x2 = 0; x2 < x1; x2++) {
                if (!generalizationSet[x2]) {
                    const bool x2Active = active[x2];
                    for (CatValue y = 0; y < noClasses_; y++) {
                        if (x1Active) { //只有当x1的频率大于0的时候，才有资格做父节点
                            //// p(x1=v1, x2=v2, Y=y) using M-estimate      // p(a=v, Y=y) using M-estimate
                            // 计算的是P(x2=v2|x1=v1 , Y=y)
                            spodeProbs[x1][y] *= xxyDist_.jointP(x1, inst.getCatVal(x1), x2, inst.getCatVal(x2), y) / xxyDist_.xyCounts.jointP(x1, inst.getCatVal(x1), y);
                        }
                        if (x2Active) { //只有当x2的频率大于0的时候，才有资格做父节点
                            // 计算的是P(x1=v1|x2=v2 , Y=y)
                            spodeProbs[x2][y] *= xxyDist_.jointP(x1, inst.getCatVal(x1), x2, inst.getCatVal(x2), y) / xxyDist_.xyCounts.jointP(x2, inst.getCatVal(x2), y);

                        }
                    }

                }
            }
        }
    }





    /**********************************infoWeight加权begin*****************************************/
    std::vector<float> infoWeight(noCatAtts_,0);
    const double totalCount = xxyDist_.xyCounts.count;
    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
        if(!active[x1]) {
            continue;
        }

        //-logp(y|xi,xj)
        for (CategoricalAttribute x2 = 0; x2 < noCatAtts_; x2++) {
            if(x1 == x2) {
                continue;
            }
            float m = 0.0;
            CatValue v1 = inst.getCatVal(x1);
            CatValue v2 = inst.getCatVal(x2);
            for (CatValue y = 0; y < noClasses_; y++) {
                const double x1x2y = xxyDist_.getCount(x1, v1, x2, v2, y);
                if (x1x2y ) {
                    m +=  log2( x1x2y /
                                (static_cast<double> (xxyDist_.getCount(x1, v1, x2, v2) ) ));

                }
            }
            infoWeight[x1] -= m;
        }


        //+(n-2)logp(y|xi)
        float sumI = 0.0;
        CatValue v1 = inst.getCatVal(x1);
        const double x1Count = xxyDist_.xyCounts.getCount(x1,v1);
        for (CatValue y = 0; y < noClasses_; y++) {
            const double yCount = xxyDist_.xyCounts.getClassCount(y);
            const double x1y = xxyDist_.xyCounts.getCount(x1, v1, y);
            if(x1y && x1Count) {
                sumI +=log(x1y/(x1Count));
            }
        }
        infoWeight[x1] += (noCatAtts_-2)*sumI;



        //I(xi,Y)
//        float sumI = 0.0;
//        CatValue v1 = inst.getCatVal(x1);
//        const double x1Count = xxyDist_.xyCounts.getCount(x1,v1);
//        for (CatValue y = 0; y < noClasses_; y++) {
//            const double yCount = xxyDist_.xyCounts.getClassCount(y);
//            const double x1y = xxyDist_.xyCounts.getCount(x1, v1, y);
//            if(x1y && x1Count && yCount) {
//                sumI +=(x1y/totalCount)*log(totalCount*x1y/(x1Count*yCount));
//            }
//        }
//        infoWeight[x1] -= sumI;


        //I(xk,Y)
//        sumI = 0;
//        for (CategoricalAttribute x2 = 0; x2 < noCatAtts_; x2++) {
//            if(x1 == x2) {
//                continue;
//            }
//            CatValue v2 = inst.getCatVal(x2);
//            const double x2Count = xxyDist_.xyCounts.getCount(x2,v2);
//            for (CatValue y = 0; y < noClasses_; y++) {
//                const double yCount = xxyDist_.xyCounts.getClassCount(y);
//                const double x2y = xxyDist_.xyCounts.getCount(x2, v2, y);
//                if(x2y && x2Count && yCount) {
//                    sumI +=(x2y/totalCount)*log(totalCount*x2y/(x2Count*yCount));
//                }
//            }
//        }
//        infoWeight[x1] = sumI;


        //I(xi,xk)
//        sumI = 0.0;
//        for (CategoricalAttribute x2 = 0; x2 < noCatAtts_; x2++) {
//            if(x1 == x2) {
//                continue;
//            }
//            CatValue v2 = inst.getCatVal(x2);
//            const double x1x2Count = xxyDist_.getCount(x1,v1,x2,v2);
//            const double x1Count = xxyDist_.xyCounts.getCount(x1,v1);
//            const double x2Count = xxyDist_.xyCounts.getCount(x2,v2);
//            if(x1x2Count && x1Count && x2Count) {
//                sumI += (x1x2Count/totalCount) * log( (x1x2Count*totalCount)/(x1Count*x2Count) );
//            }
//        }
//        infoWeight[x1]-= sumI;




//        for (CategoricalAttribute x2 = 0; x2 < noCatAtts_; x2++) {
//            if(x1 == x2) {
//                continue;
//            }
//            float m = 0.0;
//            CatValue v1 = inst.getCatVal(x1);
//            CatValue v2 = inst.getCatVal(x2);
//            for (CatValue y = 0; y < noClasses_; y++) {
//                const double x1x2y = xxyDist_.getCount(x1, v1, x2, v2, y);
//                if (x1x2y ) {
//                    m +=  (x1x2y/totalCount )* log2(totalCount * x1x2y /
//                                                    (static_cast<double> (xxyDist_.xyCounts.getCount(x1, v1)) *
//                                                     xxyDist_.xyCounts.getCount(x2, v2,y)));
//
//                }
//            }
//            infoWeight[x1] += m;
//        }
    }

    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
        infoWeight[x1] = -infoWeight[x1];
    }




    ///权值处理

    ///权值为负置零
//    bool positive = false;
//    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//        if(infoWeight[x1] > 0) {
//            positive = true;
//            break;
//        }
//    }
//    if(positive) {
//        for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//            if(infoWeight[x1] < 0) {
//                infoWeight[x1] = 0;
//            }
//        }
//    }



    ///平移权值
    float minWeight = std::numeric_limits<float>::max();
    float maxWeight = -std::numeric_limits<float>::max();
    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
        if(infoWeight[x1] < minWeight) {
            minWeight = infoWeight[x1];
        }
        if(infoWeight[x1] > maxWeight) {
            maxWeight = infoWeight[x1];
        }
    }
    if(minWeight < 0) {
        minWeight = -minWeight;
        for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
            infoWeight[x1] += minWeight;
        }
    }





    ///sigmoid
//    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//        infoWeight[x1] = sigmoid(infoWeight[x1]);
//        infoWeightGeneral[x1] = sigmoid(infoWeightGeneral[x1]);
//    }




    /// general和local 权值调和平均
//    std::vector<float> harmonicMeanWeight(noCatAtts_,0);
//    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//        if((infoWeight[x1] + infoWeightGeneral[x1]) != 0) {
//            harmonicMeanWeight[x1] = (infoWeight[x1] * infoWeightGeneral[x1])/(infoWeight[x1] + infoWeightGeneral[x1]);
//        } else {
//            harmonicMeanWeight[x1] = 0;
//        }
//    }



    /// general和local 权值算数平均
//    std::vector<float> harmonicMeanWeight(noCatAtts_,0);
//    for (CategoricalAttribute x1 = 0; x1 < noCatAtts_; x1++) {
//        harmonicMeanWeight[x1] = (infoWeight[x1] + infoWeightGeneral[x1])/2;
//    }
//
//


    /*************************************加权end**************************************/


    for(int i=0; i<noClasses_; i++) {
        classDist[i] = 0;
    }

    for (CatValue parent = 0; parent < noCatAtts_; parent++) {

        if (active[parent]) { //频率大于0且不是general才是父节点
            for (CatValue y = 0; y < noClasses_; y++) {
                classDist[y] +=  spodeProbs[parent][y] *infoWeight[parent] ;
//                classDist[y] += spodeProbs[parent][y] * harmonicMeanWeight[parent] ;
            }
        }
    }

    normalise(classDist);


    float GenAttr = 0;
    for (CategoricalAttribute i = 0; i < noCatAtts_; i++) {
        if (active[i] == true)
            GenAttr++; //统计不是general和频率为空的属性总数
    }
    UsedAttrRatio += GenAttr / noCatAtts_;
}

void aode_pw::nbClassify(const instance &inst, std::vector<double> &classDist,
                                xyDist &xyDist_) {

    for (CatValue y = 0; y < noClasses_; y++) {
        double p = xyDist_.p(y) * (std::numeric_limits<double>::max() / 2.0);
        // scale up by maximum possible factor to reduce risk of numeric underflow

        for (CategoricalAttribute a = 0; a < noCatAtts_; a++) {
            p *= xyDist_.p(a, inst.getCatVal(a), y);
        }

        assert(p >= 0.0);
        classDist[y] = p;
    }
    normalise(classDist);





}

//@Naomi
void aode_pw::compareProbability()
//用于计算验证 P(Xj=Xji|Xk=Xkp,C=Cq) < P(Xj=Xji|C=Cq) 体现在程序里就是计算 P(X1=V1|X2=V2,C=classValue)<P(X1=V1|C=classValue) 算的时候，只算X1>X2的，只用算一半，X1与X2互换的时候，大小是不会变的，也就是说如果有 P(X1=V1|X2=V2,C=classValue)<P(X1=V1|C=classValue) 那么就有 P(X2=V2|X1=V1,C=classValue)<P(X2=V2|C=classValue)
//使用的时候
//1.把XVal.cpp里 theLearner->train(*filteredInstanceStream);的下面的内容全部注释掉
//2.  bool XValInstanceStream::advance(instance &inst) 只前进，不略过任何实例 除了
// if (source_->advance(inst))
//                {
//                    count_++;
//                    return true;
//                }
//                else return false;
//这些,全部注释掉
//3.把AODE里的classify全部注释掉，只留下compareProbability();
//4.在learnerRegistry.cpp里返回 return new aode(argv, end); 其它都可以注释掉
//5.要把noFolds改成1 for (unsigned int fold = 0; fold < noFolds; fold++)//对所有的fold进行循环
//其它都不用变
//计算概率的时候记住 所有的答案都在xxyDist_.count_里面，只要遍历count_就好了
{
    int noClasses=xxyDist_.getNoClasses();
    int noValues=0;
    int noFatherValues=0;
    double probabilityfathered=0.0;
    double probability=0.0;
    int totalcount=0;//计算总数
    int noFatherCount=0;//计算不需要父节点的次数
    double delta=0.0;//没有父节点和有父节点的概率差
    double deltaProportionSum=0.0;//降幅的平均值
    for (CategoricalAttribute att = 1; att < xxyDist_.getNoCatAtts(); att++) { //之所以x1不从0开始，是应为count_[x1].resize(stream.getNoValues(x1) * x1);x1=0的时候就是 0
        noValues=xxyDist_.getNoValues(att);//x1的取值个数

        for (CatValue attValue = 0; attValue < noValues; ++attValue) {
            for (CategoricalAttribute father = 0; father < att; father++) { //只算X2<X1的属性
                noFatherValues=xxyDist_.getNoValues(father);//x2的取值个数
                for(CatValue fatherValue = 0; fatherValue < noFatherValues; ++fatherValue)
                    for(int classValue=0; classValue<noClasses; classValue++) {

                        probabilityfathered=xxyDist_.jointP(att,attValue,father,fatherValue, classValue) / xxyDist_.xyCounts.jointP(father, fatherValue, classValue);
                        //之所以有很多这个概率算出来都是一样的，比如 P(X8=X89|X5=X52,C=1)和 P(X8=X89|X5=X56,C=1)都是0.000324 那是因为本来它们的count_就没多少，全靠M估计强撑着，而M估计是一样的，所以很多值的概率都一样
                        //比如 count(X3=X35,X1=X13,C=0)=0  P(X3=X35|X1=X13,C=0)=0.001235  count(X3=X35,X1=X14,C=0)=0 P(X3=X35|X1=X14,C=0)=0.001235
                        probability=xxyDist_.xyCounts.p(att, attValue, classValue);
                        // p(x1=v1, x2=v2, Y=y) inline double xxxyDist::jointP(CategoricalAttribute x1, CatValue v1, CategoricalAttribute x2, CatValue v2, CatValue y) const
                        // p(a=v, Y=y)   inline double xyDist::jointP(CategoricalAttribute a, CatValue v, CatValue y)
                        //  p(a=v|Y=y)  inline double xyDist::p(CategoricalAttribute a, CatValue v, CatValue y)  p(a=v|Y=y)
                        // printf("P(X%d=X%d%d|X%d=X%d%d,C=%d)=%f\n",x1,x1,v1,x2,x2,v2,classValue,probabilityfathered);
                        //  printf("P(X%d=X%d%d|C=%d)=%f\n",x1,x1,v1,classValue,probability);
                        if(probabilityfathered<probability) {

                            noFatherCount++;
                            delta=probability-probabilityfathered;
                            //  printf("count(X%d=X%d%d,X%d=X%d%d,C=%d)=%d\n",x1,x1,v1,x2,x2,v2,classValue,xxyDist_.count_[x1][v1 * x1 + x2][v2 * xxyDist_.noOfClasses_ + classValue]); 要把xxyDist_的数据成员改成公有的
                            //  printf("P(X%d=X%d%d|X%d=X%d%d,C=%d)=%f\n",x1,x1,v1,x2,x2,v2,classValue,probabilityfathered);
                            //   printf("P(X%d=X%d%d|C=%d)=%f\n",x1,x1,v1,classValue,probability);
                            //  printf("父节点拖累了我,使我降低了：%f\n",(delta/probability));
                            deltaProportionSum+=(double)delta/probability;
                        }
                        totalcount++;
                    }

            }
        }
    }
    double proportion=(double)noFatherCount/totalcount;
    double deltaProportionAvg=(double)deltaProportionSum/noFatherCount;
    //printf("总共有 %d 次，父节点拖累了我 %d 次,占比为%f ,平均降幅为%f\n",totalcount,noFatherCount,proportion,deltaProportionAvg);

}



//@Naomi
void aode_pw::compareH()
//用于计算验证 H(Xi=xi|Xj=xj,Ck=c) > H(Xi=xi|Ck=c)
//使用的时候
//1.把XVal.cpp里 theLearner->train(*filteredInstanceStream);的下面的内容全部注释掉  但是要保留xVal.cpp 149行的classify
//2.  bool XValInstanceStream::advance(instance &inst) 只前进，不略过任何实例 除了
// if (source_->advance(inst))
//                {
//                    count_++;
//                    return true;
//                }
//                else return false;
//这些,全部注释掉
//3.把AODE里的classify全部注释掉，只留下comparH();
//4.在learnerRegistry.cpp里返回 return new aode(argv, end); 其它都可以注释掉
//5.要把noFolds改成1 for (unsigned int fold = 0; fold < noFolds; fold++)//对所有的fold进行循环
//其它都不用变
//计算概率的时候记住 所有的答案都在xxyDist_.count_里面，只要遍历count_就好了
{
    int noClasses = xxyDist_.getNoClasses();
    int noValues = 0;
    int noFatherValues = 0;
    double probabilityfathered = 0.0;
    double probability = 0.0;
    int totalcount = 0; //计算总数
    int noFatherCount = 0; //计算不需要父节点的次数
    double delta = 0.0; //没有父节点和有父节点的概率差
    double deltaProportionSum = 0.0; //降幅的平均值
    double alljointp = 0.0;
    double jointp = 0.0;
    for (CategoricalAttribute att = 0; att < xxyDist_.getNoCatAtts(); att++) { //之所以x1不从0开始，是应为count_[x1].resize(stream.getNoValues(x1) * x1);x1=0的时候就是 0
        noValues = xxyDist_.getNoValues(att); //att的取值个数

        for (CatValue attValue = 0; attValue < noValues; ++attValue) {
            for (CategoricalAttribute father = 0; father < xxyDist_.getNoCatAtts(); father++) {
                if (father != att) { //att与father不能是同一个
                    noFatherValues = xxyDist_.getNoValues(father);
                    for (CatValue fatherValue = 0; fatherValue < noFatherValues; ++fatherValue)
                        for (int classValue = 0; classValue < noClasses; classValue++) {
                            alljointp = xxyDist_.jointP(att, attValue, father, fatherValue, classValue);
                            probabilityfathered = alljointp / xxyDist_.xyCounts.jointP(father, fatherValue, classValue);
                            //之所以有很多这个概率算出来都是一样的，比如 P(X8=X89|X5=X52,C=1)和 P(X8=X89|X5=X56,C=1)都是0.000324 那是因为本来它们的count_就没多少，全靠M估计强撑着，而M估计是一样的，所以很多值的概率都一样
                            //比如 count(X3=X35,X1=X13,C=0)=0  P(X3=X35|X1=X13,C=0)=0.001235  count(X3=X35,X1=X14,C=0)=0 P(X3=X35|X1=X14,C=0)=0.001235
                            jointp=xxyDist_.xyCounts.jointP(att,attValue,classValue);
                            probability = xxyDist_.xyCounts.p(att, attValue, classValue);
                            // p(x1=v1, x2=v2, Y=y) inline double xxxyDist::jointP(CategoricalAttribute x1, CatValue v1, CategoricalAttribute x2, CatValue v2, CatValue y) const
                            // p(a=v, Y=y)   inline double xyDist::jointP(CategoricalAttribute a, CatValue v, CatValue y)
                            //  p(a=v|Y=y)  inline double xyDist::p(CategoricalAttribute a, CatValue v, CatValue y)  p(a=v|Y=y)
                            // printf("P(X%d=X%d%d|X%d=X%d%d,C=%d)=%f\n",x1,x1,v1,x2,x2,v2,classValue,probabilityfathered);
                            //  printf("P(X%d=X%d%d|C=%d)=%f\n",x1,x1,v1,classValue,probability);
                            if (alljointp*log(probabilityfathered)<jointp*log(probability)) {

                                noFatherCount++;
                                //  delta = probability - probabilityfathered;
                                //  printf("count(X%d=X%d%d,X%d=X%d%d,C=%d)=%d\n",x1,x1,v1,x2,x2,v2,classValue,xxyDist_.count_[x1][v1 * x1 + x2][v2 * xxyDist_.noOfClasses_ + classValue]); 要把xxyDist_的数据成员改成公有的
                                //  printf("P(X%d=X%d%d|X%d=X%d%d,C=%d)=%f\n",x1,x1,v1,x2,x2,v2,classValue,probabilityfathered);
                                //   printf("P(X%d=X%d%d|C=%d)=%f\n",x1,x1,v1,classValue,probability);
                                //  printf("父节点拖累了我,使我降低了：%f\n",(delta/probability));
                                //  deltaProportionSum += (double) delta / probability;
                            }
                            totalcount++;
                        }
                }
            }
        }
    }
    double proportion = (double) noFatherCount / totalcount;
    // double deltaProportionAvg = (double) deltaProportionSum / noFatherCount;
    //printf("总共有 %d 次，父节点拖累了我 %d 次,占比为%f \n", totalcount, noFatherCount, proportion);

}

