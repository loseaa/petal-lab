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
#ifndef RESULTSCOLLECTOR_H
#define RESULTSCOLLECTOR_H

#include <string>
#include <vector>

/**
 * A single (learner, metric, fold) observation.
 *
 * Results are kept in "tidy" long format: one row per observation, rather than
 * one block per learner. This is what lets the front-end regroup the same data
 * into a box plot, a bar chart or a table without re-parsing anything.
 */
struct MetricRecord {
  std::string learner;
  std::string metric;
  int trial;  ///< experiment index (0 unless repeating an experiment)
  int fold;   ///< cross-validation fold, or -1 for the pooled overall value
  double value;
};

/// One point of a learning curve: (learner, training-set size, trial).
struct CurveRecord {
  std::string learner;
  unsigned int trainSize;
  unsigned int trial;
  double error;    ///< 0-1 loss
  double rmse;
  double logloss;
};

/// A confusion matrix, flattened row-major as [true class][predicted class].
struct ConfusionRecord {
  std::string learner;
  unsigned int noClasses;
  std::vector<unsigned int> matrix;
};

/**
 * Per-instance predictions for one learner, used by the front-end to draw
 * ROC / PR curves — which cannot be produced from aggregate metrics alone.
 */
struct PredictionRecord {
  std::string learner;
  std::vector<unsigned int> trueClasses;
  std::vector<std::vector<double> > probs;  ///< [instance][class]
};

/**
 * Central sink for structured experiment results.
 *
 * Petal's evaluation modes each print their own human-readable text in
 * different shapes, which is fine for a terminal and impossible to plot.
 * A single shared collector decouples "producing results" from "rendering
 * them": evaluation code calls addMetric()/addCurvePoint() and never learns
 * anything about JSON or HTML.
 *
 * The instance is global for the same reason `verbosity` is: evaluation
 * functions are invoked from many places and threading an extra parameter
 * through all of them would touch every call site.
 */
class ResultsCollector {
public:
  static ResultsCollector& instance();

  void setDataset(const std::string& name);
  void setMode(const std::string& mode);

  /// Record one observation. Pass fold = -1 for an overall/pooled value.
  void addMetric(const std::string& learner, const std::string& metric,
                 int trial, int fold, double value);

  void addCurvePoint(const std::string& learner, unsigned int trainSize,
                     unsigned int trial, double error, double rmse, double logloss);

  /// @param matrix row-major [true][predicted], length noClasses * noClasses.
  void addConfusion(const std::string& learner, unsigned int noClasses,
                    const std::vector<unsigned int>& matrix);

  /**
   * Store per-instance predictions.
   *
   * @param classProbs [class][instance], the layout xVal already builds.
   *                   Transposed to [instance][class] on the way in, because
   *                   that is the natural per-observation shape for plotting.
   */
  void addPredictions(const std::string& learner,
                      const std::vector<std::vector<double> >& classProbs,
                      const std::vector<unsigned int>& trueClasses);

  /// Per-instance output is O(instances * classes); off unless asked for.
  void setPredictionsEnabled(bool enabled);
  bool predictionsEnabled() const { return predictionsEnabled_; }

  /**
   * Write everything collected so far to @p path as JSON.
   *
   * The layout is what the web front-end reads; "schema" is bumped whenever that
   * contract changes incompatibly. Failure to open the file is reported on
   * stderr and is not fatal — the experiment result is already on stdout.
   */
  void write(const char* path);

  /**
   * Drop all collected records.
   *
   * Only the observations go; the run-level labels (dataset, mode) and the
   * predictions switch survive, because they describe the process rather than
   * one experiment.
   */
  void clear();

private:
  ResultsCollector() : predictionsEnabled_(false) {}

  std::string dataset_;
  std::string mode_;
  bool predictionsEnabled_;

  std::vector<MetricRecord> metrics_;
  std::vector<CurveRecord> curves_;
  std::vector<ConfusionRecord> confusions_;
  std::vector<PredictionRecord> predictions_;
};

/// Shorthand accessor for the shared collector.
ResultsCollector& results();

#endif // RESULTSCOLLECTOR_H
