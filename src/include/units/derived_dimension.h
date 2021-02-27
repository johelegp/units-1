// The MIT License (MIT)
//
// Copyright (c) 2018 Mateusz Pusz
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <variant>
#include <boost/mp11/algorithm.hpp>
#include <units/base_dimension.h>
#include <units/bits/base_units_ratio.h>
#include <units/bits/derived_dimension_base.h>
#include <units/bits/dim_consolidate.h>
#include <units/bits/dim_unpack.h>
#include <units/bits/external/downcasting.h>
#include <units/bits/external/type_list.h>
#include <units/exponent.h>

namespace units {

namespace detail {

template<Exponent... Es>
constexpr auto sort(exponent_list<Es...>) {
  if constexpr (sizeof...(Es) == 0)
    return exponent_list<>{};
  else
    return []<std::size_t... I>(std::index_sequence<I...>) {
      constexpr std::array indexed_symbols = [] {
        std::array is{std::pair{I, Es::dimension::symbol.to_string_view()}...};
        std::ranges::sort(is, {}, &decltype(is)::value_type::second);
        return is;
      }();
      return exponent_list<boost::mp11::mp_at_c<exponent_list<Es...>, indexed_symbols[I].first>...>{};
    }(std::index_sequence_for<Es...>{});
} /* Goal:
constexpr std::vector<exponents> sort(std::vector<exponents> exps) {
  std::ranges::sort(exps, {}, &dimension::symbol, &exponent::dimension);
  return exps;
} */

template<Exponent... Es>
constexpr auto consolidate_dimensions(exponent_list<Es...>) {
  if constexpr (sizeof...(Es) == 0)
    return exponent_list<>{};
  else {
    using V = boost::mp11::mp_unique<std::variant<std::monostate, typename Es::dimension...>>;
    struct exp {
      V dimension;
      ratio r;
    };
    struct consolidated_dims {
      std::array<exp, sizeof...(Es)> exps;
      std::ptrdiff_t size;
    };
    constexpr consolidated_dims cdims = [] {
      constexpr std::array exponents{exp{V{typename Es::dimension{}}, ratio{Es::num, Es::den}}...};
      std::array consolidated_buffer{(typename Es::dimension{}, exp{{}, ratio{0}})...};
      auto consolidated_first{consolidated_buffer.begin()};
      for (const auto& e : exponents) {
        if (std::get_if<std::monostate>(&consolidated_first->dimension))
          *consolidated_first = e;
        else if (consolidated_first->dimension.index() == e.dimension.index())
          consolidated_first->r += e.r;
        else
          *++consolidated_first = e;
      }
      consolidated_first = std::ranges::remove_if(
          consolidated_buffer.begin(), ++consolidated_first, std::not_fn(&ratio::num), &exp::r).begin();
      return consolidated_dims{consolidated_buffer, consolidated_first - consolidated_buffer.begin()};
    }();
    return [&]<std::size_t... CI>(std::index_sequence<CI...>) {
      return exponent_list<exponent<boost::mp11::mp_at_c<V, cdims.exps[CI].dimension.index()>,
                           cdims.exps[CI].r.num, cdims.exps[CI].r.den>...>{};
    }(std::make_index_sequence<cdims.size>{});
  }
} /* Goal:
constexpr std::vector<exponents> consolidate(std::vector<exponents> exps) {
  return >>exps |> group_by(&exponent::dimension) |> accumulate(&exponents::r);
} */

/**
 * @brief Converts user provided derived dimension specification into a valid units::derived_dimension_base definition
 * 
 * User provided definition of a derived dimension may contain the same base dimension repeated more than once on the
 * list possibly hidden in other derived units provided by the user. The process here should:
 * 1. Extract derived dimensions into exponents of base dimensions.
 * 2. Sort the exponents so the same dimensions are placed next to each other.
 * 3. Consolidate contiguous range of exponents of the same base dimensions to a one (or possibly zero) exponent for
 *    this base dimension.
 */
template<Exponent... Es>
using make_dimension = TYPENAME to_derived_dimension_base<decltype(consolidate_dimensions(sort(typename dim_unpack<Es...>::type{})))>::type;

}  // namespace detail

/**
 * @brief The list of exponents of dimensions (both base and derived) provided by the user
 * 
 * This is the user's interface to create derived dimensions. Exponents list can contain powers of factors of both
 * base and derived dimensions. This is called a "recipe" of the dimension and among others is used to print
 * unnamed coherent units of this dimension.
 * 
 * Coherent unit is a unit that, for a given system of quantities and for a chosen set of base units, is a product
 * of powers of base units with no other proportionality factor than one.
 *
 * The implementation is responsible for unpacking all of the dimensions into a list containing only base dimensions
 * and their factors and putting them to derived_dimension_base class template.
 * 
 * Sometimes units of equivalent quantities in different systems of units do not share the same reference so they
 * cannot be easily converted to each other. An example can be a pressure for which a coherent unit in SI is pascal
 * and in CGS barye. Those two units are not directly related with each other with some ratio. As they both are
 * coherent units of their dimensions, the ratio between them is directly determined by the ratios of base units
 * defined in base dimensions end their exponents in the derived dimension recipe. To provide interoperability of
 * such quantities of different systems base_units_ratio is being used. The result of the division of two
 * base_units_ratio of two quantities of equivalent dimensions in two different systems gives a ratio between their
 * coherent units. Alternatively, the user would always have to directly define a barye in terms of pascal or vice
 * versa.
 * 
 * @tparam Child inherited class type used by the downcasting facility (CRTP Idiom)
 * @tparam U a coherent unit of a derived dimension
 * @tparam Es the list of exponents of ingredient dimensions
 */
template<typename Child, Unit U, Exponent... Es>
struct derived_dimension : downcast_dispatch<Child, typename detail::make_dimension<Es...>> {
  using recipe = exponent_list<Es...>;
  using coherent_unit = U;
  static constexpr ratio base_units_ratio = detail::base_units_ratio(typename derived_dimension::exponents());
};

}  // namespace units
