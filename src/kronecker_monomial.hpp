/***************************************************************************
 *   Copyright (C) 2009-2011 by Francesco Biscani                          *
 *   bluescarni@gmail.com                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef PIRANHA_KRONECKER_MONOMIAL_HPP
#define PIRANHA_KRONECKER_MONOMIAL_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.hpp"
#include "detail/km_commons.hpp"
#include "detail/prepare_for_print.hpp"
#include "exceptions.hpp"
#include "is_cf.hpp"
#include "is_key.hpp"
#include "kronecker_array.hpp"
#include "math.hpp"
#include "mp_integer.hpp"
#include "mp_rational.hpp"
#include "pow.hpp"
#include "safe_cast.hpp"
#include "serialization.hpp"
#include "static_vector.hpp"
#include "symbol_set.hpp"
#include "symbol.hpp"
#include "term.hpp"
#include "type_traits.hpp"

namespace piranha
{

/// Kronecker monomial class.
/**
 * This class represents a multivariate monomial with integral exponents.
 * The values of the exponents are packed in a signed integer using Kronecker substitution, using the facilities provided
 * by piranha::kronecker_array.
 * 
 * This class satisfies the piranha::is_key, piranha::key_has_degree, piranha::key_has_ldegree and piranha::key_is_differentiable type traits.
 * 
 * ## Type requirements ##
 * 
 * \p T must be suitable for use in piranha::kronecker_array. The default type for \p T is the signed counterpart of \p std::size_t.
 * 
 * ## Exception safety guarantee ##
 * 
 * Unless otherwise specified, this class provides the strong exception safety guarantee for all operations.
 * 
 * ## Move semantics ##
 * 
 * The move semantics of this class are equivalent to the move semantics of C++ signed integral types.
 *
 * ## Serialization ##
 *
 * This class supports serialization.
 * 
 * @author Francesco Biscani (bluescarni@gmail.com)
 */
