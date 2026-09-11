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
#include "streamTest.h"
#include "instanceFile.h"
#include "utils.h"
#include "globals.h"
#include "instanceStreamDiscretiser.h"
#include "correlationMeasures.h"
#include "incrementalLearner.h"
#include "ShuffledInstanceStream.h"

#include <typeinfo>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <queue>
#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#include <cmath>
#else
#include <float.h>
#endif

const bool selfContainedPlots = false;  // if true then plot files contain gnuplot commands.  If false then plotfiles just contain the data.

void StreamTestArgs::getArgs(char*const*& argv, char*const* end) {
  while (argv != end) {
    if (*argv[0] != '+') {
      break;
    }
    else if (streq(argv[0]+1, "auprc")) {
      calcAUPRC_ = true;
    }
    else if (argv[0][1] == 'f') {
      getUIntFromStr(argv[0]+2, freq_, "Output frequency");;
    }
    else if (argv[0][1] == 'p') {
      plotfile_ = fopen(argv[0]+2, "w");
      if (plotfile_ == NULL) error("Cannot open %s for plotting", argv[0]+2);
    }
    else if (streq(argv[0]+1, "rmse")) {
      zoLoss_ = false;
    }
    else if (argv[0][1] == 's') {
      getUIntFromStr(argv[0]+2, smoothing_, "Smoothing Sample Size");;
    }
    else if (argv[0][1] == 'R') {
      getUIntFromStr(argv[0]+2, unShuffledRepetitions_, "Unshuffled Repetitions");;
    }
    else if (argv[0][1] == 'S') {
      getUIntFromStr(argv[0]+2, shuffledRepetitions_, "Shuffled Repetitions");;
    }
    else break;

    ++argv;
  }

  if (freq_ == 0) {
    if (smoothing_ > 200) {
      freq_ = smoothing_ / 100;
    }
    else if (smoothing_ > 20) {
      freq_ = smoothing_ / 10;
    }
    else {
      freq_ = 1;
    }
  }
}


/**
 * Prequential ("test-then-train") evaluation of one incremental learner.
 *
 * Every instance is classified with the current model *before* being used to
 * update it, so the running error reflects how the model performs on data it
 * has never seen, and the whole stream is used for both testing and training.
 */
