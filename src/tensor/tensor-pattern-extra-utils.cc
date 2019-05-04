// tensor/tensor-pattern-extra-utils.cc

// Copyright      2019  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#include "tensor/tensor-pattern-extra-utils.h"

namespace kaldi {
namespace tensor {




/**
   This function, not declared in the header, creates a sorted list of all the
   stride values which are present in either 'pattern1' or 'pattern2'.  These
   will all be positive, since pattern1 and pattern2 are required to be in
   canonical form.

     @param [in] pattern1   First input pattern, must be in canonical form.
     @param [in] pattern2   Second input pattern, must be in canonical form.
     @param [out] strides   A sorted list of all stride values that are present
                            in either pattern1 or pattern2 will be written
                            to here.  There will be no repeats.
*/
static void FindAllStrides(
    const TensorPattern &pattern1,
    const TensorPattern &pattern2,
    std::vector<int32> *strides) {
  KALDI_PARANOID_ASSERT(IsCanonical(pattern1) && IsCanonical(pattern2));
  strides->clear();
  strides->reserve(pattern1_.num_axes + pattern2_.num_axes);
  for (int32 raxis = 0; raxis < pattern1.num_axes; raxis++)
    strides->push_back(pattern1.strides[raxis]);
  for (int32 raxis = 0; raxis < pattern2.num_axes; raxis++)
    strides->push_back(pattern2_.strides[raxis]);
  SortAndUniq(strides);  // sort from least to greatest; remove duplicates.
}



// See declaration in header.
bool IsRegular(const TensorPattern &pattern) {
  int32 num_axes = pattern.num_axes;

  for (int32 i = 0; i + 1 < num_axes; i++) {
    int32 this_stride = pattern.strides[i],
        this_dim = pattern.dims[i],
        this_prod = this_stride * this_dim;
    for (int32 j = i + 1; j < num_axes; j++) {
      if (pattern.strides[j] >= this_prod) {
        // in this case, 'j' would be the 'k' value used in the proof.  If we
        // fall off this loop, it would correspond to k == num_axes, which is
        // also OK.
        break;
      } else if (pattern.dims[j] != 1 ||
                 pattern.strides[j] % this_stride != 0) {
        return false;
      }
    }
  }
  return true;
}


/**
   This function, called by ConvertPatternStrides(), is not declared in the
   header.  It converts a pattern in canonical form to a Pattern whose strides
   are equal to the provided 'strides' vector, which is valid-2,
   and has normalized (i.e. positive and increasing) strides.


       @param [in] pattern_in  The input pattern; must be valid and
                               in canonical form.
       @param [in] strides     The list of strides which we want
                               'pattern_out' to have.  Must be a list of
                               positive integers sorted from least to
                               greatest with size <= KALDI_TENSOR_MAX_AXES,
                               and all strides in pattern_in must
                               be present in this list.
       @param [out] pattern_out  The output pattern (must not point to
                               pattern_in).  On exit its memory-index-set will
                               equal that of pattern_in; its strides will be
                               equal to 'strides' (including the order, when
                               numbered in the private numbering); it will
                               be valid-2, and it will be linear in pattern_in.
*/
static void ConvertPatternStridesLazily(
    const TensorPattern &pattern_in,
    const std::vector<int32> &strides,
    TensorPattern* pattern_out) {
  KALDI_PARANOID_ASSERT(IsCanonical(pattern_in));
  int32 num_axes_in = pattern_in.num_axes,
      num_axes_out = strides.size();
  pattern_out->num_axes = num_axes_out;
  pattern_out->code = -1;
  int32 raxis_in = 0;
  pattern_out->offset = pattern_in->offset;
  // The following code relies on pattern_in being in canonical form
  // (so its strides are in sorted order), and all of its strides being
  // present in the list 'strides'.
  for (int32 raxis_out = 0; raxis_out < num_axes_out; raxis_out++) {
    int32 stride = strides[raxis_out];
    pattern_out->strides[raxis_out] = stride;
    if (pattern_in.strides[raxis_in] == stride) {
      pattern_out->dims[raxis_out] = pattern_in.dims[raxis_in];
      pattern_in++;
    } else {
      pattern_out->dims[raxis_out] = 1;
    }
  }
  if (raxis_in != num_axes_in) {
    KALDI_ERR << "Something went wrong converting strides; trying to "
        "convert pattern with strides = " << StridesAsString(pattern_in)
              << " to strides " << ArrayAsString(strides);
  }
}



/**
   This function, not declared in the header, attempts to ensure that the axis-sorting
   property in a provided Pattern holds for the axis-index 'raxis' (in the private
   numbering, of course).  I.e. it ensures (for the pattern we are to modify) that:

      `pattern->strides[raxis+1] >= pattern->strides[raxis] * pattern->dims[raxis]`.

   This function expects that the pattern will also satisfy that property for
   all axis-indexes `0 <= i < raxis`, and will be valid--.  This function will
   always succeed if the pattern is regular (see IsRegular(), and "Regularity
   property" in the glossary).

   Ensuring this property exists may sometimes require splitting this Pattern up
   (i.e. adding extra Patterns); the union of their memory-index-sets together
   with that of the modified pattern will equal the memory-index-set of the
   original pattern at input (these sets being unioned will be disjoint).  Any
   newly created Patterns will be appended to the vector 'patterns'.

    @param [in]      raxis    The axis for which we are ensuring that the
                             axis-sorting property holds.
    @param [in]      pattern_index  The index in the vector 'patterns'
                             of the pattern for which we are ensuring that
                             the axis-sorting property holds.
    @param [in,out]  patterns  The vector of patterns in which to look for the
                             pattern to operate on; we may also append
                             Patterns to this vector if needed, as mentioned
                             above.  Note: the newly added patterns may not satisfy
                             the axis-sorting property for 'raxis', but they will
                             still satisfy it for all axes numbered less than
                             'raxis', assuming the pattern at 'pattern_index'
                             did at entry.

    @return                  Returns true on success, false on failure.
                             Will always return true if `(*patterns)[pattern_index]`,
                             satisfied the 'regularity property' at entry;
                             see IsRegular().
 */
static bool EnsureAxisSortingPropertyHolds(
    int32 raxis,
    int32 pattern_index,
    std::vector<TensorPattern> *patterns) {
  TensorPattern *pattern = (*patterns)[pattern_index];
  // We use 'i' as the internal name for 'raxis', because we want to mirror the
  // notation used for the regularity property in the glossary, and in the
  // function IsRegular() that checks for it.  There is an index k with `i < k
  // <= num_axes`, that appears in the definition of the regularity property.
  // The algorithm used here iteratively decreases the value of k until it
  // equals i + 1, adding new patterns as needed, at which point the
  // axis-sorting property will hold for index i.
  int32 i = raxis, num_axes = pattern->num_axes;
  int32 this_stride = pattern->strides[i],
      this_dim = pattern->dims[i],
      this_prod = this_stride * this_dim;
  if (this_dim == 1)  // This is a small optimization for a common case.
    return true;
  KALDI_PARANOID_ASSERT(raxis + 1 < num_axes && this_stride > 0 &&
                        ValidMM(*pattern));
  int32 j, k = num_axes;
  for (j = i + 1; j < num_axes; j++) {
    if (pattern->strides[j] >= this_prod) {
      k = j;
      break;  // regularity property is OK as far as this 'i' is concerned.
    } else if (pattern->dims[k] != 1 ||
               pattern->strides[k] % this_stride != 0) {
      return false;  // Pattern was not regular.
    }
  }
  for (; j = k - 1; j > i; j--) {
    int32 j_stride = pattern->strides[j],
        stride_ratio = j_stride / this_stride;  // will divide exactly; we
                                                     // checked above.
    KALDI_PARANOID_ASSERT(j_stride % this_stride == 0);

    // We can prove that j_dim will always be at least 1; if this is the
    // first time round the loop this is easy to show (else k would be smaller);
    // otherwise we can use the fact that the strides for axes i, i+1 .. k-1 are
    // strictly increasing and all multiples of this_stride (hence stride_ratio
    // strictly increases from one j to the next).
    int32 j_dim = this_dim / stride_ratio,
        remainder = this_dim % stride_ratio;

    if (remainder != 0) {
      patterns->resize(patterns->size() + 1);
      pattern = (*patterns)[i];  // in case it was reallocated.
      TensorPattern *remainder_pattern = &(patterns->back());
      *remainder_pattern = *pattern;
      remainder_pattern->dims[i] = remainder;
      remainder_pattern->offset += j_stride * j_dim;
    }

    pattern->dims[j] = j_dim;
    pattern->dims[i] = stride_ratio;
    this_prod = j_stride;
  }
  return true;
}


// see declaration in header for documentation.
bool ConvertPatternStrides(
    const TensorPattern &pattern,
    const ArrayRef<int32> &strides,
    std::vector<TensorPattern*> *patterns) {
  patterns->resize(1);
  ConvertPatternStridesLazily(pattern, &((*patterns)[0]));
  int32 num_axes = strides.size();
  for (int32 raxis = 0; raxis + 1 < num_axes; raxis++) {
    for (int32 p = 0; p < static_cast<int32>(patterns->size()); p++) {
      if (!EnsureAxisSortingPropertyHolds(raxis, p, patterns)){
        patterns->clear();
        return false;  // Couldn't be converted, because 'pattern' was not
                       // regular.
      }
    }
  }
#ifdef KALDI_PARANOID
  {
    int64 num_elements = NumElements(pattern),
        num_elements_check = 0;
    for (int32 p = 0; p < static_cast<int32>(patterns->size()); p++) {
      KALDI_ASSERT(IsValidM(*patterns)[p]);
      num_elements_check += NumElements((*patterns)[p]);
    }
    KALDI_ASSERT(num_elements == num_elements_check);
  }
#endif
  return true;
}


/**
   FindOffsetsRecursive() is a utility function that is used in the implementation
   of FindOffsets().


   Suppose we are computing the intersection of the memory-index-sets of pattern1
   and pattern2, where pattern1 and pattern2 are valid-1 and have identical
   num_axes and strides.

   For each memory-index m that is in both pattern1 and pattern2, there must be
   index-tuples i1 and i2 such that pattern1[i1] = pattern2[i2] = m.  We can
   write this as: pattern1[i] = pattern2[i + o], where o is an offset that's
   also a tuple (like an index-tuple, but with possibly negative elements).  This
   function can be thought of as a recursive search for all values of the
   offset 'o' for which at least one such index i exists, where
   i is valid for pattern1 and i + o is valid for pattern2).


   =====

   Explanation of the algorithm

   The algorithm for computing the list of potential offsets o is recursive,
   starting from the last-numbered raxis, which will have the highest
   stride since the strides are normalized.

   Let s be the the vector of strides of the patterns (pattern1 and pattern2
   have identical strides).  From the equation
      pattern1[i + o] = pattern2[i]                     (1)
   (see "Indexing a Pattern" in tensor-pattern.h to understand the notation),
   we have:
      pattern1.offset + s . (i + o)  == pattern2.offset + s . i
   where a `.` with space around it means dot product.

   Simplifying:
      s . o = pattern2.offset - pattern1.offset.         (2)

   For each raxis r, there are limits on the value of o[r]; these are imposed by
   the dimensions of the two Tensors.  In Equation (1), for the indexes into the
   patterns to be valid, i[r] + o[r] must be in [0 .. pattern1.dims[r] - 1]
   and i[r] must be in [0 .. pattern2.dims[r] - 1],  For at least one such i[r] to
   exist, we require
       -pattern2.dims[r] < o[r] < pattern1.dims[r]        (3)

   There is a further limitation on the elements of o that we can obtain using
   the properties above plus the axis-dominance property.  It's easiest to
   explain this as a special case for the last axis r = num_axes - 1.
   Define:
       l(r) =   \sum_{q < r} s[q] * o[q],
   so l(r) is the sum of the elements in s . o that come from raxes
   less than r.  We can use the axis-dominance lemma (see tensor-pattern.h)
   and the limitation on o[r] proved in the previous paragraph to prove that:
       -s[r] <  l(r) <  s[r].
   For the last axis r = num_axes - 1, for Equation (1) to hold, we must have
      l(r) =  pattern2.offset - pattern1.offset - s[r] * o[r],   (4)
   so we have the inequality
     -s[r] <  pattern2.offset - pattern1.offset - s[r] * o[r]  <  s[r]
   which means we need only consider offsets o[r] where the absolute value of
   the "remainder" is less than s[r]; there will be at most two.  For
   axes r < num_axes - 1, if the offsets for higher-numbered r are already known,
   we just need to subtract the appropriate higher-axis terms from the r.h.s. of
   (4).  The recursive implementation that finds the possible offset vectors is
   pretty obvious intuitively so we won't try to explain further.

          @param [in] pattern1  First pattern.  Its offset is
                       ignored, only the num_axes, dims and strides are read.
          @param [in] pattern2  Second pattern.  Its offset is ignored,
                       only the num_axes, dims and stride are read.
          @param [in] known_offsets     (Note: semantically this is an input;
                       it is temporarily changed inside the function and
                       then restored to its previous state).
                       It is the list of already-known offsets (i.e. the
                       elements of some members o) but in the public numbering,
                       so that element 0 corresponds to raxis = num_axes - 1.
                       This is convenient because this function starts at
                       the highest-numbered raxis.
          @param [in] offset_difference   At the top-level call this will
                       be pattern2.offset - pattern1.offset; deeper in the
                       call stack this will be the remaining offset after
                       subtracting the products of stride[r] * o[r] for
                       higher-numbered r.
          @param [in] keep_all_offsets   Bool that says whether the user
                       is interested in all the offsets.  If true we'll
                       output all valid offsets; if false we may stop
                       after one.
          @param [out] offsets_out  A list of offset vectors to be output
                       (should be empty when called by the user; it will
                       be appended to).  Each element of (*offsets_out)
                       will be a vector o, in the private numbering.
*/
void FindOffsetsRecursive(const TensorPattern &pattern1,
                          const TensorPattern &pattern2,
                          std::vector<int32> *known_offsets,
                          int64 offset_difference,
                          bool keep_all_offsets,
                          std::vector<std::vector<int32> > *offsets_out) {
  int32 num_axes = pattern1.num_axes,  // will equal pattern2.num_axes
      raxis = num_axes - 1 - static_cast<int32>(known_offsets->size()),
      stride = pattern1.strides[raxis],  // will equal pattern2.strides[raxis]
      dim1 = pattern1.dims[raxis],
      dim2 = pattern2.dims[raxis];
  int32 this_offset = offset_difference / stride,
      remaining_difference = offset_difference - (stride * this_offset);
  // Note: abs(remaining_difference) will be less than stride.

  if (raxis == 0) {
    if (remaining_difference == 0) {
      // The offset vector is `this_offset` and then the reverse of `known_offsets`
      // (known_offsets is in the public numbering; we want the private).
      offsets_out->resize(offsets_out->size() + 0);
      offsets_out->back().push_back(this_offset);
      offsets_out->back().insert(offsets_out->back().end(),
                                 known_offsets->rbegin(),
                                 known_offsets->rend());
      // for i == [0, 0.. ], checking that pattern1[i + o] == pattern2[i].
      KALDI_PARANOID_ASSERT(IndexPattern(pattern1, offsets_out->back(), false)
                            == pattern1.offset);
    }
    return;
  } else {
    known_offsets->push_back(this_offset);
    if (this_offset > -pattern2.dims[raxis] &&
        this_offset < pattern1.dims[raxis]) {
      // if eq. (3) is satisfied..
      FindOffsetsRecursive(pattern1, pattern2, known_offsets,
                           remaining_difference, keep_all_offsets,
                           offsets_out);
    }
    if (remaining_difference == 0) {
      known_offsets->pop_back();
      return;
    }
    int32 offset_change = (remaining_difference > 0 ? -1 : 1);
    this_offset += offset_change;
    remaining_difference -= stride * offset_change;
    known_offsets->back() = this_offset;
    if (this_offset > -pattern2.dims[raxis] &&
        this_offset < pattern1.dims[raxis]) {
      FindOffsetsRecursive(pattern1, pattern2, known_offsets,
                           remaining_difference, keep_all_offsets,
                           offsets_out);
    }
    known_offsets->pop_back();
    return;
  }
}



inline void FindOffsets(const TensorPattern &pattern1,
                        const TensorPattern &pattern2,
                        bool keep_all_offsets,
                        std::vector<std::vector<int32> > *offsets_out) {
  offsets_out->clear();
  std::vector<int32> known_offsets;
  FindOffsetsRecursive(pattern1, pattern2,
                       &known_offsets,
                       keep_all_offsets,
                       pattern1.offset - pattern2.offset,
                       offsets_out);
}


/**
   This recursive function is used to compute the intersection between
   pattern1 and pattern2, which must have identical num_axes and strides,
   must have normalized strides, and must be valid-1.  The user would call
   this with identical_raxis == pattern1.num_axes, and the recursion on
   identical_raxis takes care of the actual implementation.


        @param [in] pattern1   The first input pattern.  Must be valid-1 and
                               have normalized strides.
        @param [in] pattern2   The second input pattern.  Must be valid-1 and
                               have the same num_axes and strides as pattern1.
        @param [in] identical_raxis  Let num_axes be the num_axes of pattern1 or
                               pattern2 (it's the same).  By passing in
                               a particular value of identical_raxis, the caller
                               asserts that for all raxis with
                               identical_raxis <= raxis < num_axes,
                               `pattern1.dim[raxis] == pattern2.dim[raxis]`;
                               and furthermore that the caller is only
                               interested in the part of the overlap for which
                               pattern1 and pattern2 have the same index for all
                               raxis >= identical_raxis (and if there was
                               another part, it has been handled separately).
        @param [in] keep_all_patterns  True if the user actually wants all of
                               the patterns (as opposed to just caring whether
                               any exist).  If false, this function may return
                               early after processing on or more patterns.
        @param [out] patterns_out  The output patterns; this function will
                               append to this location a number (possibly zero)
                               of disjoint valid patterns, each of which is
                               linear in pattern1 and pattern2, the union of whose
                               memory-index-sets is identical to the intersection
                               of pattern1 and pattern2's memory-index-sets.
  */
void ComputeIntersectionRecursive(const TensorPattern &pattern1,
                                  const TensorPattern &pattern2,
                                  int32 identical_raxis,
                                  bool keep_all_patterns,
                                  std::vector<TensorPattern> *patterns_out) {
  if (identical_raxis == 0) {
    /*
      The base-case of the recursion; if we reach here, it means pattern1 and
      pattern2 have identical dims and strides; and if they also have the same
      offset, all we need to do is append one of them to 'patterns_out'
      (otherwise this part of the intersection is empty).  This is all part of a
      process of trying to make the 'offset' identical between the two patterns
      by discarding some leading indexes on one of the two patterns, and
      discarding any trailing indexes as needed to make the dim the same.  (See
      "Index:" in glossary for clarity on its meaning here).
    */

    if (pattern1.offset == pattern2.offset) {
      size_t cur_size = patterns_out->size();
      patterns_out->resize(cur_size + 1);
      RemoveTrivialAxes(pattern1, &(patterns_out[cur_size]));
    }
    return;
  }
  // we'll be modifying the dims and strides on axis 'raxis'.
  int32 raxis = identical_raxis - 1,
      stride = pattern1.strides[raxis]; // will be the same in pattern2, and positive.

  // By the '?..:' statements below we possibly switch pattern2 and
  // pattern1, thereby ensuring that pattern2_mod.offset >= pattern1_mod.offset;
  // this simplifies the later code.
  TensorPattern pattern1_mod(pattern2.offset >= pattern1.offset ? pattern1 : pattern2),
      pattern2_mod(pattern2.offset >= pattern1.offset ? pattern2 : pattern1);


  // pattern2_mod's offset is larger (or the same), so we may need to discard
  // some leading indexes of pattern1_mod (on axis 'raxis'), increasing
  // pattern1_mod's offset and reducing its dim on this raxis, to get the
  // offsets closer to being the same.

  // 'min_dim1_discarded' below will be rounded down in the division, and we will
  // also need to also consider the value that's one larger than that.  We don't
  // need to consider any other values of 'dim1_discarded' other than these two,
  // because it's possible to prove that if we recurse with the remaining offset
  // being greater than 'stride', we would never be able to get to offset=0
  // without discarding all dims of at least one axis numbered less than raxis.
  // The proof requires the axis-dominance property (together with normalized
  // strides).
  int32 offset_diff = pattern2_mod.offset - pattern1_mod.offset,
      min_dim1_discarded = offset_diff / stride,
      max_dim1_discarded = ((offset_diff == min_dim1_discarded * stride) ?
                            min_dim1_discarded : min_dim1_discarded + 1);

  // Make a copy of the relevant dims, and pattern1's offset, because the
  // versions in the patterns may get modified in the loop below.
  int32 pattern1_dim = pattern1_mod.dims[raxis],
      pattern2_dim = pattern2_mod.dims[raxis],
      pattern1_offset = pattern1.offset;
  for (int32 dim1_discarded = min_dim1_discarded;
       dim1_discarded <= max_dim1_discarded; dim1_discarded++) {
    pattern1_mod.offset = pattern1_offset + dim1_discarded * stride;
    int32 new_pattern1_dim = pattern1_dim - dim1_discarded;
    if (new_pattern1_dim <= 0)
      continue;  // There's no overlap here.
    pattern1_mod.dims[raxis] = new_pattern1_dim;
    // set both dims of pattern1_mod and pattern2_mod to the minimum
    // of the two dims.
    if (pattern2_dim > new_pattern1_dim) {
      pattern2_mod.dims[raxis] = new_pattern1_dim;
    } else {
      pattern1_mod.dims[raxis] = pattern2_dim;
      pattern2_mod.dims[raxis] = pattern2_dim;
    }
    // Recurse.
    ComputeIntersectionRecursive(pattern1, pattern2, raxis,
                                 keep_all_patterns, patterns_out);
    if (!keep_all_patterns && !patterns_out->empty())
      return;  // An optimization if we just want to test if intersection is
               // nonempty.
  }
}


// See documentation in header.
bool ComputeIntersection(const TensorPattern &pattern1_in,
                         const TensorPattern &pattern2_in,
                         std::vector<TensorPattern> *intersection,
                         bool keep_all_patterns) {
  TensorPattern pattern1(pattern1_in),
      pattern2(pattern2_in);
  CanonicalizePattern(&pattern1);
  CanonicalizePattern(&pattern2);
  std::vector<int32> strides;
  FindAllStrides(pattern1, pattern2, &strides);
  int32 num_axes = strides.size();
  if (num_axes == 0) {
    // Some of the code below with num_axes - 1 would crash
    // in this case, so handle it separately.
    // Note: for 1-element patterns, if their offsets are
    // different, they don't intersect.
    if (pattern1.offset == pattern2.offset) {
      intersection->resize(1);
      (*intersection)[0] = pattern1;
    } else {
      intersection->clear();
    }
    return true;
  }
  std::vector<TensorPattern> patterns1, patterns2;
  patterns1.reserve(8);
  patterns2.reserve(8);
  intersection->clear();
  if (!ConvertPatternStrides(pattern1, strides, &patterns1) ||
      !ConvertPatternStrides(pattern2, strides, &patterns2))
    return false;

  auto iter1 = patterns1.begin(), end1 = patterns1.end();
  for (; iter1 != end1; ++iter1) {
    Pattern &sub_pattern1 = *iter1;
    auto iter2 = patterns2.begin(), end2 = patterns2.end();

    // Below, 'end_mindex1' is not the actual largest mindex in `sub_pattern1`,
    // but an upper bound on it (in fact, it is strictly greater than it); to
    // prove this we require the axis-dominance property and the fact that the
    // strides are normalized (positive and increasing).  This is part of an
    // optimization to more quickly skip over pairs of patterns that will have
    // empty intersection.
    int64 begin_mindex1 = sub_pattern1.mindex,
        end_mindex1 = begin_mindex1 +
        sub_pattern1.strides[num_axes - 1] * sub_pattern1.dims[num_axes - 1];

    for (; iter2 != end2; ++iter2) {
      Pattern &sub_pattern2 = *iter2;
      int64 min_mindex2 = sub_pattern2.mindex,
          end_mindex2 = min_mindex2 +
          sub_pattern2.strides[num_axes - 1] * sub_pattern2.dims[num_axes - 1];
      if (min_mindex2 >= end_mindex1 || begin_mindex1 >= end_mindex2)
        continue;  //  This is an optimization for efficiency when it's easy to
                   // see that two Patterns won't overlap.

      // Here, sub_pattern1 and sub_pattern2 are the sub-pieces of pattern1 and
      // pattern2 that have been converted to share the same list of strides The
      // following call may add elements to 'intersection'.
      ComputeIntersectionRecursive(sub_pattern1, sub_pattern2,
                                   num_axes,
                                   keep_all_patterns,
                                   intersection);
      if (!keep_all_patterns && !intersection.empty())
        return true;
    }
  }
  return true;
}

bool PatternContains(const TensorPattern &pattern_in,
                     int64 mindex) {
  TensorPattern pattern_mod;
  const Pattern *pattern;
  if (!IsCanonical(pattern_in)) {
    CanonicalizePattern(pattern_in, &pattern_mod);
    pattern = &pattern_mod;
  } else {
    pattern = &pattern_in;
  }
  mindex -= pattern->offset;
  int32 num_axes = pattern->num_axes;
  for (int32 raxis = num_axes - 1; raxis >= 0; raxis--) {
    int32 index = mindex / p->strides[raxis];
    // The following expression returns true if index is outside
    //  range [ 0, p->dims[raxis] - 1 ].
    if (static_cast<uint32>(index) >= static_cast<uint32>(p->dims[raxis]))
      return false;
    mindex -= p->strides[raxis] * index;
  }
  return (mindex == 0);
}



bool ToMemoryIndexSet(const TensorPattern &pattern_in,
                      std::vector<char> *s) {
  KALDI_PARANOID_ASSERT(pattern.IsValid());
  s->clear();
  TensorPattern pattern_mod;
  const Pattern *pattern;
  if (!IsCanonical(pattern_in)) {
    CanonicalizePattern(pattern_in, &pattern_mod);
    pattern = &pattern_mod;
  } else {
    pattern = &pattern_in;
  }
  int32 num_axes = pattern->num_axes;
  if (num_axes == 0)
    num_axes = 1;  // this does the right thing, as there will be dim=1,
                   // stride=0 physically present in the pattern.

  // 'end_mindex' is actually a strict upper bound on the maximum possible
  // memory-index, i.e. it is more than the largest possible memory-index.  We
  // rely on the axis-dominance property and also, thanks to the canonical form,
  // the fact that the strides are normalized (sorted and positive).
  int64 end_mindex = pattern->strides[num_axes - 1] *
      pattern->dims[num_axes - 1];
  s->clear();
  s->resize(end_mindex, static_cast<char>(0));

  auto recursively_set_elements = [pattern] (int32 raxis, int64 mindex) {
    int32 this_stride = pattern->strides[raxis],
         this_dim = pattern->dims[raxis];
    if (raxis == 0) {
      // Base case
      char *c = &((*s)[mindex]);
      for (int32 d = 0; d < this_dim; d++)
        c[d * static_cast<int64>(this_stride)] = static_cast<char>(1);
    } else {
      for (int32 d = 0; d < this_dim; d++)
        recursively_set_elements(raxis - 1, mindex + d * this_stride);
    }
  }
  recursively_set_elements(num_axes - 1, pattern->offset);
}

int64 RandomMemoryIndex(const TensorPattern &pattern) {
  int32 num_axes = pattern.num_axes;
  int64 mindex = pattern.offset;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    mindex += RandInt(0, pattern.dims[raxis] - 1) * pattern.strides[raxis];
  }
  return mindex;
}


bool PatternsIntersectExhaustive(const TensorPattern &pattern1,
                                 const TensorPattern &pattern2) {
}


bool PatternsIntersect(const TensorPattern &pattern1,
                       const TensorPattern &pattern2) {
  KALDI_PARANOID_ASSERT(pattern1.IsValid() && pattern2.IsValid());
  int64 min_mindex1, max_mindex1,
      min_mindex2, max_mindex2;
  ComputeMinAndMaxMindex(pattern1, &min_mindex1, &max_mindex1);
  ComputeMinAndMaxMindex(pattern2, &min_mindex2, &max_mindex2);
  if (min_mindex2 > max_mindex1 ||
      min_mindex1 > max_mindex2)
    return false;

  // The next line is a check to see if one or other of the patterns includes
  // the first element of the other; this much faster than the algorithm for
  // computing pattern intersection.
  if (min_mindex2 >= min_mindex1) {
    if (PatternContains(pattern1, min_mindex2))
      return true;
  } else {
    if (PatternContains(pattern2, min_mindex1))
      return true;
  }

  bool keep_all_patterns = false;  // Settin keep_all_patterns to false sets
                                   // "fast mode", used where we just want to
                                   // see whether the intersection is empty.

  std::vector<TensorPattern> intersection;
  if (ComputeIntersection(pattern1, pattern2, &intersection,
                          keep_all_patterns)) {
    return (!intersection.empty());
  }

  // OK, if we reached here it was not possible to convert both patterns to the
  // same set of strides.  This is not expected to happen in practice for any
  // reasonable program.  Warn.
  static int32 num_warned = 0;
  int32 warn_limit = 10;
  if (num_warned < warn_limit) {
    num_warned++;
    KALDI_WARN << "Testing intersection of patterns that cannot be brought "
        "to common strides.  This will be extremely slow!";
  }

  // Randomly select 10 memory-indexes from the smaller pattern and see if it is
  // in the later pattern; this is faster than the next thing we'll try.
  const int32 num_draws = 10;
  if (NumElements(pattern1) < NumElements(pattern2)) {
    for (int32 i = 0; i < num_draws; i++)
      if (PatternContains(pattern2, RandomMemoryIndex(pattern1)))
        return true;
  } else {
    for (int32 i = 0; i < num_draws; i++)
      if (PatternContains(pattern1, RandomMemoryIndex(pattern2)))
        return true;
  }
  // OK, just try an exhaustive search.  If speed becomes an issue we may find a
  // way to disable the next check, which could be extremely slow for large
  // patterns.
  return PatternsIntersectSlow(pattern1, pattern2);
}



/**
   Offsets, and computing intersection of TensorPatterns.

   Suppose we are computing the intersetion of the memory-index-sets of pattern1
   and pattern2.

   For each memory-index m that is in both pattern1 and pattern2, there must be
   index-tuples i1 and i2 such that pattern1[i1] = pattern2[i2] = m.  We can
   write this as: pattern1[i] = pattern2[i + o], where o is an offset that's
   also a tuple (like an index-tuple, but with possibly negative elements).  This
   function can be thought of as a recursive search for all values of the
   offset 'o' for which at least one such index m exists.  For each such offset
   'o' we might end up with a TensorPattern;  and the union of all of these
   patterns is the intersection of pattern1 and pattern2.

   The algorithm for computing the list of potential offsets o is recursive,
   starting from the last-numbered raxis, which will have the highest
   stride since the strides are normalized.

   Let the vector of strides of the patterns (they're the same) be s.
   from pattern1[i] = pattern2[i + o], we have:
     pattern1.offset + s . i  == pattern2.offset + s . (i + o)
   where a `.` with space around it means dot product.

   Simplifying:
      s . o = pattern1.offset - pattern2.offset.         (1)

   For each raxis r, there are limits on the value of o[r]; these are imposed by
   the dimensions of the two Tensors.  In the equation pattern1[i] = pattern2[i
   + o], for the indexes into the patterns to be valid, i[r] must be in
   [0 .. pattern1.dims[r] - 1] and i[r] + o[r] must be in [0 .. pattern2.dims[r] - 1].
   For such an i[r] to exist, o[r] must be in the range [-(pattern1.dims[r] - 1)
   .. pattern2.dims(r) - 1].

   There is a further limitation on the elements of o that we can obtain
   using the properties above plus the axis-dominance property.  It's easiest
   to explain this if we let r be num_axes - 1, and define:
       l(r) =   \sum_{q < r} s[q] * o[q].
   Here, l(r) represents the sum of the elements in s . o that come from raxes
   lower than r.  We can use the axis-dominance lemma (see tensor-pattern.h)
   and the limitation on o[r] proved in the previous paragraph to prove that:
       -s[r] <  l(r) <  s[r].
   For the last axis r = num_axes - 1, for the equation (1) to hold, we
   must have  l(r) =  pattern1.offset - pattern2.offset - s[r] * o[r],
   so we have the inequality
     -s[r] <  pattern1.offset - pattern2.offset - s[r] * o[r]  <  s[r]
   which means we need only consider offsets o[r] where the absolute value of
   the "remainder" is less than s[r]; there will be at most two.  For an raxis r
   < num_axes - 1, if the offsets for higher-numbered r are already known we
   just subtract the appropriate terms from the remainder too.  The recursive
   implementation that finds the possible offset vectors is pretty obvious
   intuitively.
*/

/**
   This recursive function is used to compute the set-wise difference pattern1 -
   pattern2 where the two patterns must have identical num_axes and strides,
   must have normalized strides, and must be valid-1.  The user would call this
   with identical_raxis == pattern1.num_axes, and the recursion on
   identical_raxis takes care of the actual implementation.

   Notes on how this works and the math behind it:







Since
  pattern1 and pattern2 have the same strides, there will be in many cases
  multiple such pairs of index-tuples (i1, i2) with the same difference


        @param [in] pattern1   The first input pattern.  Must be valid-1 and
                               have normalized strides.
        @param [in] pattern2   The second input pattern.  Must be valid-1 and
                               have the same num_axes and strides as pattern1.
        @param [in] identical_raxis  Let num_axes be the num_axes of pattern1 or
                               pattern2 (it's the same).  By passing in
                               a particular value of identical_raxis, the caller
                               asserts that for all raxis with
                               identical_raxis <= raxis < num_axes,
                               `pattern1.dim[raxis] == pattern2.dim[raxis]`;
                               and furthermore that the caller is only
                               interested in the part of the overlap for which
                               pattern1 and pattern2 have the same index for all
                               raxis >= identical_raxis (and if there was
                               another part, it has been handled separately).
        @param [out] patterns_out  The output patterns; this function will
                               append to this location a number (possibly zero)
                               of disjoint valid patterns, each of which is
                               linear in pattern1 and pattern2, the union of whose
                               memory-index-sets is identical to the difference
                               of pattern1 and pattern2's memory-index-sets.
*/
void ComputeDifferenceRecursive(const TensorPattern &pattern1,
                                const TensorPattern &pattern2,
                                int32 identical_raxis,
                                std::vector<TensorPattern> *patterns_out) {
  if (identical_raxis == 0) {
    /*
      The base-case of the recursion; if we reach here, it means pattern1 and
      pattern2 have identical dims and strides.  If they have different
      offsets, that means they are disjoint and so pattern1 itself is
      the difference; if the offset is the same, they are the same set
      and so we don't need to output anything. */
    if (pattern1.offset != pattern2.offset) {
      size_t cur_size = patterns_out->size();
      patterns_out->resize(cur_size + 1);
      RemoveTrivialAxes(pattern1, &(patterns_out[cur_size]));
    }
    return;
  }
  // we'll be modifying the dims and strides on axis 'raxis'.
  int32 raxis = identical_raxis - 1,
      stride = pattern1.strides[raxis]; // will be the same in pattern2, and positive.


  // pattern2_mod's offset is larger (or the same), so we may need to discard
  // some leading indexes of pattern1_mod (on axis 'raxis'), increasing
  // pattern1_mod's offset and reducing its dim on this raxis, to get the
  // offsets closer to being the same.

  // 'min_dim1_discarded' below will be rounded down in the division, and we will
  // also need to also consider the value that's one larger than that.  We don't
  // need to consider any other values of 'dim1_discarded' other than these two,
  // because it's possible to prove that if we recurse with the remaining offset
  // being greater than 'stride', we would never be able to get to offset=0
  // without discarding all dims of at least one axis numbered less than raxis.
  // The proof requires the axis-dominance property (together with normalized
  // strides).
  int32 offset_diff = pattern2_mod.offset - pattern1_mod.offset,
      min_dim1_discarded = offset_diff / stride,
      max_dim1_discarded = ((offset_diff == min_dim1_discarded * stride) ?
                            min_dim1_discarded : min_dim1_discarded + 1);

  // Make a copy of the relevant dims, and pattern1's offset, because the
  // versions in the patterns may get modified in the loop below.
  int32 pattern1_dim = pattern1_mod.dims[raxis],
      pattern2_dim = pattern2_mod.dims[raxis],
      pattern1_offset = pattern1.offset;
  for (int32 dim1_discarded = min_dim1_discarded;
       dim1_discarded <= max_dim1_discarded; dim1_discarded++) {
    pattern1_mod.offset = pattern1_offset + dim1_discarded * stride;
    int32 new_pattern1_dim = pattern1_dim - dim1_discarded;
    if (new_pattern1_dim <= 0)
      continue;  // There's no overlap here.
    pattern1_mod.dims[raxis] = new_pattern1_dim;
    // set both dims of pattern1_mod and pattern2_mod to the minimum
    // of the two dims.
    if (pattern2_dim > new_pattern1_dim) {
      pattern2_mod.dims[raxis] = new_pattern1_dim;
    } else {
      pattern1_mod.dims[raxis] = pattern2_dim;
      pattern2_mod.dims[raxis] = pattern2_dim;
    }
    // Recurse.
    ComputeIntersectionRecursive(pattern1, pattern2, raxis,
                                 keep_all_patterns, patterns_out);
  }
}


// See documentation in header.
bool ComputeDifference(const TensorPattern &pattern1,
                       const TensorPattern &pattern2,
                       std::vector<TensorPattern> *difference) {
  TensorPattern pattern1(pattern1_in),
      pattern2(pattern2_in);
  CanonicalizePattern(&pattern1);
  CanonicalizePattern(&pattern2);
  std::vector<int32> strides;
  FindAllStrides(pattern1, pattern2, &strides);
  int32 num_axes = strides.size();
  if (num_axes == 0) {
    // Some of the code below with num_axes - 1 would crash
    // in this case, so handle it separately.
    // Note: for 1-element patterns, if their offsets are
    // different, they don't intersect.
    if (pattern1.offset != pattern2.offset) {
      intersection->resize(1);
      (*intersection)[0] = pattern1;
    } else {
      intersection->clear();
    }
    return true;
  }
  std::vector<TensorPattern> patterns1, patterns2;
  patterns1.reserve(8);
  patterns2.reserve(8);
  intersection->clear();
  if (!ConvertPatternStrides(pattern1, strides, &patterns1) ||
      !ConvertPatternStrides(pattern2, strides, &patterns2))
    return false;


  // The algorithm is: first initialize `cur_difference` to
  // pattern1.  Then,
  // For each member p2 of `patterns2`
  //   For each member p of cur_difference
  //      Compute (p - p2), appending the result (as zero or more
  //      patterns) to next_difference.
  //   set cur_difference = next_difference and clear next_difference.
  // Result is in cur_difference.
  std::vector<TensorPattern> cur_difference, next_difference;
  cur_difference.swap(patterns1);

  for (auto iter2 = patterns2.begin(); iter2 != patterns2.end(); ++iter2) {
    const Pattern &sub_pattern2 = *iter2;
    // Below, 'end_mindex1' is not the actual largest mindex in `sub_pattern1`,
    // but an upper bound on it (in fact, it is strictly greater than it); to
    // prove this we require the axis-dominance property and the fact that the
    // strides are normalized (positive and increasing).  This is part of an
    // optimization to more quickly skip over pairs of patterns that will have
    // empty intersection.
    int64 begin_mindex2 = sub_pattern2.offset,
        end_mindex2 = begin_mindex2 +
        sub_pattern2.strides[num_axes - 1] * sub_pattern2.dims[num_axes - 1];

    for (auto iter = cur_difference.begin(); iter != cur_difference.end();
         ++iter){
      const Pattern &sub_pattern1 = *iter;
      // as before, end_mindex1 is strictly greater than the actual largest
      // mindex.
      int64 begin_mindex1 = sub_pattern1.offset,
          end_mindex1 = begin_mindex1 +
          sub_pattern1.strides[num_axes - 1] * sub_pattern1.dims[num_axes - 1];

      if (begin_mindex2 >= end_mindex1 || begin_mindex1 >= end_mindex2) {
        //  This is an optimization for efficiency when it's easy to
        // see that two Patterns won't overlap.  In this case
        // we don't subtract anything from sub_pattern1.
        next_difference.push_back(sub_pattern1);
        continue;
      }

      // Here, sub_pattern1 and sub_pattern2 are the sub-pieces of pattern1 and
      // pattern2 that have been converted to share the same list of strides The
      // following call may add elements to 'difference'.
      ComputeDifferenceRecursive(sub_pattern1, sub_pattern2,
                                 num_axes,
                                 &next_difference);
    }
    cur_difference.swap(next_difference);
    next_difference.clear;
  }
  // output to the user-supplied vector `difference`.
  difference->swap(cur_difference);
  return true;
}


bool PatternsIntersectSlow(const TensorPattern &pattern1_in,
                           const TensorPattern &pattern2_in) {
  TensorPattern pattern1(pattern1_in),
      pattern2(pattern2_in);
  Canonicalize(&pattern1);
  Canonicalize(&pattern2);
  // Note: the offsets are the minimum elements, now that the
  // patterns are canonical.
  int64 min_offset = std::min(pattern1.offset, pattern2.offset);
  pattern1.offset -= min_offset;
  pattern2.offset -= min_offset;
  int64 max_offset = std::max(pattern1.offset, pattern2.offset);
  // Explicitly get the memory-index-set of pattern1 and pattern2
  // as possibly-huge arrays, and see if they intersect.  Obviously
  // this will be extremely slow.
  std::vector<char> pattern1_mindexes, pattern2_mindexes;
  ToMemoryIndexSet(pattern1, &pattern1_mindexes);
  ToMemoryIndexSet(pattern2, &pattern2_mindexes);
  auto iter1 = pattern1_mindexes.begin() + max_offset,
      iter2 = pattern2_mindexes.begin() + max_offset;
  for (; iter1 != pattern1_mindexes.begin() &&
           iter2 != pattern2_mindexes.end();
       ++iter1, ++iter2) {
    if (*iter1 && *iter2)
      return true;
  }
  return false;
}


bool TensorPatternRebaser::Convert(TensorPattern *pattern) {
  if (!needs_conversion_)
    return;  // An optimization to make the common case fast.

  pattern->offset = ConvertMemoryIndex(pattern->offset);

  if (num-axes_ == 0)
    return;  // Another optimization to make a fairly common case fast.
  int32 num_axes = pattern->num_axes;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 stride = pattern->strides[raxis],
        dim = pattern->dims[raxis];
    if (stride == 0)
      continue;
    int32 pstride = std::abs(stride),
        product = pstride * dim;
    // We will convert 'pstride' using


  }
  return true;  // Success

}


