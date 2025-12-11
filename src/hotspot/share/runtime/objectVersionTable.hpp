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
#include "oops/access.hpp"

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

  bool operator!=(const Timestamp& other) const { return !(*this == other); }
  bool operator<=(const Timestamp& other) const { return *this < other || *this == other; }
  bool operator>(const Timestamp& other) const { return other < *this; }
  bool operator>=(const Timestamp& other) const { return other < *this || *this == other; }

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

class ObjectVersionPayloadKey : AllStatic {
  static unsigned get_hash(const ObjectVersionPayload& entry) { return (int)cast_from_oop<uint64_t>(entry); }
  static bool equals(const ObjectVersionPayload& lhs, const ObjectVersionPayload& rhs) { 
    return lhs == rhs;
  }
};

using LocalObjectVersionStoreTable = ResizeableHashTable<ObjectNumber, ObjectVersionPayload,
                                                        AnyObj::C_HEAP, mtInternal,
                                                        ObjectNumberKey::get_hash,
                                                        ObjectNumberKey::equals>;

using LocalObjectVersionStoreReverseTable = ResizeableHashTable<ObjectVersionPayload, ObjectNumber,
                                                                AnyObj::C_HEAP, mtInternal,
                                                                ObjectVersionPayloadKey::get_hash,
                                                                ObjectVersionPayloadKey::equals>;
class LocalObjectVersionStore {
  friend class GlobalVersionHistory;

  Timestamp _timestamp;

  LocalObjectVersionStoreTable _table;
  LocalObjectVersionStoreReverseTable _reverse_table;

  template <typename ITER>
  void unlink_forward_map(ITER* iter) {
    _table.unlink(iter);
  }

  template <typename ITER>
  void unlink_reverse_map(ITER* iter) {
    _reverse_table.unlink(iter);
  }

  template <typename Function>
  void iterate_all(Function iter) const {
    _table.iterate_all(iter);
  }

  unsigned number_of_entries() const {
    return _table.number_of_entries();
  }

public:
  LocalObjectVersionStore();

  Timestamp get_timestamp() const {
    return _timestamp;
  }

  void set_timestamp(Timestamp timestamp) {
    _timestamp = timestamp;
  }

  bool put(ObjectNumber object_number, ObjectVersionPayload object) {
    return _table.put(object_number, object)
            && _reverse_table.put(object, object_number);
  }

  ObjectVersionPayload* get_object_for_object_number(ObjectNumber object_number) const {
    return _table.get(object_number);
  }

  ObjectNumber* get_object_number_for_object(ObjectVersionPayload object) const {
    return _reverse_table.get(object);
  }
};

struct ObjectVersion {
  Timestamp timestamp;
  ObjectVersionPayload version_payload;
};

using ObjectVersionHistory = GrowableArrayCHeap<ObjectVersion, mtInternal>;

using GlobalObjectVersionStore = ResizeableHashTable<ObjectNumber, ObjectVersionHistory*,
                                                    AnyObj::C_HEAP, mtInternal,
                                                    ObjectNumberKey::get_hash,
                                                    ObjectNumberKey::equals>;

class GlobalVersionHistory: public AllStatic {
  static Timestamp _global_ts;
  static ObjectNumber _next_object_number;
  static GlobalObjectVersionStore _object_version_store;

public:
  static void init() {}

  static ObjectNumber next_object_number() {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    _object_version_store.put(_next_object_number, new ObjectVersionHistory());
    return _next_object_number++;
  }

  static void push_object_versions(LocalObjectVersionStore* local_object_store) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);  

    Timestamp local_timestamp = local_object_store->get_timestamp();
    Timestamp next_timestamp = ++_global_ts;

    struct {
      Timestamp next_timestamp;
      Timestamp local_timestamp;

      bool do_entry(ObjectNumber& object_number, ObjectVersionPayload& object_payload) {
        ObjectVersionHistory** history = _object_version_store.get(object_number);
        assert(history != nullptr, "object number has not been created yet");    

        if ((*history)->length() > 0) {
          // We might have local objects in staging that haven't been pushed back yet
          ObjectVersion version = (*history)->last();
          assert(version.timestamp <= local_timestamp, "conflict: newer object version already existing in global version store");
        }

        ObjectVersion object_version{next_timestamp, object_payload};
        (*history)->append(object_version);

        return true;
      }
    } function{next_timestamp, local_timestamp};
    local_object_store->unlink_forward_map(&function);
    // TODO: currently destroying only the forward map, for now that is okay as oop->object number can't be wrong without garbage collection

    assert(local_object_store->number_of_entries() == 0, "local version store is not empty");

    local_object_store->set_timestamp(next_timestamp);
  }

  static void pull_latest_or_error(LocalObjectVersionStore* local_object_store) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    Timestamp local_timestamp = local_object_store->get_timestamp();

    local_object_store->iterate_all([&](const ObjectNumber& object_number, const ObjectVersionPayload& object_payload){
      ObjectVersionHistory** history = _object_version_store.get(object_number);
      assert(history != nullptr, "object number has not been created yet"); 

      if ((*history)->length() > 0) {
        // We might have local objects in staging that haven't been pushed back yet
        ObjectVersion version = (*history)->last();
        assert(version.timestamp <= local_timestamp, "conflict: newer object version already existing in global version store");
      }

      return true;
    });

    local_object_store->set_timestamp(_global_ts);
  }

  static ObjectVersionPayload get_object_version_for_timestamp(ObjectNumber object_number, const Timestamp& timestamp) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionHistory** history = _object_version_store.get(object_number);
    assert(history != nullptr, "object number has not been created yet");  

    int i = (*history)->find_from_end_if([&](const ObjectVersion& e) {
      return e.timestamp <= timestamp;
    });

    ObjectVersion version = (*history)->at(i);
    return version.version_payload;
  }

  // An object number will have a slot for versions to be stored, but may not yet have had any objects versions allocated
  static bool has_object_been_versioned(ObjectNumber object_number) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionHistory** history = _object_version_store.get(object_number);
    assert(history != nullptr, "object number has not been created yet");  

    return (*history)->length() > 0;
  }
};

class RefreshLocalObjectVersionStore : public OopClosure {
  const LocalObjectVersionStore& _object_version_store;

public:
  RefreshLocalObjectVersionStore(const LocalObjectVersionStore& object_version_store):
    _object_version_store(object_version_store) {}

  void do_oop(oop* p) override {
    // find the object number associated with this oop
    // and update it to the correct oop based on the existing version number
    oop obj = RawAccess<>::oop_load(p);
    if (obj != nullptr) {
      ObjectNumber* object_number = _object_version_store.get_object_number_for_object(obj);
      if (object_number == nullptr) {
        return; // some junk we weren't tracking
      }

      if (GlobalVersionHistory::has_object_been_versioned(*object_number)) {
        RawAccess<>::oop_store(p, GlobalVersionHistory::get_object_version_for_timestamp(*object_number, _object_version_store.get_timestamp()));
      }
    }
  }

  void do_oop(narrowOop* o) override { assert(false, "no support"); }
}; 

// inherit from BasicOopIterateClosure do_oop to iterate over fields

#endif