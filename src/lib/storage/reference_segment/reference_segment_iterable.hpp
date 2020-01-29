#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "storage/create_iterable_from_segment.hpp"
#include "storage/dictionary_segment.hpp"
#include "storage/fixed_string_dictionary_segment.hpp"
#include "storage/frame_of_reference_segment.hpp"
#include "storage/reference_segment.hpp"
#include "storage/run_length_segment.hpp"
#include "storage/segment_accessor.hpp"
#include "storage/segment_iterables.hpp"
#include "storage/segment_iterables/any_segment_iterable.hpp"

namespace opossum {

template <typename T, EraseReferencedSegmentType erase_reference_segment_type>
class ReferenceSegmentIterable : public SegmentIterable<ReferenceSegmentIterable<T, erase_reference_segment_type>> {
 public:
  using ValueType = T;
  using SegmentIterable<ReferenceSegmentIterable<T, erase_reference_segment_type>>::_on_with_iterators;

  explicit ReferenceSegmentIterable(const ReferenceSegment& segment) : _segment{segment} {}

  template <typename Functor>
  void _on_with_iterators(const Functor& functor) const {
    const auto referenced_table = _segment.referenced_table();
    const auto referenced_column_id = _segment.referenced_column_id();

    const auto& pos_list = _segment.pos_list();

    // TODO: Change comment to reflect changes
    // If we are guaranteed that the reference segment refers to a single non-NULL chunk, we can do some optimizations.
    // For example, we can use a single, non-virtual segment accessor instead of having to keep multiple and using
    // virtual method calls. If begin_it is NULL, chunk_id will be INVALID_CHUNK_ID. Therefore, we skip this case.
    if (pos_list->references_single_chunk() && pos_list->size() > 0 && pos_list->common_chunk_id() != INVALID_CHUNK_ID) {
      auto referenced_segment = referenced_table->get_chunk(pos_list->common_chunk_id())->get_segment(referenced_column_id);

      bool functor_was_called = false;

      if constexpr (erase_reference_segment_type == EraseReferencedSegmentType::No) {
        resolve_segment_type<T>(*referenced_segment, [&](const auto& typed_segment) {
          using SegmentType = std::decay_t<decltype(typed_segment)>;

          // This is ugly, but it allows us to define segment types that we are not interested in and save a lot of
          // compile time during development. While new segment types should be added here,
#ifdef HYRISE_ERASE_DICTIONARY
          if constexpr (std::is_same_v<SegmentType, DictionarySegment<T>>) return;
#endif

#ifdef HYRISE_ERASE_RUNLENGTH
          if constexpr (std::is_same_v<SegmentType, RunLengthSegment<T>>) return;
#endif

#ifdef HYRISE_ERASE_FIXEDSTRINGDICTIONARY
          if constexpr (std::is_same_v<SegmentType, FixedStringDictionarySegment<T>>) return;
#endif

#ifdef HYRISE_ERASE_FRAMEOFREFERENCE
          if constexpr (std::is_same_v<T, int32_t>) {
            if constexpr (std::is_same_v<SegmentType, FrameOfReferenceSegment<T>>) return;
          }
#endif

          // Always erase LZ4Segment accessors
          if constexpr (std::is_same_v<SegmentType, LZ4Segment<T>>) return;

          if constexpr (!std::is_same_v<SegmentType, ReferenceSegment>) {
            const auto segment_iterable = create_iterable_from_segment<T>(typed_segment);
            segment_iterable.with_iterators(pos_list, functor);

            functor_was_called = true;
          } else {
            Fail("Found ReferenceSegment pointing to ReferenceSegment");
          }
        });

        if (!functor_was_called) {
          PerformanceWarning("ReferenceSegmentIterable for referenced segment type erased by compile-time setting");
        }

      } else {
        PerformanceWarning("Using type-erased accessor as the ReferenceSegmentIterable is type-erased itself");
      }

      if (functor_was_called) return;

      // The functor was not called yet, because we did not instantiate specialized code for the segment type.

      const auto segment_iterable = create_any_segment_iterable<T>(*referenced_segment);
      segment_iterable.with_iterators(pos_list, functor);
    } else {
      using Accessors = std::vector<std::shared_ptr<AbstractSegmentAccessor<T>>>;

      auto accessors = std::make_shared<Accessors>(referenced_table->chunk_count());

      resolve_pos_list_type(pos_list, [&](auto& typed_pos_list){
        auto begin_it = make_pos_list_begin_iterator_nc(*typed_pos_list);
        auto end_it = make_pos_list_end_iterator_nc(*typed_pos_list);
        using dt = typename std::remove_reference<decltype(begin_it)>::type;
        auto begin = MultipleChunkIterator<dt>{referenced_table, referenced_column_id, accessors, begin_it, begin_it};
        auto end = MultipleChunkIterator<dt>{referenced_table, referenced_column_id, accessors, begin_it, end_it};
        functor(begin, end);
      });
    }
  }

  size_t _on_size() const { return _segment.size(); }

 private:
  const ReferenceSegment& _segment;

 private:
  // The iterator for cases where we iterate over a single referenced chunk
  template <typename Accessor, typename PosListIterator>
  class SingleChunkIterator : public BaseSegmentIterator<SingleChunkIterator<Accessor, PosListIterator>, SegmentPosition<T>> {
   public:
    using ValueType = T;
    using IterableType = ReferenceSegmentIterable<T, erase_reference_segment_type>;

