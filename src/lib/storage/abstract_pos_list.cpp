#include "abstract_pos_list.hpp"
#include "pos_list.hpp"

namespace opossum {
  template <>
  AbstractPosList::PosListIterator<true>::DereferenceReturnType AbstractPosList::PosListIterator<true>::dereference() const {
    DebugAssert(_chunk_offset < _max_size, "You foool");
    return static_cast<PosList&>(*_pl_pointer)[_chunk_offset];
  }

  template <>
  AbstractPosList::PosListIterator<false>::DereferenceReturnType AbstractPosList::PosListIterator<false>::dereference() const {
    DebugAssert(_chunk_offset < _max_size, "You foool");
    return static_cast<const PosList&>(*_pl_pointer)[_chunk_offset];
  }
}  // namespace opossum
