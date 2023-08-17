/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

struct ConfigFiles;
class DexMethod;
class DexStore;
class IRInstruction;
struct MethodItemEntry;

enum DexOpcode : uint16_t;

using DexStoresVector = std::vector<DexStore>;

namespace instruction_lowering {

struct Stats {
  size_t to_2addr{0};
  size_t move_for_check_cast{0};
  struct SparseSwitches {
    struct Data {
      size_t all{0};
      size_t in_hot_methods{0};

      Data() = default;
      Data(size_t all, size_t in_hot_methods)
          : all(all), in_hot_methods(in_hot_methods) {}

      Data& operator+=(const Data& rhs) {
        all += rhs.all;
        in_hot_methods += rhs.in_hot_methods;
        return *this;
      }
    };

    std::map<size_t, Data> data;

    SparseSwitches& operator+=(const SparseSwitches& rhs) {
      for (const auto& kv : rhs.data) {
        data[kv.first] += kv.second;
      }
      return *this;
    }
  } sparse_switches{};

  Stats& operator+=(const Stats& that) {
    to_2addr += that.to_2addr;
    move_for_check_cast += that.move_for_check_cast;
    sparse_switches += that.sparse_switches;
    return *this;
  }
};

/*
 * Convert IRInstructions to DexInstructions while doing the following:
 *
 *   - Check consistency of load-param opcodes
 *   - Pick the smallest opcode that can address its operands.
 *   - Insert move instructions as necessary for check-cast instructions that
 *     have different src and dest registers.
 *   - Record the number of instructions converted to /2addr form, and the
 *     number of move instructions inserted because of check-casts.
 */
Stats lower(DexMethod*,
            bool lower_with_cfg = false,
            ConfigFiles* conf = nullptr);

Stats run(DexStoresVector&,
          bool lower_with_cfg = false,
          ConfigFiles* conf = nullptr);

namespace impl {

DexOpcode select_move_opcode(const IRInstruction* insn);

DexOpcode select_const_opcode(const IRInstruction* insn);

DexOpcode select_binop_lit_opcode(const IRInstruction* insn);

bool try_2addr_conversion(MethodItemEntry*);

} // namespace impl

struct CaseKeysExtent {
  int32_t first_key{0};
  int32_t last_key{0};
  uint32_t size{0};

  // Assumes case_keys are not empty and sorted.
  static CaseKeysExtent from_ordered(const std::vector<int32_t>& case_keys);

  // Computes number of entries needed for a packed switch, accounting for any
  // holes that might exist; assumes case_keys are sorted
  uint64_t get_packed_switch_size() const;

  // Whether a sparse switch statement will be more compact than a packed switch
  bool sufficiently_sparse() const;

  // Assumes case_keys are sorted
  uint32_t estimate_switch_payload_code_units() const;
};

class CaseKeysExtentBuilder {
 private:
  std::optional<CaseKeysExtent> m_info;

 public:
  void insert(int32_t case_key);
  const CaseKeysExtent& operator*() const;
  const CaseKeysExtent* operator->() const;
};

} // namespace instruction_lowering
