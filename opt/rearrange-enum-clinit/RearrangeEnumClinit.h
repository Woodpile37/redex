/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "Pass.h"

class DexMethod;
class IRCode;

namespace rearrange_enum_clinit {

enum class MethodResult {
  kUnknown,
  kNotOneBlock,
  kFailed,
  kChanged,
};

class RearrangeEnumClinitPass : public Pass {
 public:
  RearrangeEnumClinitPass() : Pass("RearrangeEnumClinitPass") {}

  redex_properties::PropertyInteractions get_property_interactions()
      const override {
    return redex_properties::simple::preserves_all();
  }

  void bind_config() override { bind("threshold", m_threshold, m_threshold); }

  void run_pass(DexStoresVector&, ConfigFiles&, PassManager&) override;

 private:
  // "Exposed" for testing purposes.
  static MethodResult run(DexMethod* m, IRCode* code);

  size_t m_threshold{10000000};

  friend class RearrangeEnumClinitTest;
};

} // namespace rearrange_enum_clinit
