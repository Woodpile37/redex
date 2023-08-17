/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigFiles.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <json/json.h>
#include <string>
#include <vector>

#include "Debug.h"
#include "DexClass.h"
#include "FrameworkApi.h"
#include "InlinerConfig.h"
#include "MethodProfiles.h"
#include "ProguardMap.h"

using namespace std::string_literals;

namespace {

class StringTabSplitter {
 private:
  std::string_view m_line;
  std::string_view::size_type m_start{0};
  std::string_view::size_type m_end;

  std::string_view::size_type get_end() const {
    auto end = m_line.find('\t', m_start);
    return end == std::string::npos ? m_line.size() : end;
  }

 public:
  explicit StringTabSplitter(std::string_view line)
      : m_line(line), m_end(get_end()) {}

  std::string_view get() const {
    return m_line.substr(m_start, m_end - m_start);
  }

  bool next() {
    if (m_end == m_line.size()) {
      return false;
    }
    always_assert(m_line[m_end] == '\t');
    m_start = m_end + 1;
    m_end = get_end();
    return true;
  }
};

void from_chars(std::string_view s, int64_t* res) {
  // make 0-terminated, probably using small-string optimization
  std::string copy(s);
  char* endptr = nullptr;
  *res = strtol(copy.data(), &endptr, 10);
  always_assert(endptr == copy.data() + copy.size());
}
} // namespace

ConfigFiles::ConfigFiles(const Json::Value& config, const std::string& outdir)
    : m_json(config),
      outdir(outdir),
      m_global_config(GlobalConfig::default_registry()),
      m_proguard_map(
          new ProguardMap(config.get("proguard_map", "").asString(),
                          config.get("use_new_rename_map", 0).asBool())),
      m_printseeds(config.get("printseeds", "").asString()),
      m_method_profiles(new method_profiles::MethodProfiles()),
      m_secondary_method_profiles(new method_profiles::MethodProfiles()) {

  m_coldstart_class_filename = config.get("coldstart_classes", "").asString();
  if (m_coldstart_class_filename.empty()) {
    m_coldstart_class_filename =
        config.get("default_coldstart_classes", "").asString();
  }

  uint32_t instruction_size_bitwidth_limit =
      config.get("instruction_size_bitwidth_limit", 0).asUInt();
  always_assert_log(
      instruction_size_bitwidth_limit < 32,
      "instruction_size_bitwidth_limit must be between 0 and 31, actual: %u\n",
      instruction_size_bitwidth_limit);
  m_instruction_size_bitwidth_limit = instruction_size_bitwidth_limit;
}

ConfigFiles::ConfigFiles(const Json::Value& config) : ConfigFiles(config, "") {}

ConfigFiles::~ConfigFiles() {
  // Here so that we can use `unique_ptr` to hide full class defs in the header.
}

/**
 * This function relies on the g_redex.
 */
const std::unordered_set<DexType*>& ConfigFiles::get_no_optimizations_annos() {
  if (m_no_optimizations_annos.empty()) {
    Json::Value no_optimizations_anno;
    m_json.get("no_optimizations_annotations", Json::nullValue,
               no_optimizations_anno);
    if (!no_optimizations_anno.empty()) {
      for (auto const& config_anno_name : no_optimizations_anno) {
        std::string anno_name = config_anno_name.asString();
        DexType* anno = DexType::get_type(anno_name);
        if (anno) m_no_optimizations_annos.insert(anno);
      }
    }
  }
  return m_no_optimizations_annos;
}

const std::unordered_set<std::string>&
ConfigFiles::get_no_optimizations_blocklist() {
  if (m_no_optimizations_blocklist.empty()) {
    Json::Value no_optimizations_blocklist;
    m_json.get("no_optimizations_blocklist", Json::nullValue,
               no_optimizations_blocklist);
    if (!no_optimizations_blocklist.empty()) {
      for (auto const& name : no_optimizations_blocklist) {
        m_no_optimizations_blocklist.insert(name.asString());
      }
    }
  }
  return m_no_optimizations_blocklist;
}

/**
 * This function relies on the g_redex.
 */
const std::unordered_set<DexMethodRef*>& ConfigFiles::get_pure_methods() {
  if (m_pure_methods.empty()) {
    Json::Value pure_methods;
    m_json.get("pure_methods", Json::nullValue, pure_methods);
    if (!pure_methods.empty()) {
      for (auto const& method_name : pure_methods) {
        std::string name = method_name.asString();
        DexMethodRef* method = DexMethod::get_method(name);
        if (method) m_pure_methods.insert(method);
      }
    }
  }
  return m_pure_methods;
}

/**
 * This function relies on the g_redex.
 */
