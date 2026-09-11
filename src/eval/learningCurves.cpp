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
#include "learningCurves.h"
#include "StoredInstanceStream.h"
#include "StoredIndirectInstanceStream.h"
#include "IndirectInstanceSubstream.h"
#include "utils.h"
#include "globals.h"
#include "crosstab.h"
#include "incrementalLearner.h"
#include "resultsCollector.h"

#include <assert.h>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

/**
 * Status of the Matthews correlation coefficient for one (sample size, trial).
 *
 * MCC is only defined when both classes are represented in the test set. Two
 * degenerate cases are distinguished because they print differently: a trial
 * whose predictions all land in one class is reported as a bare `0`, and that
 * is not the same thing as a coefficient that happens to round to 0.000000.
 */
enum MCCStatus {
  MCC_UNDEFINED,   ///< a class has no test instances; print nothing
  MCC_DEGENERATE,  ///< every prediction is one class; print `0`
  MCC_OK
};

/// @param tp/fp/tn/fn confusion counts, with class 0 treated as "positive".
MCCStatus mccStatus(double tp, double fp, double tn, double fn, double& value) {
  if (tp + fn == 0 || tn + fp == 0) return MCC_UNDEFINED;   // no test instances for one class
  if (tp + fp == 0 || tn + fn == 0) {                       // no predictions for one class
    value = 0.0;
    return MCC_DEGENERATE;
  }
  value = (tp * tn - fp * fn) / sqrt((tp + fp) * (tp + fn) * (tn + fp) * (tn + fn));
  return MCC_OK;
}

/**
 * Print one matrix of per-trial values, indexed [sample size][trial].
 *
 * @param csv true for `1.0,2.0` per line, false for the MATLAB-style
 *            `[a, b]; [c, d];` used by the default (non-CSV) output.
 */
void printTrials(FILE* f, const std::vector<std::vector<double> >& rows, bool csv) {
  for (size_t j = 0; j < rows.size(); ++j) {
    if (csv) {
      for (size_t k = 0; k < rows[j].size(); ++k) {
        if (k) fputc(',', f);
        fprintf(f, PETAL_FLOAT_FMT, rows[j][k]);
      }
      fputc('\n', f);
    }
    else {
      fputs(j ? "; [" : "[", f);
      for (size_t k = 0; k < rows[j].size(); ++k) {
        if (k) fputs(", ", f);
        fprintf(f, PETAL_FLOAT_FMT, rows[j][k]);
      }
      fputc(']', f);
    }
  }
  if (!csv) fputs("];\n", f);
}

/**
 * Print the MCC matrix for one learner.
 *
 * The two formats disagree on undefined trials: CSV reserves a field for every
 * trial (so an undefined one becomes an empty field), while the bracketed form
 * leaves it out entirely. Both behaviours are preserved as-is.
 *
 * @tparam T InstanceCount for the count-based MCC, double for the proportional
 *           (soft) variant; the formula is the same for both.
 */
template <typename T>
void printMccTrials(FILE* f, const std::vector<std::vector<T> >& tp,
                    const std::vector<std::vector<T> >& fp,
                    const std::vector<std::vector<T> >& tn,
                    const std::vector<std::vector<T> >& fn, bool csv) {
  for (size_t j = 0; j < tp.size(); ++j) {
    bool comma = false;  // non-CSV only: a value has already been printed
    if (!csv) fputs(j ? "; [" : "[", f);

    for (size_t k = 0; k < tp[j].size(); ++k) {
      if (csv && k) fputc(',', f);

      double value = 0.0;
      const MCCStatus status = mccStatus(static_cast<double>(tp[j][k]),
                                         static_cast<double>(fp[j][k]),
                                         static_cast<double>(tn[j][k]),
                                         static_cast<double>(fn[j][k]), value);
      if (status == MCC_UNDEFINED) continue;

      if (!csv) {
        if (comma) fputs(", ", f);
        else comma = true;
      }
      if (status == MCC_DEGENERATE) fputc('0', f);
      else fprintf(f, PETAL_FLOAT_FMT, value);
    }

    if (csv) fputc('\n', f);
    else fputc(']', f);
  }
  if (!csv) fputs("];\n", f);
}

}  // namespace

