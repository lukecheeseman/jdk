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
#include "runtime/atomic.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/hashTable.hpp"
#include "utilities/resizableHashTable.hpp"

/*
 * The Global Object Version Table maps:
 *   ObjectNumber -> [ VersionNumber -> ObjectPayload ]
 *
*/

using ObjectNumber = jlong;
using VersionNumber = jlong; 
using VersionPayload = oop;

class VersionNumberKey : AllStatic {
  static unsigned get_hash(const VersionNumber& entry) { return primitive_hash(entry); }
  static bool equals(const VersionNumber& lhs, const VersionNumber& rhs) { 
    return lhs == rhs;
  }
};

using ObjectVersionHT = ResizeableHashTable<VersionNumber, VersionPayload,
                                            AnyObj::C_HEAP, mtInternal,
                                            VersionNumberKey::get_hash,
                                            VersionNumberKey::equals>;

static const int INITIAL_VERSION_TABLE_SIZE = 1007;
static const int MAX_VERSION_TABLE_SIZE     = 0x3fffffff;

class ObjectVersionTable : public CHeapObj<mtInternal> {
  ObjectVersionHT _table;
  VersionNumber _versionCounter; 
  
public:
  ObjectVersionTable() :
    _table(INITIAL_VERSION_TABLE_SIZE, MAX_VERSION_TABLE_SIZE),
    _versionCounter(0) {}

  VersionNumber create_version(VersionPayload payload) {
    _table.put(_versionCounter, payload);
    return _versionCounter++;
  }

  VersionNumber get_latest_version_number() {
    return _versionCounter - 1;
  }

  VersionPayload* get_payload(VersionNumber versionNumber) {
    return _table.get(versionNumber);
  }
};

class ObjectNumberKey : AllStatic {
  static unsigned get_hash(const ObjectNumber& entry) { return entry; }
  static bool equals(const ObjectNumber& lhs, const ObjectNumber& rhs) { 
    return lhs == rhs;
  }
};

using GlobalObjectVersionHT = ResizeableHashTable<ObjectNumber, ObjectVersionTable*,
                                                  AnyObj::C_HEAP, mtInternal,
                                                  ObjectNumberKey::get_hash,
                                                  ObjectNumberKey::equals>;

class GlobalVersionHistoryTable: public AllStatic {

  static GlobalObjectVersionHT _table;

public:
  static void init() {
    // Empty for now
  }

  static ObjectNumber create_object_number() {
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");

    ObjectNumber number = _table.number_of_entries() + 1;
    _table.put(number, new ObjectVersionTable());
    return number;
  }

  static VersionNumber create_object_version(ObjectNumber objectNumber, VersionPayload payload) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionTable** objectTable = _table.get(objectNumber);
    assert(objectTable != nullptr, "object number has not been created yet");    

    // ResourceMark rm;
    // printf("Version for payload: 0x%p\n", payload);

    return (*objectTable)->create_version(payload);
  }

  static VersionPayload* get_payload_for_object_version(ObjectNumber objectNumber, VersionNumber versionNumber) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionTable** objectTable = _table.get(objectNumber);
    assert(objectTable != nullptr, "object number has not been created yet");    

    return (*objectTable)->get_payload(versionNumber);
  }

  static VersionNumber get_latest_version_number_for_object_number(ObjectNumber objectNumber) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);

    ObjectVersionTable** objectTable = _table.get(objectNumber);
    assert(objectTable != nullptr, "object number has not been created yet");

    return (*objectTable)->get_latest_version_number();
  }

  static void destroy() {
    printf("Destroying history\n");
  }
};

class OopKey : public CHeapObj<mtInternal> {
  // WeakHandle _wh; -- the jvmti table uses weak handles, this will likely be important
  oop _obj; // temporarily hold obj while searching
 public:
  OopKey(oop obj);
  // OopKey(const OopKey& src);
  OopKey& operator=(const OopKey&) = delete;

  // oop object() const;
  // oop object_no_keepalive() const;
  // void release_weak_handle();

  static unsigned get_hash(const OopKey& entry) {
    assert(entry._obj != nullptr, "must lookup obj to hash");
    return (unsigned)entry._obj->identity_hash();
  }

  static bool equals(const OopKey& lhs, const OopKey& rhs) {
  //   oop lhs_obj = lhs._obj != nullptr ? lhs._obj : lhs.object_no_keepalive();
  //   oop rhs_obj = rhs._obj != nullptr ? rhs._obj : rhs.object_no_keepalive();
    // return lhs_obj == rhs_obj;
    return lhs._obj == rhs._obj;
  }
};

// Maps oops to object numbers

using ObjectNumberHT = ResizeableHashTable<OopKey, ObjectNumber,
                                           AnyObj::C_HEAP, mtServiceability,
                                           OopKey::get_hash,
                                           OopKey::equals>;

class ObjectNumberTable : public CHeapObj<mtInternal> {
  ObjectNumberHT _table;

public:

  ObjectNumberTable(): _table(INITIAL_VERSION_TABLE_SIZE, MAX_VERSION_TABLE_SIZE) {}

  bool put(oop oop, ObjectNumber objectNumber) {
    return _table.put(OopKey(oop), objectNumber);
  }

  ObjectNumber* get(oop oop) {
    return _table.get(OopKey(oop));
  }
};

using VersionNumberHT = ResizeableHashTable<ObjectNumber, VersionNumber,
                                            AnyObj::C_HEAP, mtServiceability,
                                            ObjectNumberKey::get_hash,
                                            ObjectNumberKey::equals>;

class VersionNumberTable : public CHeapObj<mtInternal> {
  VersionNumberHT _table;

public:
  VersionNumberTable(): _table(INITIAL_VERSION_TABLE_SIZE, MAX_VERSION_TABLE_SIZE) {}

  bool put(ObjectNumber objectNumber, VersionNumber versionNumber) {
    return _table.put(objectNumber, versionNumber);
  }

  VersionNumber* get(ObjectNumber objectNumber) {
    return _table.get(objectNumber);
  }
};

#endif