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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
** Please report any bugs to Geoff Webb <geoff.webb@monash.edu>
*/
#include "resultsCollector.h"

#include "jsonWriter.h"

#include <algorithm>
#include <stdio.h>

namespace {

/// Append @p s to @p v unless it is already present (learners repeat per fold).
void addUnique(std::vector<std::string>& v, const std::string& s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

/// Collect the learner names appearing in any record list, in first-seen order.
template <typename Record>
void collectLearners(const std::vector<Record>& records, std::vector<std::string>& out) {
  for (size_t i = 0; i < records.size(); ++i) addUnique(out, records[i].learner);
}

}  // namespace

ResultsCollector& ResultsCollector::instance() {
  static ResultsCollector theInstance;
  return theInstance;
}

ResultsCollector& results() {
  return ResultsCollector::instance();
}

void ResultsCollector::setDataset(const std::string& name) {
  dataset_ = name;
}

void ResultsCollector::setMode(const std::string& mode) {
  mode_ = mode;
}

void ResultsCollector::addMetric(const std::string& learner, const std::string& metric,
                                 int trial, int fold, double value) {
  MetricRecord rec;
  rec.learner = learner;
  rec.metric = metric;
  rec.trial = trial;
  rec.fold = fold;
  rec.value = value;
  metrics_.push_back(rec);
}

void ResultsCollector::addCurvePoint(const std::string& learner, unsigned int trainSize,
                                     unsigned int trial, double error, double rmse,
                                     double logloss) {
  CurveRecord rec;
  rec.learner = learner;
  rec.trainSize = trainSize;
  rec.trial = trial;
  rec.error = error;
  rec.rmse = rmse;
  rec.logloss = logloss;
  curves_.push_back(rec);
}

void ResultsCollector::addConfusion(const std::string& learner, unsigned int noClasses,
                                    const std::vector<unsigned int>& matrix) {
  ConfusionRecord rec;
  rec.learner = learner;
  rec.noClasses = noClasses;
  rec.matrix = matrix;
  confusions_.push_back(rec);
}

void ResultsCollector::addPredictions(const std::string& learner,
                                      const std::vector<std::vector<double> >& classProbs,
                                      const std::vector<unsigned int>& trueClasses) {
  if (!predictionsEnabled_) return;

  const size_t noClasses = classProbs.size();
  const size_t noInstances = trueClasses.size();

  PredictionRecord rec;
  rec.learner = learner;
  rec.trueClasses = trueClasses;
  rec.probs.resize(noInstances);
  for (size_t i = 0; i < noInstances; ++i) {
    rec.probs[i].resize(noClasses);
    for (size_t y = 0; y < noClasses; ++y) {
      // classProbs is [class][instance]; transpose to [instance][class].
      rec.probs[i][y] = (i < classProbs[y].size()) ? classProbs[y][i] : 0.0;
    }
  }
  predictions_.push_back(rec);
}

void ResultsCollector::setPredictionsEnabled(bool enabled) {
  predictionsEnabled_ = enabled;
}

void ResultsCollector::clear() {
  metrics_.clear();
  curves_.clear();
  confusions_.clear();
  predictions_.clear();
}

void ResultsCollector::write(const char* path) {
  FILE* f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "error: cannot open JSON results file \"%s\" for writing\n", path);
    return;
  }

  std::vector<std::string> learners;
  collectLearners(metrics_, learners);
  collectLearners(curves_, learners);
  collectLearners(confusions_, learners);
  collectLearners(predictions_, learners);

  JsonWriter jw(f);

  jw.beginObject();
  jw.field("schema", 1);
  jw.field("dataset", dataset_);
  jw.field("mode", mode_);

  jw.arrayField("learners");
  for (size_t i = 0; i < learners.size(); ++i) jw.value(learners[i].c_str());
  jw.endArray();

  jw.arrayField("metrics");
  for (size_t i = 0; i < metrics_.size(); ++i) {
    jw.beginObject();
    jw.field("learner", metrics_[i].learner);
    jw.field("metric", metrics_[i].metric);
    jw.field("trial", metrics_[i].trial);
    jw.field("fold", metrics_[i].fold);
    jw.field("value", metrics_[i].value);
    jw.endObject();
  }
  jw.endArray();

  jw.arrayField("curves");
  for (size_t i = 0; i < curves_.size(); ++i) {
    jw.beginObject();
    jw.field("learner", curves_[i].learner);
    jw.field("trainSize", curves_[i].trainSize);
    jw.field("trial", curves_[i].trial);
    jw.field("error", curves_[i].error);
    jw.field("rmse", curves_[i].rmse);
    jw.field("logloss", curves_[i].logloss);
    jw.endObject();
  }
  jw.endArray();

  jw.arrayField("confusion");
  for (size_t i = 0; i < confusions_.size(); ++i) {
    jw.beginObject();
    jw.field("learner", confusions_[i].learner);
    jw.field("noClasses", confusions_[i].noClasses);
    jw.arrayField("matrix");
    for (size_t k = 0; k < confusions_[i].matrix.size(); ++k) {
      jw.value(confusions_[i].matrix[k]);
    }
    jw.endArray();
    jw.endObject();
  }
  jw.endArray();

  jw.arrayField("predictions");
  for (size_t i = 0; i < predictions_.size(); ++i) {
    jw.beginObject();
    jw.field("learner", predictions_[i].learner);
    jw.arrayField("instances");
    for (size_t k = 0; k < predictions_[i].probs.size(); ++k) {
      jw.beginObject();
      jw.field("trueClass", predictions_[i].trueClasses[k]);
      jw.doubleArrayField("probs", predictions_[i].probs[k]);
      jw.endObject();
    }
    jw.endArray();
    jw.endObject();
  }
  jw.endArray();

  jw.endObject();
  fputc('\n', f);
  fclose(f);
}
