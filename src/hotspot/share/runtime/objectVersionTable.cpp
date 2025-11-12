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

#include "runtime/objectVersionTable.hpp"

GlobalObjectVersionHT GlobalVersionHistoryTable::_table(INITIAL_VERSION_TABLE_SIZE, MAX_VERSION_TABLE_SIZE);


OopKey::OopKey(oop obj) : _obj(obj) {}

// OopKey::OopKey(const OopKey& src) {
//   // move object into WeakHandle when copying into the table
//   if (src._obj != nullptr) {

//     // obj was read with AS_NO_KEEPALIVE, or equivalent, like during
//     // a heap walk.  The object needs to be kept alive when it is published.
//     Universe::heap()->keep_alive(src._obj);

//     _wh = WeakHandle(JvmtiExport::weak_tag_storage(), src._obj);
//   } else {
//     // resizing needs to create a copy.
//     _wh = src._wh;
//   }
//   // obj is always null after a copy.
//   _obj = nullptr;
// }

// void OopKey::release_weak_handle() {
//   // _wh.release(JvmtiExport::weak_tag_storage());
// // }

// oop OopKey::object() const {
//   assert(_obj == nullptr, "Must have a handle and not object");
//   return _wh.resolve();
// }

// oop OopKey::object_no_keepalive() const {
//   assert(_obj == nullptr, "Must have a handle and not object");
//   return _wh.peek();
// }