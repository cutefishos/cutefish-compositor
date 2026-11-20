// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_LIST_MODEL_H_
#define CUTEFISH_SHELL_COMMON_LIST_MODEL_H_

#include "base/signal.h"

namespace cutefish::shell {

// Each signal follows the change: the model already holds the new rows.
struct ListModelSignals {
    // first, count
    base::Signal<int, int> rowsInserted;
    // first, count: the rows that were there
    base::Signal<int, int> rowsRemoved;
    // from, to: the row's index before and after the move
    base::Signal<int, int> rowMoved;
    // first, last: rows whose values changed in place
    base::Signal<int, int> dataChanged;
    base::Signal<> modelReset;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_LIST_MODEL_H_
