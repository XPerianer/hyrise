#pragma once

#include "abstract_pos_list.hpp"

namespace opossum {

class SingleChunkPosList final : public AbstractPosList {
 public:
  explicit SingleChunkPosList(ChunkID chunkID) : _chunk_id(chunkID) {}
  explicit SingleChunkPosList(ChunkID chunkID, std::vector<ChunkOffset>&& chunk_offsets)
    : _chunk_id(chunkID), _offsets(std::move(chunk_offsets)) {}

  bool empty() const final;
  size_t size() const final;

  size_t memory_usage(const MemoryUsageCalculationMode) const final;
  bool references_single_chunk() const final;
  ChunkID common_chunk_id() const final;

  RowID operator[](const size_t index) const final { return RowID{_chunk_id, _offsets[index]}; }

  std::vector<ChunkOffset>& get_offsets();

  PosListIterator<SingleChunkPosList, RowID> begin() const;
  PosListIterator<SingleChunkPosList, RowID> end() const;
  PosListIterator<SingleChunkPosList, RowID> cbegin() const;
  PosListIterator<SingleChunkPosList, RowID> cend() const;

 private:
  ChunkID _chunk_id = INVALID_CHUNK_ID;
  std::vector<ChunkOffset> _offsets;
};

}  // namespace opossum