// TODO:
// - consider abstracting the km_commons in a class and use it both here and in rtkm.
// - needs sfinaeing in sub, integrate, and other methods not generalised yet.
// - review the use of a max_size for the static vector used in unpacking: it's probably too big.
//   Also we should probably check how is it used in performance critical parts, i.e., check
//   if we are not copying around too much data on the stack.
template <typename T = std::make_signed<std::size_t>::type>
class kronecker_monomial
{
	public:
		/// Alias for \p T.
		typedef T value_type;
	private:
		typedef kronecker_array<value_type> ka;
	public:
		/// Size type.
		/**
		 * Used to represent the number of variables in the monomial. Equivalent to the size type of
		 * piranha::kronecker_array.
		 */
		typedef typename ka::size_type size_type;
		/// Maximum monomial size.
		static const size_type max_size = 255u;
	private:
#if !defined(PIRANHA_DOXYGEN_INVOKED)
		static_assert(max_size <= std::numeric_limits<static_vector<int,1u>::size_type>::max(),"Invalid max size.");
		// Eval and sub typedef.
		template <typename U, typename = void>
		struct eval_type_ {};
		template <typename U>
		using e_type = decltype(math::pow(std::declval<U const &>(),std::declval<value_type const &>()));
		template <typename U>
		struct eval_type_<U,typename std::enable_if<is_multipliable_in_place<e_type<U>>::value &&
			std::is_constructible<e_type<U>,int>::value && detail::is_pmappable<U>::value>::type>
		{
			using type = e_type<U>;
		};
		// The final typedef.
		template <typename U>
		using eval_type = typename eval_type_<U>::type;
		// Enabler for pow.
		template <typename U>
		using pow_enabler = typename std::enable_if<has_safe_cast<T,
			decltype(std::declval<integer &&>() * std::declval<const U &>())>::value,int>::type;
		// Serialization support.
		friend class boost::serialization::access;
		template <typename Archive>
		void serialize(Archive &ar, unsigned int)
		{
			ar & m_value;
		}
		// Enabler for ctor from init list.
		template <typename U>
		using init_list_enabler = typename std::enable_if<has_safe_cast<value_type,U>::value,int>::type;
		// Enabler for ctor from iterator.
		template <typename Iterator>
		using it_ctor_enabler = typename std::enable_if<is_input_iterator<Iterator>::value &&
			has_safe_cast<value_type,decltype(*std::declval<Iterator &>())>::value,int>::type;
		// Enabler for multiply().
		template <typename Cf>
		using multiply_enabler = typename std::enable_if<std::is_same<Cf,decltype(std::declval<const Cf &>() * std::declval<const Cf &>())>::value &&
			is_cf<Cf>::value,int>::type;
		// Subs utilities.
		template <typename U>
		using subs_type__ = decltype(math::pow(std::declval<const U &>(),std::declval<const value_type &>()));
		template <typename U, typename = void>
		struct subs_type_
		{};
		template <typename U>
		struct subs_type_<U,typename std::enable_if<std::is_constructible<subs_type__<U>,int>::value &&
			std::is_assignable<subs_type__<U> &,subs_type__<U>>::value>::type>
		{
			using type = subs_type__<U>;
		};
		template <typename U>
		using subs_type = typename subs_type_<U>::type;
		// ipow subs utilities.
		template <typename U>
		using ipow_subs_type__ = decltype(math::pow(std::declval<const U &>(),std::declval<const integer &>()));
		template <typename U, typename = void>
		struct ipow_subs_type_
		{};
		template <typename U>
		struct ipow_subs_type_<U,typename std::enable_if<
			std::is_constructible<ipow_subs_type__<U>,int>::value && std::is_assignable<ipow_subs_type__<U> &,ipow_subs_type__<U>>::value
			>::type>
		{
			using type = ipow_subs_type__<U>;
		};
		template <typename U>
		using ipow_subs_type = typename ipow_subs_type_<U>::type;
#endif
	public:
		/// Arity of the multiply() method.
		static const std::size_t multiply_arity = 1u;
		/// Vector type used for temporary packing/unpacking.
		typedef static_vector<value_type,max_size> v_type;
		/// Default constructor.
		/**
		 * After construction all exponents in the monomial will be zero.
		 */
		kronecker_monomial():m_value(0) {}
		/// Defaulted copy constructor.
		kronecker_monomial(const kronecker_monomial &) = default;
		/// Defaulted move constructor.
		kronecker_monomial(kronecker_monomial &&) = default;
		/// Constructor from initalizer list.
		/**
		 * \note
		 * This constructor is enabled only if \p U can be cast safely to \p T.
		 *
		 * The values in the initializer list are intended to represent the exponents of the monomial:
		 * they will be converted to type \p T (if \p T and \p U are not the same type),
		 * encoded using piranha::kronecker_array::encode() and the result assigned to the internal integer instance.
		 * 
		 * @param[in] list initializer list representing the exponents.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - piranha::kronecker_array::encode(),
		 * - piranha::safe_cast() (in case \p U is not the same as \p T),
		 * - piranha::static_vector::push_back().
		 */
		template <typename U, init_list_enabler<U> = 0>
		explicit kronecker_monomial(std::initializer_list<U> list):m_value(0)
		{
			v_type tmp;
			for (const auto &x: list) {
				tmp.push_back(safe_cast<value_type>(x));
			}
			m_value = ka::encode(tmp);
		}
		/// Constructor from range.
		/**
		 * \note
		 * This constructor is enabled only if \p Iterator is an input iterator whose value type
		 * is convertible to \p T.
		 *
		 * Will build internally a vector of values from the input iterators, encode it and assign the result
		 * to the internal integer instance. The value type of the iterator is converted to \p T using
		 * piranha::safe_cast().
		 * 
		 * @param[in] start beginning of the range.
		 * @param[in] end end of the range.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - piranha::kronecker_array::encode(),
		 * - piranha::safe_cast() (in case the value type of \p Iterator is not the same as \p T),
		 * - piranha::static_vector::push_back().
		 */
		template <typename Iterator, it_ctor_enabler<Iterator> = 0>
		explicit kronecker_monomial(const Iterator &start, const Iterator &end):m_value(0)
		{
			typedef typename std::iterator_traits<Iterator>::value_type it_v_type;
			v_type tmp;
			std::transform(start,end,std::back_inserter(tmp),[](const it_v_type &v) {return safe_cast<value_type>(v);});
			m_value = ka::encode(tmp);
		}
		/// Constructor from set of symbols.
		/**
		 * After construction all exponents in the monomial will be zero.
		 * 
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - piranha::kronecker_array::encode(),
		 * - piranha::static_vector::push_back().
		 */
		explicit kronecker_monomial(const symbol_set &args)
		{
			v_type tmp;
			for (auto it = args.begin(); it != args.end(); ++it) {
				tmp.push_back(value_type(0));
			}
			m_value = ka::encode(tmp);
		}
		/// Converting constructor.
		/**
		 * This constructor is for use when converting from one term type to another in piranha::series. It will
		 * set the internal integer instance to the same value of \p other, after having checked that
		 * \p other is compatible with \p args.
		 * 
		 * @param[in] other construction argument.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @throws std::invalid_argument if \p other is not compatible with \p args.
		 */
		explicit kronecker_monomial(const kronecker_monomial &other, const symbol_set &args):m_value(other.m_value)
		{
			if (unlikely(!other.is_compatible(args))) {
				piranha_throw(std::invalid_argument,"incompatible arguments");
			}
		}
		/// Constructor from \p value_type.
		/**
		 * This constructor will initialise the internal integer instance
		 * to \p n.
		 * 
		 * @param[in] n initializer for the internal integer instance.
		 */
		explicit kronecker_monomial(const value_type &n):m_value(n) {}
		/// Trivial destructor.
		~kronecker_monomial()
		{
			PIRANHA_TT_CHECK(is_key,kronecker_monomial);
			PIRANHA_TT_CHECK(key_has_degree,kronecker_monomial);
			PIRANHA_TT_CHECK(key_has_ldegree,kronecker_monomial);
			PIRANHA_TT_CHECK(key_is_differentiable,kronecker_monomial);
		}
		/// Defaulted copy assignment operator.
		kronecker_monomial &operator=(const kronecker_monomial &) = default;
		/// Defaulted move assignment operator.
		kronecker_monomial &operator=(kronecker_monomial &&) = default;
		/// Set the internal integer instance.
		/**
		 * @param[in] n value to which the internal integer instance will be set.
		 */
		void set_int(const value_type &n)
		{
			m_value = n;
		}
		/// Get internal instance.
		/**
		 * @return value of the internal integer instance.
		 */
		value_type get_int() const
		{
			return m_value;
		}
		/// Compatibility check.
		/**
		 * Monomial is considered incompatible if any of these conditions holds:
		 * 
		 * - the size of \p args is zero and the internal integer is not zero,
		 * - the size of \p args is equal to or larger than the size of the output of piranha::kronecker_array::get_limits(),
		 * - the internal integer is not within the limits reported by piranha::kronecker_array::get_limits().
		 * 
		 * Otherwise, the monomial is considered to be compatible for insertion.
		 * 
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return compatibility flag for the monomial.
		 */
		bool is_compatible(const symbol_set &args) const noexcept
		{
			// NOTE: the idea here is to avoid unpack()ing for performance reasons: these checks
			// are already part of unpack(), and that's why unpack() is used instead of is_compatible()
			// in other methods.
			const auto s = args.size();
			// No args means the value must also be zero.
			if (s == 0u) {
				return !m_value;
			}
			const auto &limits = ka::get_limits();
			// If we overflow the maximum size available, we cannot use this object as key in series.
			if (s >= limits.size()) {
				return false;
			}
			const auto &l = limits[s];
			// Value is compatible if it is within the bounds for the given size.
			return (m_value >= std::get<1u>(l) && m_value <= std::get<2u>(l));
		}
		/// Ignorability check.
		/**
		 * A monomial is never considered ignorable.
		 * 
		 * @return \p false.
		 */
		bool is_ignorable(const symbol_set &) const noexcept
		{
			return false;
		}
		/// Merge arguments.
		/**
		 * Merge the new arguments set \p new_args into \p this, given the current reference arguments set
		 * \p orig_args.
		 * 
		 * @param[in] orig_args original arguments set.
		 * @param[in] new_args new arguments set.
		 * 
		 * @return monomial with merged arguments.
		 * 
		 * @throws std::invalid_argument if at least one of these conditions is true:
		 * - the size of \p new_args is not greater than the size of \p orig_args,
		 * - not all elements of \p orig_args are included in \p new_args.
		 * @throws unspecified any exception thrown by:
		 * - piranha::kronecker_array::encode(),
		 * - piranha::static_vector::push_back(),
		 * - unpack().
		 */
		kronecker_monomial merge_args(const symbol_set &orig_args, const symbol_set &new_args) const
		{
			return kronecker_monomial(detail::km_merge_args<v_type,ka>(orig_args,new_args,m_value));
		}
		/// Check if monomial is unitary.
		/**
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return \p true if all the exponents are zero, \p false otherwise.
		 * 
		 * @throws std::invalid_argument if \p this is not compatible with \p args.
		 */
		bool is_unitary(const symbol_set &args) const
		{
			if (unlikely(!is_compatible(args))) {
				piranha_throw(std::invalid_argument,"invalid symbol set");
			}
			// A kronecker code will be zero if all components are zero.
			return !m_value;
		}
		/// Degree.
		/**
		 * @param[in] args reference set of symbols.
		 *
		 * @return degree of the monomial.
		 *
		 * @throws unspecified any exception thrown by unpack() or by the in-place addition
		 * operator of piranha::integer.
		 */
		integer degree(const symbol_set &args) const
		{
			const auto tmp = unpack(args);
			// NOTE: this should be guaranteed by the unpack function.
			piranha_assert(tmp.size() == args.size());
			integer retval(0);
			for (const auto &x: tmp) {
				retval += x;
			}
			return retval;
		}
		/// Low degree (equivalent to the degree).
		integer ldegree(const symbol_set &args) const
		{
			return degree(args);
		}
		/// Partial degree.
		/**
		 * Partial degree of the monomial: only the symbols at the positions specified by \p p are considered.
		 *
		 * @param[in] p positions of the symbols to be considered in the calculation of the degree.
		 * @param[in] args reference set of piranha::symbol.
		 *
		 * @return the summation of the exponents of the monomial at the positions specified by \p p.
		 *
		 * @throws std::invalid_argument if \p p is not compatible with \p args.
		 * @throws unspecified any exception thrown by unpack() or by the in-place addition
		 * operator of piranha::integer.
		 */
		integer degree(const symbol_set::positions &p, const symbol_set &args) const
		{
			const auto tmp = unpack(args);
			piranha_assert(tmp.size() == args.size());
			if (unlikely(p.size() && p.back() >= tmp.size())) {
				piranha_throw(std::invalid_argument,"invalid positions");
			}
			auto cit = tmp.begin();
			integer retval(0);
			for (const auto &i: p) {
				retval += cit[i];
			}
			return retval;
		}
		/// Partial low degree (equivalent to the partial degree).
		integer ldegree(const symbol_set::positions &p, const symbol_set &args) const
		{
			return degree(p,args);
		}
		/// Multiply terms with a Kronecker monomial key.
		/**
		 * \note
		 * This method is enabled only if \p Cf satisfies piranha::is_cf and it is multipliable, yielding a result
		 * of type \p Cf.
		 *
		 * Multiply \p t1 by \p t2, storing the result in the only element of \p res. This method
		 * offers the basic exception safety guarantee.
		 *
		 * @param[out] res return value.
		 * @param[in] t1 first argument.
		 * @param[in] t2 second argument.
		 * @param[in] args reference set of arguments.
		 *
		 * @throws std::overflow_error if the computation of the result overflows type \p value_type.
		 * @throws unspecified any exception thrown by:
		 * - the multiplication of the coefficients,
		 * - piranha::kronecker_array::encode(),
		 * - unpack(),
		 * - piranha::static_vector::push_back().
		 */
		template <typename Cf, multiply_enabler<Cf> = 0>
		static void multiply(std::array<term<Cf,kronecker_monomial>,multiply_arity> &res, const term<Cf,kronecker_monomial> &t1,
			const term<Cf,kronecker_monomial> &t2, const symbol_set &args)
		{
			// NOTE: this method is actually never called as the specialised Kronecker polynomial
			// multiplier does not use it. It is here for completeness and because the series multiplier
			// for k polynomial currently inherits from the base multiplier, which needs this.
			// Coefficient first.
			res[0u].m_cf = t1.m_cf * t2.m_cf;
			// Now the key.
			const auto size = args.size();
			const auto tmp1 = t1.m_key.unpack(args), tmp2 = t2.m_key.unpack(args);
			v_type result;
			for (typename v_type::size_type i = 0u; i < size; ++i) {
				result.push_back(tmp1[i]);
				detail::km_safe_adder(result[i],tmp2[i]);
			}
			res[0u].m_key.m_value = ka::encode(result);
		}
		/// Hash value.
		/**
		 * @return the internal integer instance, cast to \p std::size_t.
		 */
		std::size_t hash() const
		{
			return static_cast<std::size_t>(m_value);
		}
		/// Equality operator.
		/**
		 * @param[in] other comparison argument.
		 * 
		 * @return \p true if the internal integral instance of \p this is equal to the integral instance of \p other,
		 * \p false otherwise.
		 */
		bool operator==(const kronecker_monomial &other) const
		{
			return m_value == other.m_value;
		}
		/// Inequality operator.
		/**
		 * @param[in] other comparison argument.
		 * 
		 * @return the opposite of operator==().
		 */
		bool operator!=(const kronecker_monomial &other) const
		{
			return m_value != other.m_value;
		}
		/// Name of the linear argument.
		/**
		 * If the monomial is linear in a variable (i.e., all exponents are zero apart from a single unitary
		 * exponent), the name of the variable will be returned. Otherwise, an error will be raised.
		 * 
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return name of the linear variable.
		 * 
		 * @throws std::invalid_argument if the monomial is not linear.
		 * @throws unspecified any exception thrown by unpack().
		 */
		std::string linear_argument(const symbol_set &args) const
		{
			const auto v = unpack(args);
			const auto size = args.size();
			decltype(args.size()) n_linear = 0u, candidate = 0u;
			for (typename v_type::size_type i = 0u; i < size; ++i) {
				integer tmp;
				try {
					tmp = safe_cast<integer>(v[i]);
				} catch (const std::invalid_argument &) {
					piranha_throw(std::invalid_argument,"exponent is not an integer");
				}
				if (tmp == 0) {
					continue;
				}
				if (tmp != 1) {
					piranha_throw(std::invalid_argument,"exponent is not unitary");
				}
				candidate = i;
				++n_linear;
			}
			if (n_linear != 1u) {
				piranha_throw(std::invalid_argument,"monomial is not linear");
			}
			return args[static_cast<decltype(args.size())>(candidate)].get_name();
		}
		/// Exponentiation.
		/**
		 * \note
		 * This method is enabled only if \p U is multipliable by piranha::integer and the result type can be
		 * safely cast back to \p T.
		 *
		 * Will return a monomial corresponding to \p this raised to the <tt>x</tt>-th power. The exponentiation
		 * is computed via the multiplication of the exponents promoted to piranha::integer by \p x. The result will
		 * be cast back to \p T via piranha::safe_cast().
		 * 
		 * @param[in] x exponent.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return \p this to the power of \p x.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - piranha::safe_cast(),
		 * - the constructor and multiplication operator of piranha::integer,
		 * - piranha::kronecker_array::encode().
		 */
		template <typename U, pow_enabler<U> = 0>
		kronecker_monomial pow(const U &x, const symbol_set &args) const
		{
			auto v = unpack(args);
			for (auto &n: v) {
				n = safe_cast<value_type>(integer(n) * x);
			}
			kronecker_monomial retval;
			retval.m_value = ka::encode(v);
			return retval;
		}
		/// Unpack internal integer instance.
		/**
		 * Will decode the internal integral instance into a piranha::static_vector of size equal to the size of \p args.
		 * 
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return piranha::static_vector containing the result of decoding the internal integral instance via
		 * piranha::kronecker_array.
		 * 
		 * @throws std::invalid_argument if the size of \p args is larger than the maximum size of piranha::static_vector.
		 * @throws unspecified any exception thrown by piranha::kronecker_array::decode().
		 */
		v_type unpack(const symbol_set &args) const
		{
			return detail::km_unpack<v_type,ka>(args,m_value);
		}
		/// Print.
		/**
		 * Will print to stream a human-readable representation of the monomial.
		 * 
		 * @param[in] os target stream.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @throws unspecified any exception thrown by unpack() or by streaming instances of \p value_type.
		 */
		void print(std::ostream &os, const symbol_set &args) const
		{
			const auto tmp = unpack(args);
			piranha_assert(tmp.size() == args.size());
			const value_type zero(0), one(1);
			bool empty_output = true;
			for (decltype(tmp.size()) i = 0u; i < tmp.size(); ++i) {
				if (tmp[i] != zero) {
					if (!empty_output) {
						os << '*';
					}
					os << args[i].get_name();
					empty_output = false;
					if (tmp[i] != one) {
						os << "**" << detail::prepare_for_print(tmp[i]);
					}
				}
			}
		}
		/// Print in TeX mode.
		/**
		 * Will print to stream a TeX representation of the monomial.
		 * 
		 * @param[in] os target stream.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @throws unspecified any exception thrown by unpack() or by streaming instances of \p value_type.
		 */
		void print_tex(std::ostream &os, const symbol_set &args) const
		{
			const auto tmp = unpack(args);
			std::ostringstream oss_num, oss_den, *cur_oss;
			const value_type zero(0), one(1);
			value_type cur_value;
			for (decltype(tmp.size()) i = 0u; i < tmp.size(); ++i) {
				cur_value = tmp[i];
				if (cur_value != zero) {
					// NOTE: here negate() is safe because of the symmetry in kronecker_array.
					cur_oss = (cur_value > zero) ? std::addressof(oss_num) : (math::negate(cur_value),std::addressof(oss_den));
					(*cur_oss) << "{" << args[i].get_name() << "}";
					if (cur_value != one) {
						(*cur_oss) << "^{" << static_cast<long long>(cur_value) << "}";
					}
				}
			}
			const std::string num_str = oss_num.str(), den_str = oss_den.str();
			if (!num_str.empty() && !den_str.empty()) {
				os << "\\frac{" << num_str << "}{" << den_str << "}";
			} else if (!num_str.empty() && den_str.empty()) {
				os << num_str;
			} else if (num_str.empty() && !den_str.empty()) {
				os << "\\frac{1}{" << den_str << "}";
			}
		}
		/// Partial derivative.
		/**
		 * This method will return the partial derivative of \p this with respect to the symbol at the position indicated by \p p.
		 * The result is a pair consisting of the exponent associated to \p p before differentiation and the monomial itself
		 * after differentiation. If \p p is empty or if the exponent associated to it is zero,
		 * the returned pair will be <tt>(0,kronecker_monomial{args})</tt>.
		 *
		 * @param[in] p position of the symbol with respect to which the differentiation will be calculated.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return result of the differentiation.
		 * 
		 * @throws std::invalid_argument if the computation of the derivative causes a negative overflow,
		 * or if \p p is incompatible with \p args or it has a size greater than one.
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - piranha::math::is_zero(),
		 * - piranha::kronecker_array::encode().
		 */
		std::pair<T,kronecker_monomial> partial(const symbol_set::positions &p, const symbol_set &args) const
		{
			auto v = unpack(args);
			// Cannot take derivative wrt more than one variable, and the position of that variable
			// must be compatible with the monomial.
			if (p.size() > 1u || (p.size() == 1u && p.back() >= args.size())) {
				piranha_throw(std::invalid_argument,"invalid size of symbol_set::positions");
			}
			// Derivative wrt a variable not in the monomial: position is empty, or refers to a
			// variable with zero exponent.
			// NOTE: safe to take v.begin() here, as the checks on the positions above ensure
			// there is a valid position and hence the size must be not zero.
			if (!p.size() || math::is_zero(v.begin()[*p.begin()])) {
				return std::make_pair(T(0),kronecker_monomial(args));
			}
			auto v_b = v.begin();
			// Original exponent.
			T n(v_b[*p.begin()]);
			// Decrement the exponent in the monomial.
			if (unlikely(n == std::numeric_limits<T>::min())) {
				piranha_throw(std::invalid_argument,"negative overflow error in the calculation of the "
					"partial derivative of a monomial");
			}
			v_b[*p.begin()] = static_cast<T>(n - T(1));
			kronecker_monomial tmp_km;
			tmp_km.m_value = ka::encode(v);
			return std::make_pair(n,std::move(tmp_km));
		}
		/// Integration.
		/**
		 * Will return the antiderivative of \p this with respect to symbol \p s. The result is a pair
		 * consisting of the exponent associated to \p s increased by one and the monomial itself
		 * after integration. If \p s is not in \p args, the returned monomial will have an extra exponent
		 * set to 1 in the same position \p s would have if it were added to \p args.
		 * 
		 * If the exponent corresponding to \p s is -1, an error will be produced.
		 * 
		 * @param[in] s symbol with respect to which the integration will be calculated.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return result of the integration.
		 * 
		 * @throws std::invalid_argument if the exponent associated to \p s is -1 or if the value of an exponent overflows.
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - piranha::math::is_zero(),
		 * - piranha::static_vector::push_back(),
		 * - piranha::kronecker_array::encode().
		 */
		std::pair<T,kronecker_monomial> integrate(const symbol &s, const symbol_set &args) const
		{
			v_type v = unpack(args), retval;
			value_type expo(0), one(1);
			for (min_int<typename v_type::size_type,decltype(args.size())> i = 0u; i < args.size(); ++i) {
				if (math::is_zero(expo) && s < args[i]) {
					// If we went past the position of s in args and still we
					// have not performed the integration, it means that we need to add
					// a new exponent.
					retval.push_back(one);
					expo = one;
				}
				retval.push_back(v[i]);
				if (args[i] == s) {
					// NOTE: here using i is safe: if retval gained an extra exponent in the condition above,
					// we are never going to land here as args[i] is at this point never going to be s.
					if (unlikely(retval[i] == std::numeric_limits<value_type>::max())) {
						piranha_throw(std::invalid_argument,"positive overflow error in the calculation of the integral of a monomial");
					}
					retval[i] = static_cast<value_type>(retval[i] + value_type(1));
					if (math::is_zero(retval[i])) {
						piranha_throw(std::invalid_argument,"unable to perform monomial integration: negative unitary exponent");
					}
					expo = retval[i];
				}
			}
			// If expo is still zero, it means we need to add a new exponent at the end.
			if (math::is_zero(expo)) {
				retval.push_back(one);
				expo = one;
			}
			return std::make_pair(expo,kronecker_monomial(ka::encode(retval)));
		}
		/// Evaluation.
		/**
		 * \note
		 * This method is available only if \p U satisfies the following requirements:
		 * - it can be used in piranha::symbol_set::positions_map,
		 * - it can be used in piranha::math::pow() with the monomial exponents as powers, yielding a type \p eval_type,
		 * - \p eval_type is constructible from \p int,
		 * - \p eval_type is multipliable in place.
		 * 
		 * The return value will be built by iteratively applying piranha::math::pow() using the values provided
		 * by \p pmap as bases and the values in the monomial as exponents. If the size of the monomial is zero, 1 will be
		 * returned. If the positions in \p pmap do not reference
		 * only and all the exponents in the monomial, an error will be thrown.
		 * 
		 * @param[in] pmap piranha::symbol_set::positions_map that will be used for substitution.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return the result of evaluating \p this with the values provided in \p pmap.
		 * 
		 * @throws std::invalid_argument if \p pmap is not compatible with \p args.
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - construction of the return type,
		 * - piranha::math::pow() or the in-place multiplication operator of the return type.
		 */
		template <typename U>
		eval_type<U> evaluate(const symbol_set::positions_map<U> &pmap, const symbol_set &args) const
		{
			using return_type = eval_type<U>;
			using size_type = typename v_type::size_type;
			// NOTE: here we can check the pmap size only against args.
			if (unlikely(pmap.size() != args.size() || (pmap.size() && pmap.back().first != pmap.size() - 1u))) {
				piranha_throw(std::invalid_argument,"invalid positions map for evaluation");
			}
			auto v = unpack(args);
			return_type retval(1);
			auto it = pmap.begin();
			for (min_int<size_type,decltype(args.size())> i = 0u; i < args.size(); ++i, ++it) {
				piranha_assert(it != pmap.end() && it->first == i);
				retval *= math::pow(it->second,v[i]);
			}
			piranha_assert(it == pmap.end());
			return retval;
		}
		/// Substitution.
		/**
		 * \note
		 * This method is enabled only if:
		 * - \p U can be raised to the value type, yielding a type \p subs_type,
		 * - \p subs_type can be constructed from \p int and it is assignable.
		 *
		 * The algorithm is equivalent to the one implemented in piranha::monomial::subs().
		 * 
		 * @param[in] s name of the symbol that will be substituted.
		 * @param[in] x quantity that will be substituted in place of \p s.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return the result of substituting \p x for \p s.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - construction and assignment of the return value,
		 * - piranha::math::pow(),
		 * - piranha::static_vector::push_back(),
		 * - piranha::kronecker_array::encode().
		 */
		template <typename U>
		std::vector<std::pair<subs_type<U>,kronecker_monomial>> subs(const std::string &s, const U &x, const symbol_set &args) const
		{
			using s_type = subs_type<U>;
			std::vector<std::pair<s_type,kronecker_monomial>> retval;
			const auto v = unpack(args);
			v_type new_v;
			s_type retval_s(1);
			for (min_int<typename v_type::size_type,decltype(args.size())> i = 0u; i < args.size(); ++i) {
				if (args[i].get_name() == s) {
					retval_s = math::pow(x,v[i]);
					new_v.push_back(value_type(0));
				} else {
					new_v.push_back(v[i]);
				}
			}
			piranha_assert(new_v.size() == v.size());
			retval.push_back(std::make_pair(std::move(retval_s),kronecker_monomial(ka::encode(new_v))));
			return retval;
		}
		/// Substitution of integral power.
		/**
		 * \note
		 * This method is enabled only if:
		 * - \p U can be raised to a piranha::integer power, yielding a type \p subs_type,
		 * - \p subs_type is constructible from \p int and assignable.
		 *
		 * This method works in the same way as piranha::monomial::ipow_subs().
		 * 
		 * @param[in] s name of the symbol that will be substituted.
		 * @param[in] n power of \p s that will be substituted.
		 * @param[in] x quantity that will be substituted in place of \p s to the power of \p n.
		 * @param[in] args reference set of piranha::symbol.
		 * 
		 * @return the result of substituting \p x for \p s to the power of \p n.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - construction and assignment of the return value,
		 * - construction of piranha::rational,
		 * - piranha::safe_cast(),
		 * - piranha::math::pow(),
		 * - piranha::static_vector::push_back(),
		 * - the in-place subtraction operator of the exponent type,
		 * - piranha::kronecker_array::encode().
		 */
		template <typename U>
		std::vector<std::pair<ipow_subs_type<U>,kronecker_monomial>> ipow_subs(const std::string &s, const integer &n, const U &x, const symbol_set &args) const
		{
			using s_type = ipow_subs_type<U>;
			const auto v = unpack(args);
			v_type new_v;
			s_type retval_s(1);
			for (min_int<typename v_type::size_type,decltype(args.size())> i = 0u; i < args.size(); ++i) {
				new_v.push_back(v[i]);
				if (args[i].get_name() == s) {
					const rational tmp(safe_cast<integer>(v[i]),n);
					if (tmp >= 1) {
						const auto tmp_t = static_cast<integer>(tmp);
						retval_s = math::pow(x,tmp_t);
						new_v[i] -= tmp_t * n;
					}
				}
			}
			std::vector<std::pair<s_type,kronecker_monomial>> retval;
			retval.push_back(std::make_pair(std::move(retval_s),kronecker_monomial(ka::encode(new_v))));
			return retval;
		}
		/// Identify symbols that can be trimmed.
		/**
		 * This method is used in piranha::series::trim(). The input parameter \p candidates
		 * contains a set of symbols that are candidates for elimination. The method will remove
		 * from \p candidates those symbols whose exponent in \p this is not zero.
		 * 
		 * @param[in] candidates set of candidates for elimination.
		 * @param[in] args reference arguments set.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - piranha::math::is_zero(),
		 * - piranha::symbol_set::remove().
		 */
		void trim_identify(symbol_set &candidates, const symbol_set &args) const
		{
			return detail::km_trim_identify<v_type,ka>(candidates,args,m_value);
		}
		/// Trim.
		/**
		 * This method will return a copy of \p this with the exponents associated to the symbols
		 * in \p trim_args removed.
		 * 
		 * @param[in] trim_args arguments whose exponents will be removed.
		 * @param[in] orig_args original arguments set.
		 * 
		 * @return trimmed copy of \p this.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - unpack(),
		 * - piranha::static_vector::push_back().
		 */
		kronecker_monomial trim(const symbol_set &trim_args, const symbol_set &orig_args) const
		{
			return kronecker_monomial(detail::km_trim<v_type,ka>(trim_args,orig_args,m_value));
		}
	private:
		value_type m_value;
};

/// Alias for piranha::kronecker_monomial with default type.
using k_monomial = kronecker_monomial<>;

}

namespace std
{

/// Specialisation of \p std::hash for piranha::kronecker_monomial.
template <typename T>
struct hash<piranha::kronecker_monomial<T>>
{
	/// Result type.
	typedef size_t result_type;
	/// Argument type.
	typedef piranha::kronecker_monomial<T> argument_type;
	/// Hash operator.
	/**
	 * @param[in] a argument whose hash value will be computed.
	 * 
	 * @return hash value of \p a computed via piranha::kronecker_monomial::hash().
	 */
	result_type operator()(const argument_type &a) const
	{
		return a.hash();
	}
};

}

#endif
