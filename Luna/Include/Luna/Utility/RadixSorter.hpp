#pragma once

#include <vector>

namespace Luna {
template <int Offset, int Bits, typename ValueT, typename IndexT>
void RadixSortPass(ValueT* __restrict outputs,
                   const ValueT* __restrict inputs,
                   IndexT* __restrict outputIndices,
                   const IndexT* __restrict inputIndices,
                   IndexT* __restrict scratchIndices,
                   size_t count) {
	constexpr int ValueCount = 1 << Bits;

	IndexT perValueCounts[ValueCount] = {};
	for (size_t i = 0; i < count; ++i) {
		ValueT c          = (inputs[i] >> Offset) & ((ValueT(1) << Bits) - ValueT(1));
		scratchIndices[i] = perValueCounts[c]++;
	}

	IndexT perValueCountsPrefix[ValueCount];
	IndexT prefixSum = 0;
	for (int i = 0; i < ValueCount; ++i) {
		perValueCountsPrefix[i] = prefixSum;
		prefixSum += perValueCounts[i];
	}

	for (size_t i = 0; i < count; ++i) {
		ValueT inp            = inputs[i];
		ValueT c              = (inp >> Offset) & ((ValueT(1) << Bits) - ValueT(1));
		IndexT effectiveIndex = scratchIndices[i] + perValueCountsPrefix[c];
		IndexT inputIndex     = inputIndices ? inputIndices[i] : i;

		outputIndices[effectiveIndex] = inputIndex;
		outputs[effectiveIndex]       = inp;
	}
}

template <typename CodeT, int... Pattern>
class RadixSorter {
	static_assert(sizeof...(Pattern) % 2 == 0, "RadixSorter requires an even number of radix passes.");
	static_assert(sizeof...(Pattern) > 0, "RadixSorter requires at least one radix pass.");

 public:
	CodeT* CodeData() {
		return _codes.data();
	}
	const CodeT* CodeData() const {
		return _codes.data();
	}
	const uint32_t* IndicesData() const {
		return _indices.data();
	}
	size_t Size() const {
		return _size;
	}

	void Resize(size_t count) {
		_codes.reserve(count * 2);
		_indices.reserve(count * 3);
		_size = count;
	}

	void Sort() {
		SortInnerFirst<Pattern...>();
	}

 private:
	template <int Offset>
	void SortInner(CodeT*, CodeT*, uint32_t*, uint32_t*, uint32_t*) {}

	template <int Offset, int Count, int... Counts>
	void SortInner(CodeT* outputValues,
	               CodeT* inputValues,
	               uint32_t* outputIndices,
	               uint32_t* inputIndices,
	               uint32_t* scratchIndices) {
		RadixSortPass<Offset, Count>(outputValues, inputValues, outputIndices, inputIndices, scratchIndices, _size);
		SortInner<Offset + Count, Counts...>(inputValues, outputValues, inputIndices, outputIndices, scratchIndices);
	}

	template <int Count, int... Counts>
	void SortInnerFirst() {
		auto* outputValues   = _codes.data();
		auto* inputValues    = _codes.data() + _size;
		auto* outputIndices  = _indices.data();
		auto* inputIndices   = _indices.data() + _size;
		auto* scratchIndices = _indices.data() + (_size * 2);

		RadixSortPass<0, Count>(
			inputValues, outputValues, inputIndices, static_cast<const uint32_t*>(nullptr), scratchIndices, _size);
		SortInner<Count, Counts...>(outputValues, inputValues, outputIndices, inputIndices, scratchIndices);
	}

	std::vector<CodeT> _codes;
	std::vector<uint32_t> _indices;
	size_t _size = 0;
};
}  // namespace Luna