const std::unordered_set<const DexString*>&
ConfigFiles::get_finalish_field_names() {
  if (m_finalish_field_names.empty()) {
    Json::Value finalish_field_names;
    m_json.get("finalish_field_names", Json::nullValue, finalish_field_names);
    if (!finalish_field_names.empty()) {
      for (auto const& field_name : finalish_field_names) {
        std::string name = field_name.asString();
        auto* str = DexString::make_string(name);
        if (str) m_finalish_field_names.insert(str);
      }
    }
  }
  return m_finalish_field_names;
}

/**
 * This function relies on the g_redex.
 */

const std::unordered_set<DexType*>& ConfigFiles::get_do_not_devirt_anon() {
  if (m_no_devirtualize_annos.empty()) {
    std::vector<std::string> no_devirtualize_anno_names;
    m_json.get("no_devirtualize_annos", {}, no_devirtualize_anno_names);
    if (!no_devirtualize_anno_names.empty()) {
      for (auto& name : no_devirtualize_anno_names) {
        auto* typ = DexType::get_type(name);
        if (typ) {
          m_no_devirtualize_annos.insert(typ);
        }
      }
    }
  }
  return m_no_devirtualize_annos;
}
/**
 * Read an interdex list file and return as a vector of appropriately-formatted
 * classname strings.
 */
std::vector<std::string> ConfigFiles::load_coldstart_classes() {
  if (m_coldstart_class_filename.empty()) {
    return {};
  }

  static constexpr std::string_view kClassTail = ".class";
  static constexpr size_t lentail = kClassTail.size();

  std::vector<std::string> coldstart_classes;

  std::ifstream input(m_coldstart_class_filename);
  if (!input) {
    throw RedexException(
        RedexError::INVALID_BETAMAP,
        "[error] Can not open <coldstart_classes> file, path is "s +
            m_coldstart_class_filename);
  }

  auto validate_class_spec =
      [](std::string_view class_spec) -> std::pair<bool, size_t> {
    if (lentail >= class_spec.size()) {
      return {false, static_cast<size_t>(-1)};
    }

    auto pos_tail = class_spec.size() - lentail;

    if (class_spec.compare(pos_tail, lentail, kClassTail) != 0) {
      return {false, static_cast<size_t>(-1)};
    }

    return {true, pos_tail};
  };

  std::string clzname;
  while (input >> clzname) {
    auto [valid_class_spec, pos_tail] = validate_class_spec(clzname);
    if (!valid_class_spec) {
      throw RedexException(RedexError::INVALID_BETAMAP,
                           "Bailing, invalid class spec"s +
                               m_coldstart_class_filename +
                               " in interdex file " + clzname);
    }

    clzname.replace(pos_tail, lentail, ";");
    coldstart_classes.emplace_back(
        m_proguard_map->translate_class("L" + clzname));
  }
  return coldstart_classes;
}

/**
 * Read a map of {list_name : class_list} from json
 */
std::unordered_map<std::string, std::vector<std::string>>
ConfigFiles::load_class_lists() {
  std::unordered_map<std::string, std::vector<std::string>> lists;
  std::string class_lists_filename;
  this->m_json.get("class_lists", "", class_lists_filename);

  if (class_lists_filename.empty()) {
    return lists;
  }

  std::ifstream input(class_lists_filename);
  Json::Reader reader;
  Json::Value root;
  bool parsing_succeeded = reader.parse(input, root);
  always_assert_log(
      parsing_succeeded, "Failed to parse class list json from file: %s\n%s",
      class_lists_filename.c_str(), reader.getFormattedErrorMessages().c_str());

  for (Json::ValueIterator it = root.begin(); it != root.end(); ++it) {
    std::vector<std::string> class_list;
    Json::Value current_list = *it;
    for (Json::ValueIterator list_it = current_list.begin();
         list_it != current_list.end();
         ++list_it) {
      lists[it.key().asString()].push_back((*list_it).asString());
    }
  }

  lists["secondary_dex_head.list"] = get_coldstart_classes();

  return lists;
}

const std::unordered_map<std::string, ConfigFiles::DeadClassLoadCounts>&
ConfigFiles::get_dead_class_list() {
  build_dead_class_and_live_class_split_lists();
  return m_dead_classes;
}

const std::unordered_set<std::string>&
ConfigFiles::get_live_class_split_list() {
  build_dead_class_and_live_class_split_lists();
  return m_live_relocated_classes;
}

bool ConfigFiles::is_relocated_class(std::string_view name) const {
  return boost::algorithm::ends_with(name, CLASS_SPLITTING_RELOCATED_SUFFIX);
}

