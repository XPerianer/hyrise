#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "expression/expression_functional.hpp"
#include "logical_query_plan/join_node.hpp"
#include "logical_query_plan/lqp_column_reference.hpp"
#include "logical_query_plan/mock_node.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "statistics/cardinality_estimator.hpp"
#include "statistics/chunk_statistics/histograms/equal_distinct_count_histogram.hpp"
#include "statistics/chunk_statistics/histograms/equal_width_histogram.hpp"
#include "statistics/chunk_statistics/histograms/generic_histogram.hpp"
#include "statistics/chunk_statistics2.hpp"
#include "statistics/segment_statistics2.hpp"
#include "statistics/table_statistics2.hpp"

using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

class CardinalityEstimatorTest : public ::testing::Test {
 public:
  void SetUp() override {
    const auto segment_statistics_a_0_a = std::make_shared<SegmentStatistics2<int32_t>>();
    const auto segment_statistics_a_0_b = std::make_shared<SegmentStatistics2<int32_t>>();

    // clang-format off
    const auto histogram_a_0_a = std::make_shared<EqualDistinctCountHistogram<int32_t>>(
      std::vector<int32_t>{1,  26, 51, 76},
      std::vector<int32_t>{25, 50, 75, 100},
      std::vector<HistogramCountType>{40, 30, 20, 10},
      10, 0);

    const auto histogram_a_0_b = std::make_shared<EqualWidthHistogram<int32_t>>(
      10, 129, std::vector<HistogramCountType>{15, 25, 35}, std::vector<HistogramCountType>{10, 20, 25}, 0);
    // clang-format on

    segment_statistics_a_0_a->equal_distinct_count_histogram = histogram_a_0_a;
    segment_statistics_a_0_b->equal_width_histogram = histogram_a_0_b;

    const auto chunk_statistics_a_0 = std::make_shared<ChunkStatistics2>(100);
    chunk_statistics_a_0->segment_statistics.emplace_back(segment_statistics_a_0_a);
    chunk_statistics_a_0->segment_statistics.emplace_back(segment_statistics_a_0_b);

    const auto table_statistics_a = std::make_shared<TableStatistics2>();
    table_statistics_a->chunk_statistics.emplace_back(chunk_statistics_a_0);

    node_a = MockNode::make(MockNode::ColumnDefinitions{{DataType::Int, "a"}, {DataType::Int, "b"}});
    node_a->set_table_statistics2(table_statistics_a);

    a_a = node_a->get_column("a");
    a_b = node_a->get_column("b");
  }

  CardinalityEstimator estimator;
  LQPColumnReference a_a, a_b;
  std::shared_ptr<MockNode> node_a;
};

TEST_F(CardinalityEstimatorTest, SinglePredicate) {
  // clang-format off
  const auto input_lqp =
  PredicateNode::make(greater_than_(a_a, 50),
    node_a);
  // clang-format on

  EXPECT_FLOAT_EQ(estimator.estimate_cardinality(input_lqp), 30.0f);

  /**
   * Test LQP output statistics objects
   */
  const auto plan_output_statistics = estimator.estimate_statistics(input_lqp);
  EXPECT_FLOAT_EQ(plan_output_statistics->row_count(), 30.0f);  // Same as above
  ASSERT_EQ(plan_output_statistics->chunk_statistics.size(), 1u);

  const auto plan_output_statistics_0 = plan_output_statistics->chunk_statistics.at(0);
  ASSERT_EQ(plan_output_statistics_0->segment_statistics.size(), 2u);

  const auto plan_output_statistics_0_a = std::dynamic_pointer_cast<SegmentStatistics2<int32_t>>(plan_output_statistics_0->segment_statistics.at(0));
  const auto plan_output_statistics_0_b = std::dynamic_pointer_cast<SegmentStatistics2<int32_t>>(plan_output_statistics_0->segment_statistics.at(1));
  ASSERT_TRUE(plan_output_statistics_0_a);
  ASSERT_TRUE(plan_output_statistics_0_b);

  ASSERT_TRUE(plan_output_statistics_0_a->generic_histogram);
  ASSERT_TRUE(plan_output_statistics_0_b->equal_width_histogram);

  EXPECT_TRUE(plan_output_statistics_0_a->generic_histogram->does_not_contain(PredicateCondition::LessThan, 50));
  EXPECT_FLOAT_EQ(plan_output_statistics_0_a->generic_histogram->estimate_cardinality(PredicateCondition::GreaterThan, 75).first, 10.f);
  EXPECT_FLOAT_EQ(plan_output_statistics_0_b->equal_width_histogram->estimate_cardinality(PredicateCondition::LessThan, 50).first, 5.f);
}

TEST_F(CardinalityEstimatorTest, TwoPredicates) {
  // clang-format off
  const auto input_lqp =
  PredicateNode::make(greater_than_(a_a, 50),
    PredicateNode::make(less_than_equals_(a_a, 75),
      node_a));
  // clang-format on

  EXPECT_FLOAT_EQ(estimator.estimate_cardinality(input_lqp), 20);
}

}  // namespace opossum