   public:
    explicit SingleChunkIterator(const std::shared_ptr<Accessor>& accessor, const PosListIterator& begin_pos_list_it,
                                 const PosListIterator& pos_list_it)
        : _begin_pos_list_it{begin_pos_list_it}, _pos_list_it{pos_list_it}, _accessor{accessor} {}

   private:
    friend class boost::iterator_core_access;  // grants the boost::iterator_facade access to the private interface

    void increment() { ++_pos_list_it; }

    void decrement() { --_pos_list_it; }

    void advance(std::ptrdiff_t n) { _pos_list_it += n; }

    bool equal(const SingleChunkIterator& other) const { return _pos_list_it == other._pos_list_it; }

    std::ptrdiff_t distance_to(const SingleChunkIterator& other) const { return other._pos_list_it - _pos_list_it; }

    SegmentPosition<T> dereference() const {
      const auto pos_list_offset = static_cast<ChunkOffset>(std::distance(_begin_pos_list_it, _pos_list_it));

      if (_pos_list_it->is_null()) return SegmentPosition<T>{T{}, true, pos_list_offset};

      const auto& chunk_offset = _pos_list_it->chunk_offset;

      const auto typed_value = _accessor->access(chunk_offset);

      if (typed_value) {
        return SegmentPosition<T>{std::move(*typed_value), false, pos_list_offset};
      } else {
        return SegmentPosition<T>{T{}, true, pos_list_offset};
      }
    }

   private:
    PosListIterator _begin_pos_list_it;
    PosListIterator _pos_list_it;
    std::shared_ptr<Accessor> _accessor;
  };

  // The iterator for cases where we potentially iterate over multiple referenced chunks
  template <typename PosListIterator>
  class MultipleChunkIterator : public BaseSegmentIterator<MultipleChunkIterator<PosListIterator>, SegmentPosition<T>> {
   public:
    using ValueType = T;
    using IterableType = ReferenceSegmentIterable<T, erase_reference_segment_type>;

   public:
    explicit MultipleChunkIterator(
        const std::shared_ptr<const Table>& referenced_table, const ColumnID referenced_column_id,
        const std::shared_ptr<std::vector<std::shared_ptr<AbstractSegmentAccessor<T>>>>& accessors,
        const PosListIterator& begin_pos_list_it, const PosListIterator& pos_list_it)
        : _referenced_table{referenced_table},
          _referenced_column_id{referenced_column_id},
          _begin_pos_list_it{begin_pos_list_it},
          _pos_list_it{pos_list_it},
          _accessors{accessors} {}

   private:
    friend class boost::iterator_core_access;  // grants the boost::iterator_facade access to the private interface

    void increment() { ++_pos_list_it; }

    void decrement() { --_pos_list_it; }

    void advance(std::ptrdiff_t n) { _pos_list_it += n; }

    bool equal(const MultipleChunkIterator& other) const { return _pos_list_it == other._pos_list_it; }

    std::ptrdiff_t distance_to(const MultipleChunkIterator& other) const { return other._pos_list_it - _pos_list_it; }

    // TODO(anyone): benchmark if using two maps instead doing the dynamic cast every time really is faster.
    SegmentPosition<T> dereference() const {
      const auto pos_list_offset = static_cast<ChunkOffset>(std::distance(_begin_pos_list_it, _pos_list_it));

      if (_pos_list_it->is_null()) return SegmentPosition<T>{T{}, true, pos_list_offset};

      const auto chunk_id = _pos_list_it->chunk_id;
      const auto& chunk_offset = _pos_list_it->chunk_offset;

      if (!(*_accessors)[chunk_id]) {
        _create_accessor(chunk_id);
      }
      const auto typed_value = (*_accessors)[chunk_id]->access(chunk_offset);

      if (typed_value) {
        return SegmentPosition<T>{std::move(*typed_value), false, pos_list_offset};
      } else {
        return SegmentPosition<T>{T{}, true, pos_list_offset};
      }
    }

    void _create_accessor(const ChunkID chunk_id) const {
      auto segment = _referenced_table->get_chunk(chunk_id)->get_segment(_referenced_column_id);
      auto accessor = std::move(create_segment_accessor<T>(segment));
      (*_accessors)[chunk_id] = std::move(accessor);
    }

   private:
    std::shared_ptr<const Table> _referenced_table;
    ColumnID _referenced_column_id;

    PosListIterator _begin_pos_list_it;
    PosListIterator _pos_list_it;

    // PointAccessIterators share vector with one Accessor per Chunk
    std::shared_ptr<std::vector<std::shared_ptr<AbstractSegmentAccessor<T>>>> _accessors;
  };
};

template <typename T>
struct is_reference_segment_iterable {
  static constexpr auto value = false;
};

template <template <typename, EraseReferencedSegmentType> typename Iterable, typename T,
          EraseReferencedSegmentType erase_reference_segment_type>
struct is_reference_segment_iterable<Iterable<T, erase_reference_segment_type>> {
  static constexpr auto value = std::is_same_v<ReferenceSegmentIterable<T, erase_reference_segment_type>,
                                               Iterable<T, erase_reference_segment_type>>;
};

template <typename T>
inline constexpr bool is_reference_segment_iterable_v = is_reference_segment_iterable<T>::value;

}  // namespace opossum
