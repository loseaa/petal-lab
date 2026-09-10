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
** Please report any bugs to Geoff Webb <geoff.webb@monash.edu>
*/

#ifdef _MSC_VER
#ifdef _DEBUG
//#define _CRTDBG_MAP_ALLOC
//#ifndef DBG_NEW
//#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
//#define new DBG_NEW
//#endif
#include <stdlib.h>
#include <crtdbg.h>
#endif
#endif



#include "xVal.h"
#include "xValInstanceStream.h"
#include "utils.h"
#include "globals.h"
#include "crosstab.h"
#include "instanceStreamDiscretiser.h"
#include "correlationMeasures.h"

#include <iomanip>
#include <fstream>
#include <assert.h>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#endif

void xVal(learner *theLearner, InstanceStream &instStream, FilterSet &filters, char* args) {
  unsigned int noFolds = 10;
  unsigned int noExperiments = 1;
  std::vector<unsigned int*> vals;
  unsigned int  specifiedFold=noFolds;
  unsigned int argsLength;

  vals.push_back(&noFolds);
  vals.push_back(&noExperiments);
  vals.push_back(&specifiedFold);

  argsLength=getUIntListFromStr(args, vals, "cross validation settings");

  //if the fold has not been specified, the fold will be noFolds
  if(argsLength<3)
  {
	  specifiedFold=noFolds;
  }

  const unsigned int noClasses = instStream.getNoClasses();

  std::vector<double> classDist(noClasses);
  std::vector<double> zOLoss;  // 0-1 loss from each experiment
  std::vector<double> rmse;    // rmse from each experiment
  std::vector<double> rmsea;    // rmse for all classes from each experiment
  std::vector<double> logloss;    // logarithmic loss for all classes from each experiment
  std::vector<double> auc;    // auc from each experiment

  std::vector<double> zOLossSD;  // standard deviation of 0-1 loss from each experiment
  std::vector<double> rmseSD;    // standard deviation of rmse from each experiment
  std::vector<double> rmseaSD;    // standard deviation of rmse for all classes from each experiment
  std::vector<double> loglossSD;    // standard deviation of logarithmic loss for all classes from each experiment
  //std::vector<double> aucSD;    // standard deviation of auc from each experiment

  std::vector<long int> trainTimeM;  //training time from each experiment
  std::vector<long int> testTimeM;    //test time from each experiment

  for (unsigned int exp = 0; exp < noExperiments; exp++) {
    if (verbosity >= 1) printf("Cross validation experiment %d for %s\n", exp+1, instStream.getName());

    InstanceCount count = 0;
    unsigned int zeroOneLoss = 0;
    double squaredError = 0.0;
    double squaredErrorAll = 0.0;
    double logLoss = 0.0;
    long int trainTime = 0;
    long int testTime = 0;

    std::vector<double> foldZOLoss;   ///< 0-1 loss from each fold
    std::vector<double> foldrmse;     ///< rmse from each fold
    std::vector<double> foldrmsea;    ///< rmse for all classes from each fold
    std::vector<double> foldlogloss;  ///< logarithmic loss for all classes from each fold

    std::vector<std::vector<double> > probs(noClasses);
    std::vector<CatValue>  trueClasses;

    crosstab<InstanceCount> xtab(noClasses);
    XValInstanceStream xValStream(&instStream, noFolds, exp);

	 // std::ofstream proFile("probs.txt");
     // std::ofstream classFile("class.txt");

    for (unsigned int fold = 0; fold < noFolds; fold++) {

        //if the specified fold is less than noFolds, run only the specified fold
        //otherwise run all the folds
        if(specifiedFold<noFolds) {
            if(fold!=specifiedFold)
            continue;

        printf("\nResults for fold %d\n", fold);
        }


      InstanceCount foldcount = 0;      ///< a count of the number of test instances in the fold
      unsigned int foldzeroOneLoss = 0;
      double foldsquaredError = 0.0;
      double foldsquaredErrorAll = 0.0;
      double foldlogLoss = 0.0;
      long int timeFold = 0;
      #ifdef __linux__
      struct rusage usage;
      #endif

      if (verbosity >= 2) printf("Fold %d\n", fold);

      xValStream.startSubstream(fold, true);    // start the cross validation training stream for the fold

      InstanceStream* filteredInstanceStream = filters.apply(&xValStream);  // train the filters on the training stream

      #ifdef __linux__
      getrusage(RUSAGE_SELF, &usage);
      timeFold= usage.ru_utime.tv_sec+usage.ru_stime.tv_sec;
      #endif

      theLearner->train(*filteredInstanceStream);  // train the classifier on the filtered training stream

      #ifdef __linux__
      getrusage(RUSAGE_SELF, &usage);
      trainTime += ((usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)-timeFold);
      #endif

      xValStream.startSubstream(fold, false); // reset the cross validation stream to the test stream for the fold, leaving the trained filters in place

      filteredInstanceStream->rewind();  // rewind the filtered stream to the start

      instance inst(*filteredInstanceStream); // create a test instance

      #ifdef __linux__
      getrusage(RUSAGE_SELF, &usage);
      timeFold= usage.ru_utime.tv_sec+usage.ru_stime.tv_sec;
      #endif




     //std::vector<std::vector<double> > foldProbs(3);


//
//      foldProbs[0].push_back(0.2);
//      foldProbs[1].push_back(0.8);
//
//      foldProbs[0].push_back(0.3);
//      foldProbs[1].push_back(0.7);
//
//      foldProbs[0].push_back(0.5);
//      foldProbs[1].push_back(0.5);
//      foldProbs[0].push_back(0.5);
//      foldProbs[1].push_back(0.5);
//
//      foldProbs[0].push_back(0.5);
//      foldProbs[1].push_back(0.5);
//
//      foldProbs[0].push_back(0.5);
//      foldProbs[1].push_back(0.5);
//
//            foldProbs[0].push_back(0.7);
//      foldProbs[1].push_back(0.3);
//
//foldTrueClasses.push_back(1);
//foldTrueClasses.push_back(1);
//foldTrueClasses.push_back(0);
//foldTrueClasses.push_back(0);
//foldTrueClasses.push_back(1);
//foldTrueClasses.push_back(1);
//foldTrueClasses.push_back(0);
//
//      foldProbs[0].push_back(0.2);
//      foldProbs[1].push_back(0.5);
//         foldProbs[2].push_back(0.3);
//      foldProbs[0].push_back(0.5);
//      foldProbs[1].push_back(0.3);
//           foldProbs[2].push_back(0.2);
//         foldProbs[0].push_back(0.4);
//      foldProbs[1].push_back(0.2);
//               foldProbs[2].push_back(0.4);
//            foldProbs[0].push_back(0.2);
//      foldProbs[1].push_back(0.3);
//               foldProbs[2].push_back(0.5);
//
//foldTrueClasses.push_back(0);
//foldTrueClasses.push_back(2);
//foldTrueClasses.push_back(1);
//foldTrueClasses.push_back(1);



      while (!filteredInstanceStream->isAtEnd()) {
        if (filteredInstanceStream->advance(inst)) {
          count++;
          foldcount++;


          theLearner->classify(inst, classDist);

          const CatValue prediction = indexOfMaxVal(classDist);
          const CatValue trueClass = inst.getClass();

          if (prediction != trueClass) {
            zeroOneLoss++;
            foldzeroOneLoss++;
          }

          const double error = 1.0-classDist[trueClass];
          squaredError += error * error;
          squaredErrorAll += error * error;
          logLoss += log2(classDist[trueClass]);
          foldsquaredError += error * error;
          foldsquaredErrorAll += error * error;
          foldlogLoss += log2(classDist[trueClass]);
          for (CatValue y = 0; y < noClasses; y++) {
            if (y != trueClass) {
              const double err = classDist[y];
              squaredErrorAll += err * err;
              foldsquaredErrorAll += err * err;
            }
          }

          xtab[trueClass][prediction]++;

          for (CatValue y = 0; y < noClasses; y++) {
            probs[y].push_back(classDist[y]);
           // proFile<<std::fixed<<std::setprecision(20)<<classDist[y]<<' ';
          }

          trueClasses.push_back(trueClass);

      //    classFile<<trueClass<<std::endl;
       //   proFile<<std::endl;

          //print(classDist);
         // printf("\n");
        }
      }



      #ifdef __linux__
      getrusage(RUSAGE_SELF, &usage);
      testTime += ((usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)-timeFold);
      #endif

      if (foldcount == 0) {
        printf("Fold %d is empty\n", fold);
      }
      else {

        foldZOLoss.push_back(foldzeroOneLoss/static_cast<double>(foldcount));
        foldrmse.push_back(sqrt(foldsquaredError/foldcount));
        foldrmsea.push_back(sqrt(foldsquaredErrorAll/(foldcount* noClasses)));
        foldlogloss.push_back(-foldlogLoss/foldcount);


        if (verbosity >= 2){
            printf("\n0-1 loss (fold %d): %0.4f\n", fold, foldzeroOneLoss/static_cast<double>(foldcount));
            printf("RMSE (fold %d): %0.4f\n", fold, sqrt(foldsquaredError/foldcount));
            printf("RMSE All Classes (fold %d):  %0.4f\n", fold, sqrt(foldsquaredErrorAll/(foldcount* noClasses)));
            printf("Logarithmic Loss (fold %d):  %0.4f\n", fold, -foldlogLoss/foldcount);
            printf("--------------------------------------------\n");
        }
      }
    }

   // print(trueClasses);printf("\n");
	//proFile.close();
	//classFile.close();

     //compute the auc based on classDist and trueClasses
    double a=calcMultiAUC(probs, trueClasses);

//	if(noClasses==2)
//		a=calcBinaryAUC(probs, trueClasses);


    zOLoss.push_back(zeroOneLoss/static_cast<double>(count));
    assert(squaredError >= 0);
    rmse.push_back(sqrt(squaredError/count));
    rmsea.push_back(sqrt(squaredErrorAll/(count * noClasses)));
    logloss.push_back(-logLoss/count);
    auc.push_back(a);

    zOLossSD.push_back(stddev(foldZOLoss));
    rmseSD.push_back(stddev(foldrmse));
    rmseaSD.push_back(stddev(foldrmsea));
    loglossSD.push_back(stddev(foldlogloss));


    trainTimeM.push_back(trainTime /= noFolds);
    testTimeM.push_back(testTime /= noFolds);

    if (verbosity >= 1) {
      theLearner->printClassifier();
      printf("number of instances is %" ICFMT "\n",count);
      printResults(xtab, xValStream);
      double MCC = calcMCC(xtab);
      printf("\nMCC:\n");
      printf("%0.4f\n", MCC);
    }
  }



  printf("\n0-1 loss:\n");
  print(zOLoss);
  if(specifiedFold==noFolds) {
    printf("\n+/-: ");
    print(zOLossSD);
  }
  printf("\nRMSE:\n");
  print(rmse);
  if(specifiedFold==noFolds) {
    printf("\n+/-: ");
    print(rmseSD);
  }
  printf("\nRMSE All Classes:\n");
  print(rmsea);
  if(specifiedFold==noFolds) {
    printf("\n+/-: ");
    print(rmseaSD);
  }
  printf("\nLogarithmic Loss:\n");
  print(logloss);
  if(specifiedFold==noFolds) {
    printf("\n+/-:");
    print(loglossSD);
  }

  printf("\nAUC:\n");
  print(auc);

  printf("\nTraining time: ");
  print(trainTimeM); printf(" seconds");
  printf("\nClassification time: ");
  print(testTimeM); printf(" seconds");

  if (noExperiments > 1) {
    printf("\nMean 0-1 loss: %0.4f + %0.4f\nMean RMSE: %0.4f + %0.4f\nMean RMSE All: %0.4f + %0.4f\n"
            "Mean Logarithmic Loss: %0.4f + %0.4f\nMean Training time: %ld\nMean Classification time: %ld\n",
            mean(zOLoss), stddev(zOLoss), mean(rmse), stddev(rmse), mean(rmsea), stddev(rmsea), mean(logloss),
            stddev(logloss),mean(trainTimeM),mean(testTimeM));
  }
  else {
    putchar('\n');
  }
}