int64 TensorPatternRebaser::ConvertMemoryIndex(int64 m) {
  int32 num_axes = num_axes_;
  int64 ans = dest_offset_;
  m -= src_offset_;
  if (num_axes == 0)
    return m;
  // We visit the compressed axes in order from greatest to least src_stride.
  // What this loop does is to reverse engineer the indexes into (the compressed
  // version of) src_pattern that we'd need to get memory-offset m.  The 'i'
  // values in the loop are those indexes.
  for (int32 raxis = num_axes - 1; raxis >= 0; raxis--) {
    int32 stride = src_strides_[raxis];
    int64 i = m / stride;
    m -= i * stride;
    ans += i * dest_strides_[raxis]
  }
  if (m != 0) {
    // This should not happen; likely it means the memory-index m was not covered
    // by the src_pattern passed to the constructor, so someone was trying
    // to rebase a pattern which was not covered by src_pattern.
    KALDI_ERR << "Could not convert this memory-index (likely code error)";
  }
  return ans;
}


// Note on implementation: likely the most common case we'll call this
// is when -DKALDI_PARANOID has been set and we are checking that
// tensors we are rebasing are strictly inside the source tensor.
// So in the common case, pattern1 *will* include pattern2.
bool PatternIncludes(const TensorPattern &pattern1,
                     const TensorPattern &pattern2) {

  std::vector<TensorPattern> intersection;
  if (!ComputeIntersection(pattern1, pattern2, &intersection))
    return -1;  // Could not determine whether the patterns intersect.
  int64 num_elements = 0;
  for (auto pattern : intersection)
    num_elements += NumElements(pattern);
  if (num_elements == NumElements(pattern1))
    return 1;  // pattern1 includes pattern2;
  else
    return 0;  // pattern1 does not include pattern2
}


