/** \mainpage Petal: An open source system for classification learning from very large data
 * Copyright (C) 2012 Geoffrey I Webb
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Please report any bugs to Geoff Webb <geoff.webb@monash.edu>
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

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <new>

#include "resultsCollector.h"

#include "instanceFile.h"
#include "instanceStreamDiscretiser.h"
#include "instanceStreamDynamicDiscretiser.h"
#include "instanceStreamDynamicPIDDiscretiser.h"
#include "instanceStreamClassFilter.h"
#include "instanceStreamNormalisationFilter.h"
#include "derivedInstanceStream.h"
#include "learningCurves.h"
#include "learner.h"
#include "mtrand.h"
#include "utils.h"
#include "globals.h"
#include "FILEtype.h"
#include "learnerRegistry.h"
#include "ALGLIB_ap.h"
#include "FilterSet.h"
#include "syntheticInstanceStream.h"
#include "dataStatisticsAction.h"
#include "instanceStreamFeatureConstructor.h"

// Train & test utilities
#include "trainTest.h"
#include "streamTest.h"
#include "xVal.h"
#include "biasvariance.h"
#include "externXVal.h"
#include "emptyTest.h"
/** 
 * Type of experiment and evaluation method.
 */
enum experimentType {
	etNone, /**< Nothing is done by default. */
	etStreamTest, /**< Use a stream test (specified with -s) */
	etTrainTest, /**< Use training set for testing (specified with -t) */
  etXVal, /**< Cross-validation (-x10 by default). */
	etBiasVariance, /**< Bias/variance experiments. */
	etExternXVal, /**< Learn a external classifier (e.g. libsvm) with petal
	 * folds. */
	etLearningCurves, /**< Do a learning curves experiment */
  etDataStats, /**< Collect data statistics */
  etEmpty /**< an empty entry to test a function*/
};

/**
 * Parse the command line, build the requested experiment and run it.
 *
 * Besides the single-letter options handled in the switch below, two long
 * options are accepted: @c --json=FILE writes the structured results gathered
 * during the run (see resultsCollector.h), and @c --dump-predictions adds the
 * per-instance predictions that ROC/PR curves need — off by default because it
 * costs O(instances * classes).
 *
 * @param argv Options for the experiment
 * @param argc Number of options
 * @return An integer 0 upon exit success
 */
