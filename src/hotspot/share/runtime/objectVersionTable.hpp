/*
 * Copyright (c) 1997, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Azul Systems, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_RUNTIME_OBJECTVERSIONTABLES_HPP
#define SHARE_RUNTIME_OBJECTVERSIONTABLES_HPP

#include "memory/allocation.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/resizableHashTable.hpp"

static const int INITIAL_VERSION_TABLE_SIZE = 1007;
static const int MAX_VERSION_TABLE_SIZE     = 0x3fffffff;

class Timestamp { 
  uint64_t _epoch; 
  uint64_t _counter;

public:
  bool operator==(const Timestamp& other) const {
    return _epoch == other._epoch && _counter == other._counter;
  }

  bool operator<(const Timestamp& other) const {
    return _epoch < other._epoch || (_epoch == other._epoch && _counter < other._counter);
  }

    // pre-increment
  Timestamp& operator++() {
    if (_counter == UINT64_MAX) {
      _counter = 0;
      ++_epoch;
    } else {
      ++_counter;
    }
    return *this;
  }

  // post-increment
  Timestamp operator++(int) {
    Timestamp old = *this;
    ++(*this);
    return old;
  }
};

using ObjectNumber = jlong;
using ObjectVersionPayload = oop;

class ObjectNumberKey : AllStatic {
  static unsigned get_hash(const ObjectNumber& entry) { return entry; }
  static bool equals(const ObjectNumber& lhs, const ObjectNumber& rhs) { 
    return lhs == rhs;
  }
};

struct ObjectVersion {
  Timestamp timestamp;
  ObjectVersionPayload version_payload;
};

using ObjectVersionHistory = GrowableArrayCHeap<ObjectVersion, mtInternal>;

using ObjectVersionStore = ResizeableHashTable<ObjectNumber, ObjectVersionHistory*,
                                               AnyObj::C_HEAP, mtInternal,
                                               ObjectNumberKey::get_hash,
                                               ObjectNumberKey::equals>;

class GlobalVersionHistory: public AllStatic {
  static Timestamp _global_ts;
  static ObjectNumber _next_object_number;
  static ObjectVersionStore _object_version_store;

public:
  static void init() {}

  static ObjectNumber next_object_number() {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    _object_version_store.put(_next_object_number, new ObjectVersionHistory());
    return _next_object_number++;
  }

  static Timestamp commit_object_version(ObjectNumber object_number, ObjectVersionPayload payload) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionHistory** history = _object_version_store.get(object_number);
    assert(history != nullptr, "object number has not been created yet");    

    ObjectVersion object_version{_global_ts++, payload};
    (*history)->append(object_version);
    return _global_ts;
  }

  static ObjectVersionPayload get_object_version_for_timestamp(ObjectNumber object_number, Timestamp timestamp) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionHistory** history = _object_version_store.get(object_number);
    assert(history != nullptr, "object number has not been created yet");  

    int i = (*history)->find_from_end_if([&](const ObjectVersion& e) {
      return e.timestamp == timestamp || e.timestamp < timestamp; // fix the rel operators
    });
    assert(i != -1, "object history does not contain an object for this timestamp");

    ObjectVersion version = (*history)->at(i);
    return version.version_payload;
  }
};

using LocalObjectVersionStore = ResizeableHashTable<ObjectNumber, ObjectVersionPayload,
                                                    AnyObj::C_HEAP, mtInternal,
                                                    ObjectNumberKey::get_hash,
                                                    ObjectNumberKey::equals>;

#endif