void ConfigFiles::remove_relocated_part(std::string_view* name) {
  always_assert(name != nullptr);
  if (name->length() < CLASS_SPLITTING_RELOCATED_SUFFIX_LEN) {
    return;
  }
  name->remove_suffix(CLASS_SPLITTING_RELOCATED_SUFFIX_LEN);
}

void ConfigFiles::build_dead_class_and_live_class_split_lists() {
  if (!m_dead_class_list_attempted) {
    m_dead_class_list_attempted = true;
    std::string dead_class_list_filename;
    this->m_json.get("dead_class_list", "", dead_class_list_filename);
    if (!dead_class_list_filename.empty()) {
      std::ifstream input(dead_class_list_filename);
      if (!input) {
        fprintf(stderr,
                "[error] Can not open <dead_class_list> file, path is %s\n",
                dead_class_list_filename.c_str());
        exit(EXIT_FAILURE);
      }
      for (std::string line; std::getline(input, line);) {
        // trim trailing whitespace
        line.erase(std::find_if(line.rbegin(), line.rend(),
                                [](auto c) { return c > ' '; })
                       .base(),
                   line.end());
        DeadClassLoadCounts load_counts;
        StringTabSplitter splitter(line);
        std::string_view classname = splitter.get();
        if (splitter.next()) {
          std::string_view str = splitter.get();
          from_chars(str, &load_counts.sampled);
          if (splitter.next()) {
            str = splitter.get();
            from_chars(str, &load_counts.unsampled);
          }
        }
        bool is_relocated = is_relocated_class(classname);
        if (is_relocated) {
          remove_relocated_part(&classname);
        }
        std::string converted;
        converted.reserve(classname.size() + 2);
        converted.append(1, 'L');
        converted.append(classname);
        converted.append(1, ';');
        std::replace(converted.begin(), converted.end(), '.', '/');
        if (!is_relocated) {
          auto translated = m_proguard_map->translate_class(converted);
          m_dead_classes.emplace(std::move(translated), load_counts);
        } else {
          // No need to proguard translate the name of the live classes since
          // we use the unobfuscated name. The unobfuscated name is already
          // translated in ProguardMap.apply_deobfuscated_names called
          // from redex_frontend in main.cpp.
          m_live_relocated_classes.insert(std::move(converted));
        }
      }
    }
  }
}

void ConfigFiles::ensure_agg_method_stats_loaded() const {
  if (m_method_profiles->is_initialized()) {
    return;
  }
  std::vector<std::string> csv_filenames;
  get_json_config().get("agg_method_stats_files", {}, csv_filenames);
  if (csv_filenames.empty()) {
    return;
  }
  m_method_profiles->initialize(csv_filenames);
}

void ConfigFiles::ensure_secondary_method_stats_loaded() const {
  std::vector<std::string> csv_filenames;
  get_json_config().get("secondary_method_stats_files", {}, csv_filenames);
  if (csv_filenames.empty() || m_secondary_method_profiles->is_initialized()) {
    return;
  }
  m_secondary_method_profiles->initialize(csv_filenames);
}

