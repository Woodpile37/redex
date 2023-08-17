/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include "Reachability.h"
#include "RedexTest.h"
#include "Walkers.h"

class ReachabilityTest : public RedexIntegrationTest {};

TEST_F(ReachabilityTest, ReachabilityFromProguardTest) {
  const auto& dexen = stores[0].get_dexen();
  auto pg_config = process_and_get_proguard_config(dexen, R"(
    -keepclasseswithmembers public class RemoveUnreachableTest {
      public void testMethod();
    }
    -keepclasseswithmembers class A {
      int foo;
      <init>();
      int bar();
    }
  )");

  EXPECT_TRUE(pg_config->ok);
  EXPECT_EQ(pg_config->keep_rules.size(), 2);

  reachability::ObjectCounts before = reachability::count_objects(stores);

  EXPECT_EQ(before.num_classes, 20);
  EXPECT_EQ(before.num_methods, 39);
  EXPECT_EQ(before.num_fields, 3);

  int num_ignore_check_strings = 0;
  reachability::IgnoreSets ig_sets;
  reachability::ReachableAspects reachable_aspects;
  auto scope = build_class_scope(stores);
  walk::parallel::code(scope, [&](auto*, auto& code) { code.build_cfg(); });
  auto reachable_objects = reachability::compute_reachable_objects(
      stores, ig_sets, &num_ignore_check_strings, &reachable_aspects);
  walk::parallel::code(scope, [&](auto*, auto& code) { code.clear_cfg(); });

  reachability::sweep(stores, *reachable_objects, nullptr);

  reachability::ObjectCounts after = reachability::count_objects(stores);

  EXPECT_EQ(after.num_classes, 7);
  EXPECT_EQ(after.num_methods, 14);
  EXPECT_EQ(after.num_fields, 2);
}

TEST_F(ReachabilityTest, ReachabilityMarkAllTest) {
  const auto& dexen = stores[0].get_dexen();
  auto pg_config = process_and_get_proguard_config(dexen, R"(
    -keepclasseswithmembers public class RemoveUnreachableTest {
      public void testMethod();
    }
    -keepclasseswithmembers class A {
      int foo;
      <init>();
      int bar();
    }
  )");

  EXPECT_TRUE(pg_config->ok);
  EXPECT_EQ(pg_config->keep_rules.size(), 2);

  reachability::ObjectCounts before = reachability::count_objects(stores);

  EXPECT_EQ(before.num_classes, 20);
  EXPECT_EQ(before.num_methods, 39);
  EXPECT_EQ(before.num_fields, 3);

  int num_ignore_check_strings = 0;
  reachability::IgnoreSets ig_sets;
  reachability::ReachableAspects reachable_aspects;
  auto scope = build_class_scope(stores);
  walk::parallel::code(scope, [&](auto*, auto& code) { code.build_cfg(); });
  auto reachable_objects = reachability::compute_reachable_objects(
      stores, ig_sets, &num_ignore_check_strings, &reachable_aspects,
      /* record_reachability */ false, /* relaxed_keep_class_members */ false,
      /* cfg_gathering_check_instantiable */ false,
      /* cfg_gathering_check_instance_callable */ false,
      /* should_mark_all_as_seed */ true, nullptr);
  walk::parallel::code(scope, [&](auto*, auto& code) { code.clear_cfg(); });

  reachability::sweep(stores, *reachable_objects, nullptr);

  reachability::ObjectCounts after = reachability::count_objects(stores);

  EXPECT_EQ(after.num_classes, 20);
  EXPECT_EQ(after.num_methods, 39);
  EXPECT_EQ(after.num_fields, 3);
}