int main(int argc, char* const argv[]) {
	MTRand rand;
	char* testfilename = NULL;
	char* metafilename = NULL;
	experimentType et = etNone;
	char* expArgs = NULL;
	std::vector<learner*> theLearners;
	char* const * eXValArgv = NULL;
	int eXValArgc = 0;
	char* const * argvEnd = argv + argc;
	FilterSet filters;
	LearningCurveArgs lcArgs;
	TrainTestArgs ttArgs;
	StreamTestArgs stArgs;
  DataStatisticsActionArgs dsArgs;
  InstanceStream* instanceStream = NULL;
  InstanceStream* testStream = NULL;
  const char* jsonOutFile = NULL;  ///< non-NULL once --json=<file> is seen

#ifdef _MSC_VER
#ifdef _DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
#endif

	// First parse the command line arguments
	try {
		printf("======================\n"
				"Petal: the system for learning from big data\nVersion 0.2\n");
		for (int i = 0; i < argc; i++) {
			printf("%s ", argv[i]);
		}
		putchar('\n');
		putchar('\n');

		if (argc < 3) {
			error("Usage: %s <metafile> <trainingfile> [-p<posClassName>]"
					" [<test method args>] -l<learner> [<learner args>]"
					" [--json=<file>] [--dump-predictions]",
					argv[0]);
		}

    if (streq(argv[1], "-syn")) {
      // a synthetic data stream test
      argv += 2; // skip the program name, and this argument
      instanceStream = new SyntheticInstanceStream(argv, argvEnd);
    }
    else {
      // standard input from a data file
		  metafilename = argv[1];
      
      instanceStream = new InstanceFile(argv[1], argv[2]);

		  argv += 3; // skip the program name, the meta file name and the data file name
    }

    ArgParensCheck parensCheck;

    while (argv != argvEnd) {
      if (parensCheck.check(argv)) {
        ++argv;
      }
      else {
        if (**argv != '-') {
          error("Argument '%s' requires '-'", *argv);
        }

        char *p = argv[0] + 1;

        switch (*p) {
        case 'b':
          // use a bias-variance experiment
		  if (et != etNone) error("Only one action can be specified");
		  et = etBiasVariance;
          expArgs = p + 1;
          ++argv;
          break;
        case 'c':
          // learning curves
          if (et != etNone) error("Only one action can be specified");
          et = etLearningCurves;
          lcArgs.getArgs(++argv, argvEnd);
          break;
        case 'd':
          // discretise
          if (streq(p+1, "dynamic")) {
            filters.push_back(new InstanceStreamDynamicDiscretiser(++argv, argvEnd));
          }
          else if (streq(p+1, "dynamicPID")) {
            filters.push_back(new InstanceStreamDynamicPIDDiscretiser(++argv, argvEnd));
          }
          else {
            filters.push_back(new InstanceStreamDiscretiser(p + 1, ++argv, argvEnd));
          }
          break;
        case 'e':
          // use an external cross validation experiment
          if (et != etNone) error("Only one action can be specified");
          et = etExternXVal;

          expArgs = p + 1;

          // all arguments are collected and passed to the xval
          eXValArgv = ++argv;
          eXValArgc = argvEnd - argv;
          argv = argvEnd;
          break;
        case 'f':
          // feature construction
          filters.push_back(new InstanceStreamFeatureConstructor(++argv, argvEnd));
          break;
        case 'l':
          // specify the learner

          // create the learner
          theLearners.push_back(createLearner(p + 1, ++argv, argvEnd));

          if (theLearners.back() == NULL) {
            std::vector<std::string> learners;
            
            errorMsg("Learner %s is not supported.\nAvailable learners are:", p + 1);
            getLearnerRegistry().getLearnerList(learners);

            for (std::vector<std::string>::const_iterator it = learners.begin(); it != learners.end(); it++) {
              errorMsg("- %s", it->c_str());
            }
            exit(0);
          }
          break;
        case 'n':
          filters.push_back(new InstanceStreamNormalisationFilter(++argv, argvEnd));
          break;
        case 'p':
          // filter the classes into binary classification
          // this cannot be added to filters because it changes the number of classes
          instanceStream = new InstanceStreamClassFilter(instanceStream, p + 1, ++argv, argvEnd);
          break;
        case 's':
          // use a stream test experiment
		  if (et != etNone) error("Only one action can be specified");
		  et = etStreamTest;
          stArgs.getArgs(++argv, argvEnd);
          break;
        case 't':
          // use a trainingfile-testfile experiment
          // the testfile name must follow the t
		  if (et != etNone) error("Only one action can be specified");
		  et = etTrainTest;
          testfilename = p + 1;
          testStream = new InstanceFile(metafilename, testfilename);

          ttArgs.getArgs(++argv, argvEnd);
          break;
        case 'v':
          // set the verbosity level - the default is 1
          getUIntFromStr(p + 1, verbosity, "verbosity");
          ++argv;
          break;
        case 'x':
          // use a cross validation experiment
		  if (et != etNone) error("Only one action can be specified");
		  et = etXVal;
          expArgs = p + 1;
          ++argv;
          break;
        case 'z':
          // get data statistics
		  if (et != etNone) error("Only one action can be specified");
		  et = etDataStats;
          dsArgs.getArgs(++argv, argvEnd);
          break;
		case 'm':
			if(et != etNone) error("Only one action can be specified");
			et=etEmpty;
			++argv;
			break;
        case '-':
          // Long options. The switch dispatches on one character, so these are
          // handled here; p points just past the first '-', hence p + 1 is the
          // text after "--".
          if (strncmp(p + 1, "json=", 5) == 0) {
            jsonOutFile = p + 6;  // the file name, just past "--json="
          }
          else if (streq(p + 1, "dump-predictions")) {
            // Per-instance output is O(instances * classes), hence opt-in.
            results().setPredictionsEnabled(true);
          }
          else {
            error("Unrecognised argument '%s'", *argv);
          }
          ++argv;
          break;
        default:
          error("-%c flag is not supported", *p);
        }
      }
    }

    if (!parensCheck.balanced()) {
      error("Parentheses do not balance.");
    }

    if (et == etExternXVal) {
      externXVal(instanceStream, filters, expArgs, eXValArgv, eXValArgc);
    }
    else {
      if (theLearners.empty() && et != etDataStats) {
        error("No learner specified");
      }

      // Record the data set name so the front-end can label a run, and so that
      // several result files can be compared side by side later.
      if (instanceStream != NULL) {
        results().setDataset(instanceStream->getName());
      }

      // perform the experiment
      switch (et) {
      case etStreamTest:
        if (theLearners.size() > 1)
          error("Stream test only accepts a single learner");

        streamTest(theLearners[0], *instanceStream, filters, stArgs);
        break;
      case etTrainTest:
        if (theLearners.size() > 1)
          error("Train/test only accepts a single learner");

        trainTest(theLearners[0], *instanceStream, *testStream, filters, ttArgs);
        break;
      case etXVal:
        if (theLearners.size() > 1)
          error("Cross validation only accepts a single learner");

        xVal(theLearners[0], *instanceStream, filters, expArgs);
        break;
      case etBiasVariance:
        if (theLearners.size() > 1)
          error("Bias Variance only accepts a single learner");

        biasVariance(theLearners[0], *instanceStream, filters, expArgs);
        break;
      case etLearningCurves:
        genLearningCurves(theLearners, *instanceStream, filters, &lcArgs);
        break;
      case etDataStats:
        dataStatisticsAction(*instanceStream, filters, dsArgs);
        break;
	  case etEmpty:
		  emptyTest();
		  break;

      default:
        error("No action specified");
        break;
      }

      for (std::vector<learner*>::iterator it = theLearners.begin();
        it != theLearners.end(); it++) {
          delete *it;
      }
    }
        } catch (std::bad_alloc) {
          error("Out of memory");
        } catch (alglib::ap_error err) {
          error(err.msg.c_str());
        }

        if (verbosity >= 1)
          summariseUsage();

        // Write while the streams are still alive: the collector may still read
        // class names and counts off them.
        if (jsonOutFile != NULL) {
          results().write(jsonOutFile);
        }

        if (instanceStream != NULL) delete instanceStream;
        if (testStream != NULL) delete testStream;

        return 0;
}

