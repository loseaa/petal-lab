OPUS Miner



This is an open source implementation of the OPUS Miner algorithm
which applies OPUS search for Filtered Top-k Association Discovery
of Self-Sufficient Itemsets, as described in  the following papers.

Webb, G.I. & Vreeken, J. (2014) Efficient Discovery of the Most Interesting Associations. Transactions on Knowledge Discovery from Data. 8(3). ACM, pages 15:1-15:31.


Webb, G.I. (2011). Filtered-top-k Association Discovery.
WIREs Data Mining and Knowledge Discovery 1(3).
Wiley, pages 183-192.
Pre-Publication PDF: http://www.csse.monash.edu.au/~webb/Files/Webb11.pdf
Link to paper via Wiley Online Library: http://dx.doi.org/10.1002/widm.28


Webb, G.I. (2010). Self-Sufficient Itemsets: An Approach to Screening
Potentially Interesting Associations Between Items.
Transactions on Knowledge Discovery from Data 4. ACM, pages 3:1-3:20.
Link to paper via ACM Digital Library: http://www.csse.monash.edu.au/~webb/redirects/Webb10.html


Webb, G.I. (2008). Layered Critical Values: A Powerful Direct-Adjustment
Approach to Discovering Significant Patterns.
Machine Learning 71(2-3). Netherlands: Springer, pages 307-323 [Technical Note].
Link to paper via Springerlink: http://dx.doi.org/10.1007/s10994-008-5046-x


Webb, G.I. (2007). Discovering Significant Patterns.
Machine Learning 68(1). Netherlands: Springer, pages 1-33.
Link to paper via Springerlink: http://dx.doi.org/10.1007/s10994-007-5006-x


Copyright (C) 2012 Geoffrey I Webb


This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.


This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Please report any bugs to Geoff Webb geoff.webb@monash.edu.


USAGE
=====

In its basic mode of operation, Opus Miner is invoked with two arguments. 
The first is the name of the input file and the second is the name of the
output file.

The input file should contain a sequence of transactions, one per line.
Each transaction comprises a list of comma, space and/or tab separated items.
Each items is a sequence of printable characters other than commas, spaces
or tabs.  For examples of files in this format see the files at
http://fimi.ua.ac.be/data/.

The output contains some simple summary statistics plus a list of the
top-k productive non-redundant itemsets on the measure of interest,
with those that are not independently productive separated from the rest.


OPTIONS
=======

The following options are supported.
-c: Each output itemset is followed by its closure.
-f: Supress filtering out itemsets that are not independently productive.
-k<i>: Set k to the integer value <i>.  By default it is 100.
-l: Set the measure of interest to lift.  By default it is leverage.
-r: Allow redundant itemsets.