void doStreamTest(IncrementalLearner *learner, InstanceStream &sourceInstanceStream, FilterSet &filters, const StreamTestArgs &args) {
  InstanceStream* instanceStream = filters.apply(&sourceInstanceStream);

  const unsigned int noClasses = instanceStream->getNoClasses();

  crosstab<InstanceCount> xtab(noClasses);
  
  long int trainTime = 0;
  #ifdef __linux__
  struct rusage usage;
  #endif

  #ifdef __linux__
  getrusage(RUSAGE_SELF, &usage);
  trainTime = usage.ru_utime.tv_sec+usage.ru_stime.tv_sec;
  #endif

  learner->testCapabilities(*instanceStream);
  learner->reset(*instanceStream);
  learner->initialisePass();
  
  #ifdef __linux__
  getrusage(RUSAGE_SELF, &usage);
  trainTime = ((usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)-trainTime);
  #endif

  instance inst(*instanceStream);

  if (verbosity >= 1) printf("Testing against file %s\n", sourceInstanceStream.getName());

  std::vector<double> classDist(noClasses);
  InstanceCount count = 0;
  InstanceCount errors = 0;
  double squaredError = 0.0;
  InstanceCount lastErrors = 0;

  if (args.plotfile_ == NULL) {
    printf("<<< BEGIN STREAM RESULTS <<<\n");
    for (CatValue y = 0; y < instanceStream->getNoClasses(); y++) {
      printf("%s, ", instanceStream->getClassName(y));
    }
    printf(", index\n");
  }
  else {
    if (selfContainedPlots)
      fprintf(args.plotfile_, 
        "# Gnuplot script file for plotting inline data\n"
        "set   autoscale                        # scale axes automatically\n"
        "unset log                              # remove any log-scaling\n"
        "unset label                            # remove any previous labels\n"
        "set xtic auto                          # set xtics automatically\n"
        "set ytic auto                          # set ytics automatically\n"
        "set title 'Stream Results for %s'\n"
        "set xlabel 'Time Step (t)'\n"
        "set ylabel 'Error'\n"
        "set key \n"
        "set terminal postscript\n"
        "plot    '-' using 1:2 ps 0 title '%s' with linespoints\n",
        instanceStream->getName(),
        learner->getName()->c_str()
        );
  }

  // Sliding window of the last smoothing_ errors; averaged to give the smoothed
  // curve that stream plots need. A deque rather than a queue because we iterate
  // over it to take the mean, and std::queue offers no iterators.
  std::deque<double> errorWindow;

  while (!instanceStream->isAtEnd()) {
    if (instanceStream->advance(inst)) {
      count++;

      learner->classify(inst, classDist);

      const CatValue prediction = indexOfMaxVal(classDist);
      const CatValue trueClass = inst.getClass();

      const double error = 1.0-classDist[trueClass];

      errorWindow.push_back(error);
      if (errorWindow.size() > args.smoothing_) errorWindow.pop_front();

      xtab[trueClass][prediction]++;

      squaredError += error * error;
      if (prediction != trueClass) errors++;

      // Sampling: a point is emitted every freq_ instances, so that a long
      // stream does not produce an unmanageable amount of output.
      if (args.freq_ <= 1 || count%args.freq_ == 0) {
        if (args.plotfile_ == NULL) {
          // output the true class
          printf("%d\n", trueClass);
        }
        else {
          if (args.zoLoss_) {
            // output the error rate over the last interval
            fprintf(args.plotfile_, "%d\t" PETAL_FLOAT_FMT "\n", count, (errors-lastErrors)/static_cast<double>(args.freq_));
            lastErrors = errors;
          }
          else if (errorWindow.size() >= args.smoothing_) {
            fprintf(args.plotfile_, "%d\t" PETAL_FLOAT_FMT "\n", count, mean(errorWindow));
          }
        }
      }

      learner->train(inst);
    }
  }

  if (args.plotfile_ == NULL) {
    printf("<<< END STREAM RESULTS <<<\n");
  }
  else {
    if (selfContainedPlots)
      fprintf(args.plotfile_, "EOF\n");
    fclose(args.plotfile_);
  }

  printf("\n%" ICFMT " test cases\n", count);

  if (verbosity >= 1) {
    instanceStream->printStats();

    learner->printClassifier();
    printResults(xtab, *instanceStream);

    double MCC = calcMCC(xtab);
    printf("\nMCC: " PETAL_FLOAT_FMT "\n", MCC);
  }

  printf("0-1 loss: " PETAL_FLOAT_FMT "\nRMSE: " PETAL_FLOAT_FMT "\nTraining time: %ld\n",
            errors/static_cast<double>(count), sqrt(squaredError/count), trainTime);
}

void streamTest(learner *theLearner, InstanceStream &sourceInstanceStream, FilterSet &filters, const StreamTestArgs &args) {
  // Prequential evaluation only makes sense for a learner that can be updated
  // one instance at a time; a batch learner has nothing to update with.
  IncrementalLearner* learner = dynamic_cast<IncrementalLearner*>(theLearner);

  if (learner == NULL) {
    error("streamTest requires an incremental learner");
  }

  if (args.shuffledRepetitions_) {
    ShuffledInstanceStream shuffledStream;

    shuffledStream.setSource(sourceInstanceStream);

    for (unsigned int i = 0; i < args.shuffledRepetitions_; i++) {
      shuffledStream.shuffle();
      shuffledStream.rewind();
      doStreamTest(learner, shuffledStream, filters, args);
    }
  }
  else if (args.unShuffledRepetitions_) {
    for (unsigned int i = 0; i < args.unShuffledRepetitions_; i++) {
      sourceInstanceStream.rewind();
      doStreamTest(learner, sourceInstanceStream, filters, args);
    }
  }
  else {
    doStreamTest(learner, sourceInstanceStream, filters, args);
  }
}
