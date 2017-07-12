/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
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
  void Set(uint64 start, uint64 limit, const V& value);

  // Finds the value associated with the interval containing key. Returns false
  // if no interval contains key.
  bool Lookup(uint64 key, V* value) const;

  // Find the interval containing key, or the next interval containing
  // something greater than key. Returns false if one is not found, otherwise
  // it sets start, limit, and value to the corresponding values from the
  // interval.
  bool FindNext(uint64 key, uint64* start, uint64* limit, V* value) const;

  // Remove all entries from the map.
  void Clear();

  // Clears everything in the interval map from [clear_start,
  // clear_limit). This may cut off sections or entire intervals in
  // the map. This will invalidate iterators to intervals which have a
  // start value residing in [clear_start, clear_limit).
  void ClearInterval(uint64 clear_start, uint64 clear_limit);

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
  bool Decrement(ConstMapIter* iter) const;
  bool Decrement(MapIter* iter);

  // Removes everything in the interval map from [remove_start,
  // remove_limit). This may cut off sections or entire intervals in
  // the map. This will invalidate iterators to intervals which have a
  // start value residing in [remove_start, remove_limit).
  void RemoveInterval(uint64 remove_start, uint64 remove_limit);

  void Insert(uint64 start, uint64 limit, const V& value);

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
void IntervalMap<V>::Set(uint64 start, uint64 limit, const V& value) {
  AssertValidInterval(start, limit);
  RemoveInterval(start, limit);
  Insert(start, limit, value);
}

template <class V>
bool IntervalMap<V>::Lookup(uint64 key, V* value) const {
  const auto contain = GetContainingInterval(key);
  if (contain == interval_start_.end()) {
    return false;
  }
  *value = contain->second.value;
  return true;
}

template <class V>
bool IntervalMap<V>::FindNext(uint64 key, uint64* start, uint64* limit,
                              V* value) const {
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
void IntervalMap<V>::ClearInterval(uint64 clear_start, uint64 clear_limit) {
  AssertValidInterval(clear_start, clear_limit);
  RemoveInterval(clear_start, clear_limit);
}

template <class V>
uint64 IntervalMap<V>::Size() const {
  return interval_start_.size();
}

template <class V>
void IntervalMap<V>::RemoveInterval(uint64 remove_start, uint64 remove_limit) {
  if (remove_start >= remove_limit) {
    return;
  }
  // It starts by splitting intervals that will only be partly cleared into two,
  // where one of those will be fully cleared and the other will not be cleared.
  SplitInterval(GetContainingInterval(remove_limit), remove_limit);
  SplitInterval(GetContainingInterval(remove_start), remove_start);

  auto remove_interval_start = interval_start_.lower_bound(remove_start);
  auto remove_interval_end = interval_start_.lower_bound(remove_limit);
  // Note that if there are no intervals to be cleared, then
  // clear_interval_start == clear_interval_end and the erase will be a noop.
  interval_start_.erase(remove_interval_start, remove_interval_end);
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
bool IntervalMap<V>::Decrement(ConstMapIter* iter) const {
  if ((*iter) == interval_start_.begin()) {
    return false;
  }
  --(*iter);
  return true;
}

template <class V>
bool IntervalMap<V>::Decrement(MapIter* iter) {
  if ((*iter) == interval_start_.begin()) {
    return false;
  }
  --(*iter);
  return true;
}

template <class V>
void IntervalMap<V>::Insert(uint64 start, uint64 limit, const V& value) {
  interval_start_.emplace(std::pair<uint64, Value>{start, {limit, value}});
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
