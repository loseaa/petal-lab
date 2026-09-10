/* Open source system for classification learning from very large data
** Class for an input stream in which instances are stored in core
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
#include "StoredInstanceStream.h"
#include "mtrand.h"

#include <vector>


class MultiAccessStoredInstanceStream : public StoredInstanceStream
{
public:
  MultiAccessStoredInstanceStream();
  ~MultiAccessStoredInstanceStream(void);

  class SharedStream : public AddressableInstanceStream
  {
  public:
    SharedStream();
    ~SharedStream() {};

    void reset(MetaData* metaData, std::vector<instance>* store);

    void rewind();                                              ///< return to the first instance in the stream
    bool advance();                                             ///< advance, discarding the next instance in the stream.  Return true iff successful.
    bool advance(instance &inst);                               ///< advance to the next instance in the stream.  Return true iff successful. @param inst the instance record to receive the new instance. 
    bool isAtEnd() const;                                       ///< true if we have advanced past the last instance
    InstanceCount size();                                       ///< the number of instances in the stream. This may require a pass through the stream to determine so should be used only if absolutely necessary.  The stream state is undefined after a call to size(), so a rewind shouldbe performed before the next advance.

    instance* current();
    void goTo(InstanceCount position);

    std::vector<instance>* store_;
    unsigned int next_;                ///< index of the next instance to be retrieved by advance. Use index rather than iterator to avoid problems with the iterator becoming invalid
  };

  void initStream(SharedStream* stream);
};