// get settings from command line arguments
void LearningCurveArgs::getArgs(char*const*& argv, char*const* end) {
  while (argv != end) {
    if (*argv[0] != '+') {
      break;
    }
    else if (streq(argv[0]+1, "csv", false)) {
      csvFormat_ = true;
    }
    else if (argv[0][1] == 'e') {
      getUIntFromStr(argv[0]+2, endingPoint_, "e");
    }
    else if (argv[0][1] == 'h') {
      getUIntFromStr(argv[0]+2, testSetSize_, "h");
    }
    else if (argv[0][1] == 'n') {
      logProgression_ = false;
      getUIntFromStr(argv[0]+2, noOfPoints_, "n");
    }
    else if (argv[0][1] == 's') {
      getUIntFromStr(argv[0]+2, startingPoint_, "s");
    }
    else if (argv[0][1] == 't') {
      getUIntFromStr(argv[0]+2, noOfTrials_, "t");
    }
    else {
      break;
    }

    ++argv;
  }
}


void genLearningCurves(std::vector<learner*> theLearners, InstanceStream &instStream, FilterSet &filters, LearningCurveArgs* args) {
  IndirectInstanceSubstream substream;    // the first n instances in the instanceOrder
  const InstanceCount testSetSize = args->testSetSize_;
  const unsigned int noOfTrials = args->noOfTrials_;
  const InstanceCount minSampleSize = args->startingPoint_;
  const unsigned int noClasses = instStream.getNoClasses();
  std::vector<InstanceCount> sampleSizes;
  InstanceCount step;
  std::vector<double> classDist(noClasses);
  FILE* csvf = stdout;  ///< redundant, but retained in order to assist in converting to having separate output files if desired at a later date

  StoredInstanceStream store;

  store.setSource(*filters.apply(&instStream));

  StoredIndirectInstanceStream instanceOrder(store);

  if (testSetSize > store.size()) error("Cannot take %" ICFMT " test examples from %" ICFMT " examples", testSetSize, store.size());

  const InstanceCount availableForTraining =  min(store.size()-testSetSize, args->endingPoint_);

  unsigned int noOfSampleSizes = 0;

  // print the data set name and sample sizes
  if (args->csvFormat_) {
    fprintf(csvf, "\n>>> begin learning curves >>>\ndatasetName:,%s\ntrainsizes:", instStream.getName());
  }
  else {
    printf("\n>>> begin learning curves >>>\ndatasetName = %s;\ntrainsize = [", instStream.getName());
  }

  if (args->logProgression_) {
    for (InstanceCount sampleSize = minSampleSize; sampleSize <= availableForTraining; sampleSize *= 2) {
      if (args->csvFormat_) fprintf(csvf,",%d", sampleSize);
      else printf(" %d", sampleSize);
      sampleSizes.push_back(sampleSize);
      ++noOfSampleSizes;
    }
  }
  else {
    noOfSampleSizes = args->noOfPoints_;
    step = (availableForTraining-minSampleSize)/(noOfSampleSizes-1);
    for (InstanceCount sampleSize = minSampleSize; sampleSize <= availableForTraining; sampleSize += step) {
      if (args->csvFormat_) fprintf(csvf,",%d", sampleSize);
      else printf(" %d", sampleSize);
      sampleSizes.push_back(sampleSize);
    }
  }
  if (!args->csvFormat_) puts("];");

  // Every measure is indexed [learner][sample size][trial], so that the same
  // index triple picks one point out of any curve below.
  //
  // tp/fp/tn/fn are only meaningful for two-class problems: class 0 counts as
  // "positive". The p-prefixed vectors are their "proportional" counterparts,
  // accumulating predicted probabilities instead of hard decisions.
  std::vector<std::vector<std::vector<double> > > rmse(theLearners.size());
  std::vector<std::vector<std::vector<double> > > logLoss(theLearners.size());
  std::vector<std::vector<std::vector<double> > > zoLoss(theLearners.size());      // 0-1 loss
  std::vector<std::vector<std::vector<InstanceCount> > > tp(theLearners.size());   // true positives
  std::vector<std::vector<std::vector<InstanceCount> > > fp(theLearners.size());   // false positives
  std::vector<std::vector<std::vector<InstanceCount> > > tn(theLearners.size());   // true negatives
  std::vector<std::vector<std::vector<InstanceCount> > > fn(theLearners.size());   // false negatives
  std::vector<std::vector<std::vector<double> > > ptp(theLearners.size());         // proportional tp
  std::vector<std::vector<std::vector<double> > > pfp(theLearners.size());         // proportional fp
  std::vector<std::vector<std::vector<double> > > ptn(theLearners.size());         // proportional tn
  std::vector<std::vector<std::vector<double> > > pfn(theLearners.size());         // proportional fn

  for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
    theLearners[learner]->testCapabilities(instanceOrder); //after filters    
    zoLoss[learner].resize(noOfSampleSizes);
    tp[learner].resize(noOfSampleSizes);
    fp[learner].resize(noOfSampleSizes);
    tn[learner].resize(noOfSampleSizes);
    fn[learner].resize(noOfSampleSizes);
    ptp[learner].resize(noOfSampleSizes);
    pfp[learner].resize(noOfSampleSizes);
    ptn[learner].resize(noOfSampleSizes);
    pfn[learner].resize(noOfSampleSizes);
    rmse[learner].resize(noOfSampleSizes);
    logLoss[learner].resize(noOfSampleSizes);
  }

  // One pass over the trials: each trial reshuffles the instance order and then
  // walks up the sample sizes from that same order, so consecutive points of a
  // curve are nested training sets rather than independent samples.
  for (unsigned int trial = 0; trial < noOfTrials; trial++) {
    int ssIndex = 0;
    instanceOrder.shuffle();

    for (InstanceCount sampleSize = minSampleSize; sampleSize <= availableForTraining; sampleSize = (args->logProgression_ ? (2*sampleSize) : (sampleSize+step))) {
      instanceOrder.setIndirectInstanceSubstream(substream, 0, sampleSize);


      // for each learner train on each successive sample size and store the rmse
      for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
        theLearners[learner]->train(substream);

        // test on the last testSetSize instances
        instanceOrder.goTo(store.size()-testSetSize+1);

        double squaredError = 0.0;
        InstanceCount thisZOLoss = 0;
        InstanceCount thisTP = 0;
        InstanceCount thisFP = 0;
        InstanceCount thisTN = 0;
        InstanceCount thisFN = 0;
        double thisPTP = 0.0;
        double thisPFP = 0.0;
        double thisPTN = 0.0;
        double thisPFN = 0.0;
        double thisLogLoss = 0.0;

        while (!instanceOrder.isAtEnd()) {
          theLearners[learner]->classify(*instanceOrder.current(), classDist);

          const CatValue trueClass = instanceOrder.current()->getClass();
          const CatValue predictedClass = indexOfMaxVal(classDist);

          const double error = 1.0-classDist[trueClass];
          squaredError += error * error;
          thisLogLoss += log2(classDist[trueClass]);

          if (trueClass != predictedClass) ++thisZOLoss;

          if (trueClass == 0) {
            if (predictedClass == 0) ++thisTP;
            else ++thisFN;

            thisPTP += classDist[0];
            thisPFN += classDist[1];
          }
          else {
            if (predictedClass == 0) ++thisFP;
            else ++thisTN;

            thisPTN += classDist[1];
            thisPFP += classDist[0];
          }

          instanceOrder.advance();
        }

        zoLoss[learner][ssIndex].push_back(thisZOLoss/static_cast<double>(testSetSize));
        rmse[learner][ssIndex].push_back(sqrt(squaredError/testSetSize));
        logLoss[learner][ssIndex].push_back(-thisLogLoss/testSetSize);
        tp[learner][ssIndex].push_back(thisTP);
        tn[learner][ssIndex].push_back(thisTN);
        fp[learner][ssIndex].push_back(thisFP);
        fn[learner][ssIndex].push_back(thisFN);
        ptp[learner][ssIndex].push_back(thisPTP);
        ptn[learner][ssIndex].push_back(thisPTN);
        pfp[learner][ssIndex].push_back(thisPFP);
        pfn[learner][ssIndex].push_back(thisPFN);
      }

      ++ssIndex;
    }
  }

  // Structured results for the web front-end: one record per
  // (learner, sample size, trial), which is exactly what a learning curve plots.
  results().setMode("learning-curves");
  for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
    const std::string learnerName = *theLearners[learner]->getName();
    for (unsigned int j = 0; j < noOfSampleSizes && j < sampleSizes.size(); j++) {
      for (unsigned int k = 0; k < zoLoss[learner][j].size(); ++k) {
        results().addCurvePoint(learnerName, static_cast<unsigned int>(sampleSizes[j]),
                                k, zoLoss[learner][j][k],
                                rmse[learner][j][k], logLoss[learner][j][k]);
      }
    }
  }

  // output the learning curves
  if (args->csvFormat_) {
    // CSV groups everything for one learner together, so that each block can be
    // pasted straight into a spreadsheet.
    for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
      fprintf(csvf, "\nLearner:,");
      print_(csvf, *theLearners[learner]->getName());

      fprintf(csvf,"\n\n=== Zero-One Loss ===\n");
      printTrials(csvf, zoLoss[learner], true);

      fprintf(csvf, "\n=== RMSE ===\n");
      printTrials(csvf, rmse[learner], true);

      fprintf(csvf, "\n=== Log Loss ===\n");
      printTrials(csvf, logLoss[learner], true);

      if (store.getNoClasses() == 2) {
        fprintf(csvf, "\n=== Matthews Correlation Coefficient ===\n");
        printMccTrials(csvf, tp[learner], fp[learner], tn[learner], fn[learner], true);

        fprintf(csvf, "\n=== Proportional Matthews Correlation Coefficient ===\n");
        printMccTrials(csvf, ptp[learner], pfp[learner], ptn[learner], pfn[learner], true);
      }
    }
  }
  else {
    // The default format groups by measure, so the same curve for several
    // learners can be read off one line.
    printf("=== Zero-One Loss ===\n");
    for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
      print_(stdout, *theLearners[learner]->getName());
      printf(" = [");
      printTrials(stdout, zoLoss[learner], false);
    }

    printf("\n=== RMSE ===\n");
    for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
      print_(stdout, *theLearners[learner]->getName());
      printf(" = [");
      printTrials(stdout, rmse[learner], false);
    }

    printf("\n=== Log Loss ===\n");
    for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
      print_(stdout, *theLearners[learner]->getName());
      printf(" = [");
      printTrials(stdout, logLoss[learner], false);
    }

    if (store.getNoClasses() == 2) {
      printf("\n=== Matthews Correlation Coefficient ===\n");
      for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
        print_(stdout, *theLearners[learner]->getName());
        printf(" = [");
        printMccTrials(stdout, tp[learner], fp[learner], tn[learner], fn[learner], false);
      }

      printf("\n=== Proportional Matthews Correlation Coefficient ===\n");
      for (unsigned int learner = 0; learner < theLearners.size(); learner++) {
        print_(stdout, *theLearners[learner]->getName());
        printf(" = [");
        printMccTrials(stdout, ptp[learner], pfp[learner], ptn[learner], pfn[learner], false);
      }
    }
    printf("<<< end learning curves <<<\n");
  }
}
