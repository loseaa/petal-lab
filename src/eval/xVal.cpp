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
#include "resultsCollector.h"
#include "instanceStreamDiscretiser.h"
#include "correlationMeasures.h"

#include <assert.h>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#endif

/**
 * Run cross-validation and report the usual performance measures.
 *
 * @param args the text following -x: "<folds>[,<experiments>[,<fold>]]".
 *             Naming a fold restricts the run to that fold only, which is
 *             useful when re-running one fold of a long experiment.
 *
 * Every experiment reshuffles the fold assignment; within an experiment each
 * fold trains on the other folds and tests on its own. Results are printed in
 * full and, independently, pushed into the global ResultsCollector so that
 * --json can emit them for the web front-end.
 */
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
  // No aucSD: AUC is computed over the pooled predictions, not per fold.

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

    for (unsigned int fold = 0; fold < noFolds; fold++) {
      // A fold may be named as the third -x argument; when it is, only that
      // fold runs. Otherwise specifiedFold == noFolds and all folds run.
      if (specifiedFold < noFolds) {
        if (fold != specifiedFold) continue;

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

      // probs is [class][instance]: AUC needs one full class vector per
      // instance; ResultsCollector transposes it to [instance][class].

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
          }

          trueClasses.push_back(trueClass);

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
            printf("\n0-1 loss (fold %d): " PETAL_FLOAT_FMT "\n", fold, foldzeroOneLoss/static_cast<double>(foldcount));
            printf("RMSE (fold %d): " PETAL_FLOAT_FMT "\n", fold, sqrt(foldsquaredError/foldcount));
            printf("RMSE All Classes (fold %d):  " PETAL_FLOAT_FMT "\n", fold, sqrt(foldsquaredErrorAll/(foldcount* noClasses)));
            printf("Logarithmic Loss (fold %d):  " PETAL_FLOAT_FMT "\n", fold, -foldlogLoss/foldcount);
            printf("--------------------------------------------\n");
        }
      }
    }

    // AUC is over the pooled predictions of all folds, so it needs the whole
    // experiment rather than one fold's worth of them.
    const double a = calcMultiAUC(probs, trueClasses);

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
      printf(PETAL_FLOAT_FMT "\n", MCC);
    }

    // Structured results for the web front-end (see src/utils/resultsCollector.h).
    // Collected unconditionally: ResultsCollector::write() only runs when --json
    // was supplied, so a plain text run pays just a few vector pushes per learner.
    {
      const std::string learnerName = *theLearner->getName();

      results().setMode("xval");
      results().addMetric(learnerName, "0-1_loss",   static_cast<int>(exp), -1, zOLoss.back());
      results().addMetric(learnerName, "rmse",       static_cast<int>(exp), -1, rmse.back());
      results().addMetric(learnerName, "rmse_all",   static_cast<int>(exp), -1, rmsea.back());
      results().addMetric(learnerName, "log_loss",   static_cast<int>(exp), -1, logloss.back());
      results().addMetric(learnerName, "auc",        static_cast<int>(exp), -1, auc.back());
      results().addMetric(learnerName, "mcc",        static_cast<int>(exp), -1, calcMCC(xtab));
      results().addMetric(learnerName, "train_time", static_cast<int>(exp), -1,
                          static_cast<double>(trainTimeM.back()));
      results().addMetric(learnerName, "test_time",  static_cast<int>(exp), -1,
                          static_cast<double>(testTimeM.back()));

      // Per-fold values drive the box plots. Empty folds are skipped upstream,
      // so this index counts non-empty folds rather than naming the fold itself.
      for (size_t fi = 0; fi < foldZOLoss.size(); ++fi) {
        results().addMetric(learnerName, "0-1_loss", static_cast<int>(exp),
                            static_cast<int>(fi), foldZOLoss[fi]);
        results().addMetric(learnerName, "rmse", static_cast<int>(exp),
                            static_cast<int>(fi), foldrmse[fi]);
        results().addMetric(learnerName, "rmse_all", static_cast<int>(exp),
                            static_cast<int>(fi), foldrmsea[fi]);
        results().addMetric(learnerName, "log_loss", static_cast<int>(exp),
                            static_cast<int>(fi), foldlogloss[fi]);
      }

      std::vector<unsigned int> flat(noClasses * noClasses);
      for (unsigned int y = 0; y < noClasses; ++y) {
        for (unsigned int p = 0; p < noClasses; ++p) {
          flat[y * noClasses + p] = static_cast<unsigned int>(xtab[y][p]);
        }
      }
      results().addConfusion(learnerName, noClasses, flat);

      // xVal already gathers per-instance probabilities for AUC; hand the same
      // data to the collector so the front-end can draw ROC/PR curves.
      results().addPredictions(learnerName, probs, trueClasses);
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
    printf("\nMean 0-1 loss: " PETAL_FLOAT_FMT " + " PETAL_FLOAT_FMT "\nMean RMSE: " PETAL_FLOAT_FMT " + " PETAL_FLOAT_FMT "\nMean RMSE All: " PETAL_FLOAT_FMT " + " PETAL_FLOAT_FMT "\n"
            "Mean Logarithmic Loss: " PETAL_FLOAT_FMT " + " PETAL_FLOAT_FMT "\nMean Training time: %ld\nMean Classification time: %ld\n",
            mean(zOLoss), stddev(zOLoss), mean(rmse), stddev(rmse), mean(rmsea), stddev(rmsea), mean(logloss),
            stddev(logloss),mean(trainTimeM),mean(testTimeM));
  }
  else {
    putchar('\n');
  }
}