void ConfigFiles::load_inliner_config(inliner::InlinerConfig* inliner_config) {
  Json::Value config;
  m_json.get("inliner", Json::nullValue, config);
  if (config.empty()) {
    m_json.get("MethodInlinePass", Json::nullValue, config);
    always_assert_log(
        config.empty(),
        "MethodInlinePass is no longer used for inliner config, use "
        "\"inliner\"");
    fprintf(stderr, "WARNING: No inliner config\n");
    return;
  }
  JsonWrapper jw(config);
  jw.get("delete_non_virtuals", true, inliner_config->delete_non_virtuals);
  jw.get("virtual", true, inliner_config->virtual_inline);
  jw.get("true_virtual_inline", false, inliner_config->true_virtual_inline);
  jw.get("throws", false, inliner_config->throws_inline);
  jw.get("throw_after_no_return", false, inliner_config->throw_after_no_return);
  jw.get("max_cost_for_constant_propagation",
         MAX_COST_FOR_CONSTANT_PROPAGATION,
         inliner_config->max_cost_for_constant_propagation);
  jw.get("enforce_method_size_limit",
         true,
         inliner_config->enforce_method_size_limit);
  jw.get("use_call_site_summaries", true,
         inliner_config->use_call_site_summaries);
  jw.get("intermediate_shrinking", false,
         inliner_config->intermediate_shrinking);
  jw.get("multiple_callers", false, inliner_config->multiple_callers);
  auto& shrinker_config = inliner_config->shrinker;
  jw.get("run_const_prop", false, shrinker_config.run_const_prop);
  jw.get("run_cse", false, shrinker_config.run_cse);
  jw.get("run_copy_prop", false, shrinker_config.run_copy_prop);
  jw.get("run_local_dce", false, shrinker_config.run_local_dce);
  jw.get("run_reg_alloc", false, shrinker_config.run_reg_alloc);
  jw.get("run_fast_reg_alloc", false, shrinker_config.run_fast_reg_alloc);
  jw.get("run_dedup_blocks", false, shrinker_config.run_dedup_blocks);
  jw.get("debug", false, inliner_config->debug);
  jw.get("blocklist", {}, inliner_config->m_blocklist);
  jw.get("caller_blocklist", {}, inliner_config->m_caller_blocklist);
  jw.get("intradex_allowlist", {}, inliner_config->m_intradex_allowlist);
  jw.get("reg_alloc_random_forest", "",
         shrinker_config.reg_alloc_random_forest);
  jw.get("respect_sketchy_methods", true,
         inliner_config->respect_sketchy_methods);
  jw.get("check_min_sdk_refs", true, inliner_config->check_min_sdk_refs);
  size_t max_relevant_invokes_when_local_only;
  jw.get("max_relevant_invokes_when_local_only", 10,
         max_relevant_invokes_when_local_only);
  inliner_config->max_relevant_invokes_when_local_only =
      max_relevant_invokes_when_local_only;

  std::vector<std::string> no_inline_annos;
  jw.get("no_inline_annos", {}, no_inline_annos);
  for (const auto& type_s : no_inline_annos) {
    auto type = DexType::get_type(type_s);
    if (type != nullptr) {
      inliner_config->m_no_inline_annos.emplace(type);
    } else {
      fprintf(stderr, "WARNING: Cannot find no_inline annotation %s\n",
              type_s.c_str());
    }
  }

  std::vector<std::string> force_inline_annos;
  jw.get("force_inline_annos", {}, force_inline_annos);
  for (const auto& type_s : force_inline_annos) {
    auto type = DexType::get_type(type_s);
    if (type != nullptr) {
      inliner_config->m_force_inline_annos.emplace(type);
    } else {
      fprintf(stderr, "WARNING: Cannot find force_inline annotation %s\n",
              type_s.c_str());
    }
  }
}

const inliner::InlinerConfig& ConfigFiles::get_inliner_config() {
  if (m_inliner_config == nullptr) {
    m_inliner_config = std::make_unique<inliner::InlinerConfig>();
    load_inliner_config(m_inliner_config.get());
  }
  return *m_inliner_config;
}

void ConfigFiles::parse_global_config() {
  m_global_config.parse_config(m_json);
}

void ConfigFiles::load(const Scope& scope) {
  get_inliner_config();
  m_inliner_config->populate(scope);
}

void ConfigFiles::process_unresolved_method_profile_lines() {
  ensure_agg_method_stats_loaded();
  m_method_profiles->process_unresolved_lines();
}

void ConfigFiles::process_unresolved_secondary_method_profile_lines() {
  ensure_secondary_method_stats_loaded();
  m_secondary_method_profiles->process_unresolved_lines();
}

const api::AndroidSDK& ConfigFiles::get_android_sdk_api(int32_t min_sdk_api) {
  if (m_android_min_sdk_api == nullptr) {
    always_assert(m_min_sdk_api_level == 0); // not set
    m_min_sdk_api_level = min_sdk_api;
    auto api_file = get_android_sdk_api_file(min_sdk_api);
    m_android_min_sdk_api = std::make_unique<api::AndroidSDK>(api_file);
  }

  always_assert(min_sdk_api == m_min_sdk_api_level);
  return *m_android_min_sdk_api;
}

const ProguardMap& ConfigFiles::get_proguard_map() const {
  return *m_proguard_map;
}

bool ConfigFiles::force_single_dex() const {
  return m_json.get("force_single_dex", false);
}

bool ConfigFiles::emit_incoming_hashes() const {
  return m_json.get("emit_incoming_hashes", false);
}

bool ConfigFiles::emit_outgoing_hashes() const {
  return m_json.get("emit_outgoing_hashes", false);
}

bool ConfigFiles::create_init_class_insns() const {
  return m_json.get("create_init_class_insns", true);
}

bool ConfigFiles::finalize_resource_table() const {
  return m_json.get("finalize_resource_table", false);
}

void ConfigFiles::set_outdir(const std::string& new_outdir) {
  // Gotta ensure "meta" exists.
  auto meta_path = boost::filesystem::path(new_outdir) / "meta";
  boost::filesystem::create_directory(meta_path);
  outdir = new_outdir;
}

void ConfigFiles::set_class_lists(
    std::unordered_map<std::string, std::vector<std::string>> l) {
  m_class_lists = std::move(l);
  m_load_class_lists_attempted = true;
}
