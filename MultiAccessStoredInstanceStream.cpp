/* Open source system for classification learning from very large data
** Class for an input stream that stores its instances and creates a vector of input streams each of which provides thread-safe access to the shared store.

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
#include "MultiAccessStoredInstanceStream.h"
#include <assert.h>

MultiAccessStoredInstanceStream::SharedStream::SharedStream() {
}
  
void MultiAccessStoredInstanceStream::SharedStream::reset(MetaData* metaData, std::vector<instance>* store)  {
  metaData_ = metaData;
  store_ = store; 
  rewind();
}

void MultiAccessStoredInstanceStream::SharedStream::rewind() {
  next_ = 0;
}

InstanceCount MultiAccessStoredInstanceStream::SharedStream::size() {
  return store_->size();
}

/// advance, discarding the next instance in the stream.  Return true iff successful.
bool MultiAccessStoredInstanceStream::SharedStream::advance() {
  ++next_;
  return next_ <= store_->size();
}

/// get a pointer to the current instance.
/// Requires that there be a current instance, so must either check isAtEnd or the the most recent advance was successful.
instance* MultiAccessStoredInstanceStream::SharedStream::current() {
  assert(next_ > 0 && next_ <= store_->size());
  return &((*store_)[next_-1]);
}

/// advance to the next instance in the stream. Return true iff successful. @param inst the instance record to receive the new instance. 
bool MultiAccessStoredInstanceStream::SharedStream::advance(instance &inst) {
  if (next_ >= store_->size()) return false;
  else {
    inst = (*store_)[next_];
    ++next_;
    return true;
  }
}


/// advance to the specified position in the stream.
void MultiAccessStoredInstanceStream::SharedStream::goTo(InstanceCount position) {
  next_ = position; // indexes start at 1, whereas next_ is indexed starting from 0
}


bool MultiAccessStoredInstanceStream::SharedStream::isAtEnd() const {
  return next_ >= store_->size();
}


MultiAccessStoredInstanceStream::MultiAccessStoredInstanceStream()
{
}

MultiAccessStoredInstanceStream::~MultiAccessStoredInstanceStream(void)
{
}

void MultiAccessStoredInstanceStream::initStream(SharedStream* stream) {
  stream->reset(metaData_, &store_);
}

