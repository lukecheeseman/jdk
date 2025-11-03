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
                                            AnyObj::C_HEAP, mtServiceability,
                                            VersionNumberKey::get_hash,
                                            VersionNumberKey::equals>;

static const int INITIAL_TABLE_SIZE = 1007;
static const int MAX_TABLE_SIZE     = 0x3fffffff;

class ObjectVersionTable : public CHeapObj<mtInternal> {
  ObjectVersionHT _table;
  VersionNumber _versionCounter; 
  
public:
  ObjectVersionTable() :
    _table(INITIAL_TABLE_SIZE, MAX_TABLE_SIZE),
    _versionCounter(0) {}

  VersionNumber createVersion(VersionPayload payload) {
    _table.put(_versionCounter, payload);
    return _versionCounter++;
  }
};

class ObjectNumberKey : AllStatic {
  static unsigned get_hash(const ObjectNumber& entry) { return entry; }
  static bool equals(const ObjectNumber& lhs, const ObjectNumber& rhs) { 
    return lhs == rhs;
  }
};

using GlobalObjectVersionHT = ResizeableHashTable<ObjectNumber, ObjectVersionTable*,
                                                  AnyObj::C_HEAP, mtServiceability,
                                                  ObjectNumberKey::get_hash,
                                                  ObjectNumberKey::equals>;

class GlobalVersionHistoryTable: public AllStatic {

  static GlobalObjectVersionHT _table;

public:
  static void init() {
    // Empty for now
  }

  static ObjectNumber createObjectNumber() {
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");

    ObjectNumber number = _table.number_of_entries() + 1;
    _table.put(number, new ObjectVersionTable());
    return number;
  }

  static VersionNumber createVersion(ObjectNumber obj, VersionPayload payload) {
    MutexLocker mu(GlobalVersionHistoryTable_lock, Mutex::_no_safepoint_check_flag);
    assert(GlobalVersionHistoryTable_lock != nullptr, "not initialized!");

    ObjectVersionTable** objectTable = _table.get(obj);
    assert(objectTable != nullptr, "object number has not been created yet");    
    return (*objectTable)->createVersion(payload);
  }
};

#endif