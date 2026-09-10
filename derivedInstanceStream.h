/* Open source system for classification learning from very large data
** Abstract class for an instance stream that is derived from another instance stream
**
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
#pragma once

#include "instanceStream.h"

/**
<!-- globalinfo-start -->
 * Class for an instance stream that is derived from a source instance stream, adding functionality.<p>
 * The filter can be used in either of two modes<br>
 * - directly as an instance stream, in which case it extracts the instances from the source stream and converts them to serve up to the consumer, or<br>
 * - the convert() method (depreciated) can directly translate an instance from the base stream into an instance for the output stream.<p>
 * The default methods pass all calls through to the source stream.<p>
 * The ClassSafeInstanceStreamFilter subclass is for stream filters that do not change the number of classes<p>
 *
 *
 * @author Geoff Webb (geoff.webb@monash.edu)
 */

class DerivedInstanceStream: public InstanceStream
{
public:
  DerivedInstanceStream();
  virtual ~DerivedInstanceStream(void);

  virtual void rewind();                                    ///< return to the first instance in the stream
  virtual bool advance();                                   ///< advance, discarding the next instance in the stream.  Return true iff successful.
  virtual bool advance(instance &inst);                     ///< advance to the next instance in the stream.  Return true iff successful. @param inst the instance record to receive the new instance. 
  virtual bool isAtEnd() const;                                   ///< true if we have advanced past the last instance
  virtual InstanceCount size();                             ///< the number of instances in the stream. This may require a pass through the stream to determine so should be used only if absolutely necessary.  The stream state is undefined after a call to size(), so a rewind shouldbe performed before the next advance.

  virtual void setSource(InstanceStream &source);           ///< set the source for the filter

protected:
  InstanceStream *source_; ///< the source instance stream
};

