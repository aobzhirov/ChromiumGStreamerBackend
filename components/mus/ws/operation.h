// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_WS_OPERATION_H_
#define COMPONENTS_MUS_WS_OPERATION_H_

#include <set>

#include "base/macros.h"
#include "components/mus/common/types.h"

namespace mus {
namespace ws {

class WindowServer;
class WindowTree;

enum class OperationType {
  NONE,
  ADD_TRANSIENT_WINDOW,
  ADD_WINDOW,
  DELETE_WINDOW,
  EMBED,
  RELEASE_CAPTURE,
  REMOVE_TRANSIENT_WINDOW_FROM_PARENT,
  REMOVE_WINDOW_FROM_PARENT,
  REORDER_WINDOW,
  SET_CAPTURE,
  SET_FOCUS,
  SET_WINDOW_BOUNDS,
  SET_WINDOW_PREDEFINED_CURSOR,
  SET_WINDOW_PROPERTY,
  SET_WINDOW_VISIBILITY,
};

// This class tracks the currently pending client-initiated operation.
// This is typically used to suppress superfluous notifications generated
// by suboperations in the window server.
class Operation {
 public:
  Operation(WindowTree* tree,
            WindowServer* window_server,
            OperationType operation_type);
  ~Operation();

  ConnectionSpecificId source_tree_id() const { return source_tree_id_; }

  const OperationType& type() const { return operation_type_; }

  // Marks the tree with the specified id as having been sent a message
  // during the course of |this| operation.
  void MarkTreeAsMessaged(ConnectionSpecificId tree_id) {
    message_ids_.insert(tree_id);
  }

  // Returns true if MarkTreeAsMessaged(tree_id) was invoked.
  bool DidMessageTree(ConnectionSpecificId tree_id) const {
    return message_ids_.count(tree_id) > 0;
  }

 private:
  WindowServer* const window_server_;
  const ConnectionSpecificId source_tree_id_;
  const OperationType operation_type_;

  // See description of MarkTreeAsMessaged/DidMessageTree.
  std::set<ConnectionSpecificId> message_ids_;

  DISALLOW_COPY_AND_ASSIGN(Operation);
};

}  // namespace ws
}  // namespace mus

#endif  // COMPONENTS_MUS_WS_OPERATION_H_