void MakeCompactAndJustified(const TensorPattern &src,
                             TensorPattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  // The sorter object provides an order in which we can visit the axes of 'src'
  // that is from least to greatest abs(stride).
  OutOfPlaceAxisSorter sorter(src);

  int64 offset = 0;  // 'offset' will be the offset that ensures 'dest' is
                     // justified (means lowest memory-index is 0).
  int32 next_abs_stride = 1;
  for (int32 i = 0; i < num_axes; i++) {
    int32 raxis = sorter.GetIndex(i);
    // We are going through the raxis-indexes in increasing order of stride.
    // We'll set each stride to the product of the preceding dims.
    int32 this_stride = src.strides[raxis],
        this_dim = src.dims[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // Note: if 'src' is valid, this implies the dim is 1,
      // so no need to multiply 'next_stride'
    } else {
      int32 abs_stride = std::abs(this_stride);
      KALDI_PARANOID_ASSERT(abs_stride >= next_abs_stride &&
                            "Input pattern was not valid.");
      if (this_stride < 0) {
        offset += next_stride * (this_dim - 1);
        dest->strides[raxis] = -next_abs_stride;
      } else {
        dest->strides[raxis] = next_abs_stride;
      }
      next_abs_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = offset;
  SetDefaultCodeAndProperties(dest);

  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}


void MakeCompactNonnegativeAndJustified(const TensorPattern &src,
                                        TensorPattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  // The sorter object provides an order in which we can visit the axes of 'src'
  // that is from least to greatest abs(stride).
  OutOfPlaceAxisSorter sorter(src);

  int32 next_stride = 1;
  for (int32 i = 0; i < num_axes; i++) {
    int32 raxis = sorter.GetIndex(i);
    // We are going through the raxis-indexes in increasing order of stride.
    // We'll set each stride to the product of the preceding dims.
    int32 this_stride = src.strides[raxis],
        this_dim = src.dims[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // Note: if 'src' is valid, this implies the dim is 1,
      // so no need to multiply 'next_stride'
    } else {
      dest->strides[raxis] = next_stride;
      next_abs_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = 0;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        HasNonnegativeStrides(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}



void MakeCompactNormalizedAndJustified(const TensorPattern &src,
                                       TensorPattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  int32 next_stride = 1;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 this_dim = src.dims[raxis],
        this_stride = src.strides[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // no need to multiply next_stride by dim, since it must be 1.
    } else {
      dest->strides[raxis] = next_stride;
      next_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = 0;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        HasNormalizedStrides(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}





}  // namespace kaldi
}  // namespace tensor