/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Google Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PERFTOOLS_INTERVALMAP_H_
#define PERFTOOLS_INTERVALMAP_H_

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>

#include "int_compat.h"

namespace perftools {

template <class V>
class IntervalMap {
 public:
  IntervalMap();

  // Set [start, limit) to value. If this interval overlaps one currently in the
  // map, the overlapping section will be overwritten by the new interval.
  void Set(uint64 start, uint64 limit, const V &value);

  // Finds the value associated with the interval containing key. Returns false
  // if no interval contains key.
  bool Lookup(uint64 key, V *value) const;

  // Find the interval containing key, or the next interval containing
  // something greater than key. Returns false if one is not found, otherwise
  // it sets start, limit, and value to the corresponding values from the
  // interval.
  bool FindNext(uint64 key, uint64 *start, uint64 *limit, V *value) const;

  // Remove all entries from the map.
  void Clear();

  uint64 Size() const;

 private:
  struct Value {
    uint64 limit;
    V value;
  };

  using MapIter = typename std::map<uint64, Value>::iterator;
  using ConstMapIter = typename std::map<uint64, Value>::const_iterator;

  // For an interval to be valid, start must be strictly less than limit.
  void AssertValidInterval(uint64 start, uint64 limit) const;

  // Returns an iterator pointing to the interval containing the given key, or
  // end() if one was not found.
  ConstMapIter GetContainingInterval(uint64 point) const {
    auto bound = interval_start_.upper_bound(point);
    if (!Decrement(&bound)) {
      return interval_start_.end();
    }
    if (bound->second.limit <= point) {
      return interval_start_.end();
    }
    return bound;
  }

  MapIter GetContainingInterval(uint64 point) {
    auto bound = interval_start_.upper_bound(point);
    if (!Decrement(&bound)) {
      return interval_start_.end();
    }
    if (bound->second.limit <= point) {
      return interval_start_.end();
    }
    return bound;
  }

  // Decrements the provided iterator to interval_start_, or returns false if
  // iter == begin().
  bool Decrement(ConstMapIter *iter) const;
  bool Decrement(MapIter *iter);

  // Clears everything in the interval map from [clear_start, clear_limit). This
  // may cut off sections or entire intervals in the map. This will invalidate
  // iterators to intervals which have a start value resididing in [clear_start,
  // clear_limit)
  void ClearInterval(uint64 clear_start, uint64 clear_limit);

  void Insert(uint64 start, uint64 limit, const V &value);

  // Split an interval into two intervals, [iter.start, point) and
  // [point, iter.limit). If point is not within (iter.start, point) or iter is
  // end(), it is a noop.
  void SplitInterval(MapIter iter, uint64 point);

  // Map from the start of the interval to the limit of the interval and the
  // corresponding value.
  std::map<uint64, Value> interval_start_;
};

template <class V>
IntervalMap<V>::IntervalMap() {}

template <class V>
void IntervalMap<V>::Set(uint64 start, uint64 limit, const V &value) {
  AssertValidInterval(start, limit);
  ClearInterval(start, limit);
  Insert(start, limit, value);
}

template <class V>
bool IntervalMap<V>::Lookup(uint64 key, V *value) const {
  const auto contain = GetContainingInterval(key);
  if (contain == interval_start_.end()) {
    return false;
  }
  *value = contain->second.value;
  return true;
}

template <class V>
bool IntervalMap<V>::FindNext(uint64 key, uint64 *start, uint64 *limit,
                              V *value) const {
  auto iter = interval_start_.upper_bound(key);
  if (iter == interval_start_.end()) {
    return false;
  }
  *start = iter->first;
  *limit = iter->second.limit;
  *value = iter->second.value;
  return true;
}

template <class V>
void IntervalMap<V>::Clear() {
  interval_start_.clear();
}

template <class V>
uint64 IntervalMap<V>::Size() const {
  return interval_start_.size();
}

template <class V>
void IntervalMap<V>::ClearInterval(uint64 clear_start, uint64 clear_limit) {
  if (clear_start >= clear_limit) {
    return;
  }
  // It starts by splitting intervals that will only be partly cleared into two,
  // where one of those will be fully cleared and the other will not be cleared.
  SplitInterval(GetContainingInterval(clear_limit), clear_limit);
  SplitInterval(GetContainingInterval(clear_start), clear_start);

  auto clear_interval_start = interval_start_.lower_bound(clear_start);
  auto clear_interval_end = interval_start_.lower_bound(clear_limit);
  // Note that if there are no intervals to be cleared, then
  // clear_interval_start == clear_interval_end and the erase will be a noop.
  interval_start_.erase(clear_interval_start, clear_interval_end);
}

template <class V>
void IntervalMap<V>::SplitInterval(MapIter iter, uint64 point) {
  if (iter == interval_start_.end() || point <= iter->first ||
      point >= iter->second.limit) {
    return;
  }
  const auto larger_limit = iter->second.limit;
  iter->second.limit = point;
  Insert(point, larger_limit, iter->second.value);
}

template <class V>
bool IntervalMap<V>::Decrement(ConstMapIter *iter) const {
  if ((*iter) == interval_start_.begin()) {
    return false;
  }
  --(*iter);
  return true;
}

template <class V>
bool IntervalMap<V>::Decrement(MapIter *iter) {
  if ((*iter) == interval_start_.begin()) {
    return false;
  }
  --(*iter);
  return true;
}

template <class V>
void IntervalMap<V>::Insert(uint64 start, uint64 limit, const V &value) {
  interval_start_[start] = Value{limit, value};
}

template <class V>
void IntervalMap<V>::AssertValidInterval(uint64 start, uint64 limit) const {
  if (start >= limit) {
    std::cerr << "Invalid interval. Start may not be >= limit." << std::endl
              << "Start: " << start << std::endl
              << "Limit: " << limit << std::endl;
    abort();
  }
}

}  // namespace perftools

#endif  // PERFTOOLS_INTERVALMAP_H_
