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
// #include "utilities/concurrentHashTable.hpp"
#include "utilities/concurrentHashTable.inline.hpp"
#include "utilities/concurrentHashTableTasks.inline.hpp"
/*
 * The Global Object Version Table maps:
 *   ObjectNumber -> [ VersionNumber -> ObjectPayload ]
 *
*/

using ObjectNumber = jlong;
using VersionNumber = jlong; 
using VersionPayload = oop;

static const int INITIAL_TABLE_SIZE = 1007;
static const int MAX_TABLE_SIZE     = 0x3fffffff;

// class VersionNumberKey : AllStatic {
//   static unsigned get_hash(const VersionNumber& entry) { return entry; }
//   static bool equals(const VersionNumber& lhs, const VersionNumber& rhs) { 
//     return lhs == rhs;
//   }
// };

// using ObjectVersionHT = ResizeableHashTable<VersionNumber, VersionPayload,
//                                             AnyObj::C_HEAP, mtServiceability,
//                                             VersionNumberKey::get_hash,
//                                             VersionNumberKey::equals>;

class ObjectVersionTable {

//   ObjectVersionHT _table;
//   VersionNumber _versionCounter;
  
// public:
//   ObjectVersionTable() :
//     _table(INITIAL_TABLE_SIZE, MAX_TABLE_SIZE),
//     // _tableLock(Mutex::nosafepoint, "objectVersionHistoryTable_lock"),
//     _versionCounter(0) {}

//   VersionNumber createVersion(VersionPayload payload) {
//     // MutexLocker mu(&_tableLock, Mutex::_no_safepoint_check_flag);
//     _table.put(_versionCounter, payload);
//     return _versionCounter++;
//   }

};

// class ObjectNumberKey : AllStatic {
//   static unsigned get_hash(const ObjectNumber& entry) { return entry; }
//   static bool equals(const ObjectNumber& lhs, const ObjectNumber& rhs) { 
//     return lhs == rhs;
//   }
// };

class GlobalObjectVersionTableEntry : public CHeapObj<mtInternal> {
private:
  ObjectNumber _objectNumber;
  ObjectVersionTable* _versionTable;

public:
  GlobalObjectVersionTableEntry(ObjectNumber objectNumber, ObjectVersionTable* versionTable):
    _objectNumber(objectNumber), _versionTable(versionTable) {}

  ObjectNumber objectNumber() { return _objectNumber; }

  ObjectVersionTable* versionTable() { return _versionTable; }
};

class GlobalObjectVersionTableConfig : public AllStatic {
public:
  using Value = GlobalObjectVersionTableEntry*;

  static uintx get_hash(Value const& value, bool* is_dead) {
    jlong tid = value->objectNumber();
    return primitive_hash(tid);
  }

  static void* allocate_node(void* context, size_t size, Value const& value) {
//   ThreadIdTable::item_added();
    return AllocateHeap(size, mtInternal);
  }

  static void free_node(void* context, void* memory, Value const& value) {
    delete value;
    FreeHeap(memory);
//   ThreadIdTable::item_removed();
  }
};

class GlobalObjectVersionTableLookup : public StackObj {
private:
  ObjectNumber _objectNumber;
  uintx _hash;
public:
  GlobalObjectVersionTableLookup(ObjectNumber objectNumber)
    : _objectNumber(objectNumber), _hash(primitive_hash(objectNumber)) {}
  uintx get_hash() const {
    return _hash;
  }
  bool equals(GlobalObjectVersionTableEntry** value) {
    bool equals = primitive_equals(_objectNumber, (*value)->objectNumber());
    if (!equals) {
      return false;
    }
    return true;
  }
  bool is_dead(GlobalObjectVersionTableEntry** value) {
    return false;
  }
};

class GlobalObjectVersionTableGet : public StackObj {
private:
  ObjectVersionTable* _return;
public:
  GlobalObjectVersionTableGet(): _return(nullptr) {}
  void operator()(GlobalObjectVersionTableEntry** val) {
    _return = (*val)->versionTable();
  }
  ObjectVersionTable* getResult() {
    return _return;
  }
};

using GlobalObjectVersionHT = ConcurrentHashTable<GlobalObjectVersionTableConfig, mtInternal>;

class GlobalVersionHistoryTable: AllStatic {

  static GlobalObjectVersionHT* _table;
  static volatile ObjectNumber _objectNumber;

public:
  static void init() {
    _table = new GlobalObjectVersionHT();
  }

  // I don't think this should be on the HistoryTable
  static ObjectNumber createObjectNumber() {
    return Atomic::fetch_then_add(&_objectNumber, 1);
  }

  static VersionNumber createVersion(ObjectNumber objNumber, VersionPayload payload) {
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");
    // There's going to be some double checked locking...probably

    Thread* thread = Thread::current();
    GlobalObjectVersionTableLookup lookup(objNumber);
    GlobalObjectVersionTableGet get;
    bool found = _table->get(thread, lookup, get);
    // ObjectVersionTable* objectTable;
    // {
    //   MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);
    //   ObjectVersionTable** table = _table.get(obj);
    //   if (table) {
    //     objectTable = *table;
    //   } else {
    //     // This is a new object that we are versioning, so we have to create
    //     // a new table to track the objects versions
    //     objectTable = NEW_C_HEAP_OBJ(ObjectVersionTable, mtInternal);
    //     assert(objectTable  != nullptr, "failed to allocate!");
    //     _table.put(obj, objectTable);
    //   }
    // }

    // return objectTable->createVersion(payload);
    return 0;
  }
};

#endif