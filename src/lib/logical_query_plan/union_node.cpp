#include "union_node.hpp"

#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "constant_mappings.hpp"
#include "expression/expression_utils.hpp"
#include "utils/assert.hpp"

namespace opossum {

UnionNode::UnionNode(const UnionMode union_mode) : AbstractLQPNode(LQPNodeType::Union), union_mode(union_mode) {}

std::string UnionNode::description() const { return "[UnionNode] Mode: " + union_mode_to_string.left.at(union_mode); }

const std::vector<std::shared_ptr<AbstractExpression>>& UnionNode::column_expressions() const {
  // This is certainly true for UnionMode::Positions, but once we allow a proper SQL-style union, where the two
  // expressions might even come from different tables, we need to revisit this

  std::cout << "left: ";
  for (const auto& x : left_input()->column_expressions()) std::cout << *x << "\t";
  std::cout << "\nright: ";
  for (const auto& x : right_input()->column_expressions()) std::cout << *x << "\t";
  std::cout << std::endl;

  // Assert(expressions_equal(left_input()->column_expressions(), right_input()->column_expressions()),
  //        "Input Expressions must match");
  return left_input()->column_expressions();
}

bool UnionNode::is_column_nullable(const ColumnID column_id) const {
  Assert(left_input() && right_input(), "Need both inputs to determine nullability");

  return left_input()->is_column_nullable(column_id) || right_input()->is_column_nullable(column_id);
}

size_t UnionNode::_shallow_hash() const { return boost::hash_value(union_mode); }

std::shared_ptr<AbstractLQPNode> UnionNode::_on_shallow_copy(LQPNodeMapping& node_mapping) const {
  return UnionNode::make(union_mode);
}

bool UnionNode::_on_shallow_equals(const AbstractLQPNode& rhs, const LQPNodeMapping& node_mapping) const {
  const auto& union_node = static_cast<const UnionNode&>(rhs);
  return union_mode == union_node.union_mode;
}

}  // namespace opossum
