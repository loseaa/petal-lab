/* Open source system for classification learning from very large data
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
extern unsigned int verbosity;

/**
 * The printf conversion used for every floating-point value Petal writes out.
 *
 * Petal used to mix %f, %.2f, %.3f, %0.5f, %.10g and %.17g from one report to
 * the next, so the same metric could come out with different numbers of
 * decimals depending on which mode produced it. Every decimal now goes through
 * this one macro: four decimal places, fixed notation, everywhere.
 *
 * Use it by concatenation next to the surrounding literal, e.g.
 *   printf("0-1 loss = " PETAL_FLOAT_FMT "\n", loss);
 *
 * Change it here and the whole system follows — do not hand-write a precision
 * at a call site.
 */
#define PETAL_FLOAT_FMT "%.4f"
