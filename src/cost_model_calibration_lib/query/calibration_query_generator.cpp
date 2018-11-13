#include "calibration_query_generator.hpp"

#include <random>

#include "../configuration/calibration_table_specification.hpp"
#include "calibration_query_generator_aggregates.hpp"
#include "calibration_query_generator_join.hpp"
#include "calibration_query_generator_predicates.hpp"
#include "expression/expression_functional.hpp"

namespace opossum {

const std::vector<const std::shared_ptr<AbstractLQPNode>> CalibrationQueryGenerator::generate_queries(
    const std::vector<CalibrationTableSpecification>& table_definitions) {
  std::vector<const std::shared_ptr<AbstractLQPNode>> queries;
  queries.reserve(table_definitions.size());

  const auto& add_query_if_present = [](std::vector<const std::shared_ptr<AbstractLQPNode>>& vector,
                                        const std::shared_ptr<AbstractLQPNode>& query) {
    if (query) {
      vector.push_back(query);
    }
  };

  const auto& add_queries_if_present = [](std::vector<const std::shared_ptr<AbstractLQPNode>>& vector,
                                          const std::vector<std::shared_ptr<AbstractLQPNode>>& queries) {
    for (const auto& query : queries) {
      if (query) {
        vector.push_back(query);
      }
    }
  };

  for (const auto& table_definition : table_definitions) {
    add_query_if_present(queries, CalibrationQueryGenerator::_generate_aggregate(table_definition));

    add_query_if_present(
        queries,
        _generate_table_scan(table_definition, CalibrationQueryGeneratorPredicates::generate_predicate_column_value));

    add_query_if_present(
        queries,
        _generate_table_scan(table_definition, CalibrationQueryGeneratorPredicates::generate_predicate_column_column));

    add_query_if_present(
        queries, _generate_table_scan(table_definition,
                                      CalibrationQueryGeneratorPredicates::generate_predicate_between_value_value));

    add_query_if_present(
        queries, _generate_table_scan(table_definition,
                                      CalibrationQueryGeneratorPredicates::generate_predicate_between_column_column));

    add_query_if_present(
        queries, _generate_table_scan(table_definition, CalibrationQueryGeneratorPredicates::generate_predicate_like));

    add_query_if_present(
        queries, _generate_table_scan(table_definition, CalibrationQueryGeneratorPredicates::generate_predicate_or));

    add_query_if_present(queries,
                         _generate_table_scan(table_definition,
                                              CalibrationQueryGeneratorPredicates::generate_predicate_equi_on_strings));
  }

  add_queries_if_present(queries, CalibrationQueryGenerator::_generate_join(table_definitions));
  // add_query_if_present(queries, CalibrationQueryGenerator::_generate_foreign_key_join(table_definitions));

  return queries;
}

const std::shared_ptr<AbstractLQPNode> CalibrationQueryGenerator::_generate_table_scan(
    const CalibrationTableSpecification& table_definition, const PredicateGeneratorFunctor& predicate_generator) {
  const auto table = StoredTableNode::make(table_definition.table_name);
  auto projection_node = _generate_projection(table->get_columns());

  auto predicate =
      CalibrationQueryGeneratorPredicates::generate_predicates(predicate_generator, table_definition.columns, table, 3);

  // We are not interested in queries without Predicate
  if (!predicate) {
    return {};
  }

  projection_node->set_left_input(predicate);
  return projection_node;
}

const std::vector<std::shared_ptr<AbstractLQPNode>> CalibrationQueryGenerator::_generate_join(
    const std::vector<CalibrationTableSpecification>& table_definitions) {
  static std::mt19937 engine((std::random_device()()));
  std::uniform_int_distribution<u_int64_t> table_dist(0, table_definitions.size() - 1);

  if (table_definitions.empty()) return {};
  const auto left_table_definition = std::next(table_definitions.begin(), table_dist(engine));
  const auto right_table_definition = std::next(table_definitions.begin(), table_dist(engine));
  const auto left_table = StoredTableNode::make(left_table_definition->table_name);
  const auto right_table = StoredTableNode::make(right_table_definition->table_name);

  const auto join_nodes = CalibrationQueryGeneratorJoin::generate_join(
      CalibrationQueryGeneratorJoin::generate_join_predicate, left_table, right_table);

  if (join_nodes.empty()) {
    return {};
  }

  auto left_predicate = CalibrationQueryGeneratorPredicates::generate_predicates(
      CalibrationQueryGeneratorPredicates::generate_predicate_column_value, left_table_definition->columns, left_table,
      2);

  auto right_predicate = CalibrationQueryGeneratorPredicates::generate_predicates(
      CalibrationQueryGeneratorPredicates::generate_predicate_column_value, right_table_definition->columns,
      right_table, 2);

  // We are not interested in queries without Predicates
  if (!left_predicate || !right_predicate) {
    return {};
  }

  for (const auto& join_node : join_nodes) {
    join_node->set_left_input(left_predicate);
    join_node->set_right_input(right_predicate);
  }

  return join_nodes;
}

// Both Join Inputs are filtered randomly beforehand
//   auto string_template = "SELECT %1% FROM %2% l JOIN %3% r ON l.%4%=r.%5% WHERE (%6%) AND (%7%);";

//   std::random_device random_device;
//   std::mt19937 engine{random_device()};

//
//    Generate Projection columns
//   std::map<std::string, CalibrationColumnSpecification> columns;
//   for (const auto& column : left_table->columns) {
//     const auto column_name = "l." + column.first;
//     columns.insert(std::pair<std::string, CalibrationColumnSpecification>(column_name, column.second));
//   }
//
//   for (const auto& column : right_table->columns) {
//     const auto column_name = "r." + column.first;
//     columns.insert(std::pair<std::string, CalibrationColumnSpecification>(column_name, column.second));
//   }
//
//   const auto join_columns = _generate_join_columns(left_table->columns, right_table->columns);
//
//   if (!join_columns) {
//      Missing potential join columns, will not produce cross join here.
//     std::cout << "There are no join partners for query generation. "
//                  "Check the table configuration, whether there are two columns with the same datatype."
//               << std::endl;
//     return {};
//   }
//
//   const auto left_predicate =
//       CalibrationQueryGeneratorPredicates::generate_predicate_column_value(join_columns->first, *left_table, "l.");
//   const auto right_predicate =
//       CalibrationQueryGeneratorPredicates::generate_predicate_column_value(join_columns->second, *right_table, "r.");
//
//   auto select_columns = _generate_select_columns(columns);
//
//   if (!left_predicate || !right_predicate) {
//     std::cout << "Failed to generate join predicates." << std::endl;
//     return {};
//   }
//
//   return boost::str(boost::format(string_template) % select_columns % left_table->table_name % right_table->table_name %
//                     join_columns->first.first % join_columns->second.first % *left_predicate % *right_predicate);

const std::shared_ptr<AbstractLQPNode> CalibrationQueryGenerator::_generate_aggregate(
    const CalibrationTableSpecification& table_definition) {
  const auto table = StoredTableNode::make(table_definition.table_name);

  const auto aggregate_node = CalibrationQueryGeneratorAggregates::generate_aggregates();

  auto predicate = CalibrationQueryGeneratorPredicates::generate_predicates(
      CalibrationQueryGeneratorPredicates::generate_predicate_column_value, table_definition.columns, table, 1);

  if (!predicate) {
    std::cout << "Failed to generate predicate for Aggregate" << std::endl;
    return {};
  }

  aggregate_node->set_left_input(predicate);
  return aggregate_node;
}

const std::shared_ptr<ProjectionNode> CalibrationQueryGenerator::_generate_projection(
    const std::vector<LQPColumnReference>& columns) {
  static std::mt19937 engine((std::random_device()()));

  std::uniform_int_distribution<u_int64_t> dist(1, columns.size());

  std::vector<LQPColumnReference> sampled;
  std::sample(columns.begin(), columns.end(), std::back_inserter(sampled), dist(engine), engine);

  std::vector<std::shared_ptr<AbstractExpression>> column_expressions;
  for (const auto& column_ref : sampled) {
    column_expressions.push_back(expression_functional::lqp_column_(column_ref));
  }

  return ProjectionNode::make(column_expressions);
}
}  // namespace opossum
