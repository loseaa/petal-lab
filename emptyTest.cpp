#include "emptyTest.h"
#include <stdio.h>
#include <fstream>
#include <vector>
#include <iostream>
#include "utils.h"

using namespace std;


// This is to test why  the functioncalcAUCMultiClasses does not work on poker-hand. 
//It turns out that the unsigned int type is not wide enough.
// The type unsigned long int is the same as unsigned int.
//we change to unsigned long long.

void emptyTest()
{
		
	const unsigned int noClasses=3;

	ifstream proFile("D:\\Matlabprogram\\contact-lensesprobs.txt");
    
	ifstream classFile("D:/Matlabprogram/contact-lensesclass.txt");
	      
	vector<unsigned int>  trueClasses;
	vector<vector<double> > probs(noClasses);
	unsigned int trueClass;
	double prob;

	while (!classFile.eof() )
	{

		classFile>>trueClass;
		trueClasses.push_back(trueClass);
	}

	for(int j=0;j<trueClasses.size();j++)
	{
		for(int i=0;i<noClasses;i++)
		{

			proFile>>prob;
			probs[i].push_back(prob);

		}
	}


	double a=calcMultiAUC(probs, trueClasses);
	cout<<a<<endl;

}


