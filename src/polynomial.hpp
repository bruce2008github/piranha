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

#ifndef PIRANHA_POLYNOMIAL_HPP
#define PIRANHA_POLYNOMIAL_HPP

#include <algorithm>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <cmath> // For std::ceil.
#include <condition_variable>
#include <cstddef>
#include <functional> // For std::bind.
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.hpp"
#include "debug_access.hpp"
#include "detail/divisor_series_fwd.hpp"
#include "detail/poisson_series_fwd.hpp"
#include "detail/polynomial_fwd.hpp"
#include "detail/sfinae_types.hpp"
#include "exceptions.hpp"
#include "forwarding.hpp"
#include "ipow_substitutable_series.hpp"
#include "is_cf.hpp"
#include "kronecker_array.hpp"
#include "kronecker_monomial.hpp"
#include "math.hpp"
#include "monomial.hpp"
#include "mp_integer.hpp"
#include "pow.hpp"
#include "power_series.hpp"
#include "safe_cast.hpp"
#include "serialization.hpp"
#include "series.hpp"
#include "series_multiplier.hpp"
#include "settings.hpp"
#include "substitutable_series.hpp"
#include "symbol.hpp"
#include "symbol_set.hpp"
#include "t_substitutable_series.hpp"
#include "thread_pool.hpp"
#include "trigonometric_series.hpp"
#include "tuning.hpp"
#include "type_traits.hpp"

namespace piranha
{

namespace detail
{

struct polynomial_tag {};

// Type trait to check the key type in polynomial.
template <typename T>
struct is_polynomial_key
{
	static const bool value = false;
};

template <typename T>
struct is_polynomial_key<kronecker_monomial<T>>
{
	static const bool value = true;
};

template <typename T, typename U>
struct is_polynomial_key<monomial<T,U>>
{
	static const bool value = true;
};

// Implementation detail to check if the monomial key supports the linear_argument() method.
template <typename Key>
struct key_has_linarg: detail::sfinae_types
{
	template <typename U>
	static auto test(const U &u) -> decltype(u.linear_argument(std::declval<const symbol_set &>()));
	static no test(...);
	static const bool value = std::is_same<std::string,decltype(test(std::declval<Key>()))>::value;
};

}

/// Polynomial class.
/**
 * This class represents multivariate polynomials as collections of multivariate polynomial terms.
 * \p Cf represents the ring over which the polynomial is defined, while \p Key represents the monomial type.
 * 
 * This class satisfies the piranha::is_series type trait.
 * 
 * ## Type requirements ##
 * 
 * \p Cf must be suitable for use in piranha::series as first template argument,
 * \p Key must be an instance of either piranha::monomial or piranha::kronecker_monomial.
 * 
 * ## Exception safety guarantee ##
 * 
 * This class provides the same guarantee as the base series type it derives from.
 * 
 * ## Move semantics ##
 * 
 * Move semantics is equivalent to the move semantics of the base series type it derives from.
 *
 * ## Serialization ##
 *
 * This class supports serialization if the underlying coefficient and key types do.
 * 
 * @author Francesco Biscani (bluescarni@gmail.com)
 */
template <typename Cf, typename Key>
class polynomial:
	public power_series<trigonometric_series<ipow_substitutable_series<substitutable_series<t_substitutable_series<series<Cf,Key,
	polynomial<Cf,Key>>,polynomial<Cf,Key>>,polynomial<Cf,Key>>,polynomial<Cf,Key>>>,polynomial<Cf,Key>>,detail::polynomial_tag
{
		// Check the key.
		PIRANHA_TT_CHECK(detail::is_polynomial_key,Key);
		// Make friend with debug class.
		template <typename>
		friend class debug_access;
		// Make friend with Poisson series.
		template <typename>
		friend class poisson_series;
		// Make friend with divisor series.
		template <typename, typename>
		friend class divisor_series;
		// The base class.
		using base = power_series<trigonometric_series<ipow_substitutable_series<substitutable_series<t_substitutable_series<series<Cf,Key,
			polynomial<Cf,Key>>,polynomial<Cf,Key>>,polynomial<Cf,Key>>,polynomial<Cf,Key>>>,polynomial<Cf,Key>>;
		template <typename Str>
		void construct_from_string(Str &&str)
		{
			typedef typename base::term_type term_type;
			// Insert the symbol.
			this->m_symbol_set.add(symbol(std::forward<Str>(str)));
			// Construct and insert the term.
			this->insert(term_type(Cf(1),typename term_type::key_type{1}));
		}
		template <typename T = Key, typename std::enable_if<detail::key_has_linarg<T>::value && has_safe_cast<integer,Cf>::value,int>::type = 0>
		std::map<std::string,integer> integral_combination() const
		{
			try {
				std::map<std::string,integer> retval;
				for (auto it = this->m_container.begin(); it != this->m_container.end(); ++it) {
					const std::string lin_arg = it->m_key.linear_argument(this->m_symbol_set);
					piranha_assert(retval.find(lin_arg) == retval.end());
					retval[lin_arg] = safe_cast<integer>(it->m_cf);
				}
				return retval;
			} catch (const std::invalid_argument &) {
				piranha_throw(std::invalid_argument,"polynomial is not an integral linear combination");
			}
		}
		template <typename T = Key, typename std::enable_if<!detail::key_has_linarg<T>::value || !has_safe_cast<integer,Cf>::value,int>::type = 0>
		std::map<std::string,integer> integral_combination() const
		{
			piranha_throw(std::invalid_argument,"the polynomial type does not support the extraction of a linear combination");
		}
		// Integration utils.
		// Empty for SFINAE.
		template <typename T, typename = void>
		struct integrate_type_
		{};
		// The type resulting from the integration of the key of series T.
		template <typename T>
		using key_integrate_type = decltype(std::declval<const typename T::term_type::key_type &>().integrate(
			std::declval<const symbol &>(),std::declval<const symbol_set &>()).first);
		// Basic integration requirements for series T, to be satisfied both when the coefficient is integrable
		// and when it is not. ResT is the type of the result of the integration.
		template <typename T, typename ResT>
		using basic_integrate_requirements = typename std::enable_if<
			// Coefficient differentiable, and can call is_zero on the result.
			has_is_zero<decltype(math::partial(std::declval<const typename T::term_type::cf_type &>(),std::declval<const std::string &>()))>::value &&
			// The key is integrable.
			detail::true_tt<key_integrate_type<T>>::value &&
			// The result needs to be addable in-place.
			is_addable_in_place<ResT>::value &&
			// It also needs to be ctible from zero.
			std::is_constructible<ResT,int>::value
		>::type;
		// Non-integrable coefficient.
		template <typename T>
		using nic_res_type = decltype((std::declval<const T &>() * std::declval<const typename T::term_type::cf_type &>()) /
			std::declval<const key_integrate_type<T> &>());
		template <typename T>
		struct integrate_type_<T,typename std::enable_if<!is_integrable<typename T::term_type::cf_type>::value &&
			detail::true_tt<basic_integrate_requirements<T,nic_res_type<T>>>::value>::type>
		{
			using type = nic_res_type<T>;
		};
		// Integrable coefficient.
		// The type resulting from the differentiation of the key of series T.
		template <typename T>
		using key_partial_type = decltype(std::declval<const typename T::term_type::key_type &>().partial(
			std::declval<const symbol_set::positions &>(),std::declval<const symbol_set &>()).first);
		// Type resulting from the integration of the coefficient.
		template <typename T>
		using i_cf_type = decltype(math::integrate(std::declval<const typename T::term_type::cf_type &>(),std::declval<const std::string &>()));
		// Type above, multiplied by the type coming out of the derivative of the key.
		template <typename T>
		using i_cf_type_p = decltype(std::declval<const i_cf_type<T> &>() * std::declval<const key_partial_type<T> &>());
		// Final series type.
		template <typename T>
		using ic_res_type = decltype(std::declval<const i_cf_type_p<T> &>() * std::declval<const T &>());
		template <typename T>
		struct integrate_type_<T,typename std::enable_if<is_integrable<typename T::term_type::cf_type>::value &&
			detail::true_tt<basic_integrate_requirements<T,ic_res_type<T>>>::value &&
			// We need to be able to add the non-integrable type.
			is_addable_in_place<ic_res_type<T>,nic_res_type<T>>::value &&
			// We need to be able to compute the partial degree and cast it to integer.
			has_safe_cast<integer,decltype(std::declval<const typename T::term_type::key_type &>().degree(std::declval<const symbol_set::positions &>(),
			std::declval<const symbol_set &>()))>::value &&
			// This is required in the initialisation of the return value.
			std::is_constructible<i_cf_type_p<T>,i_cf_type<T>>::value &&
			// We need to be able to assign the integrated coefficient times key partial.
			std::is_assignable<i_cf_type_p<T> &,i_cf_type_p<T>>::value &&
			// Needs math::negate().
			has_negate<i_cf_type_p<T>>::value
			>::type>
		{
			using type = ic_res_type<T>;
		};
		// Final typedef.
		template <typename T>
		using integrate_type = typename integrate_type_<T>::type;
		// Integration with integrable coefficient.
		template <typename T = polynomial>
		integrate_type<T> integrate_impl(const symbol &s, const typename base::term_type &term,
			const std::true_type &) const
		{
			typedef typename base::term_type term_type;
			typedef typename term_type::cf_type cf_type;
			typedef typename term_type::key_type key_type;
			// Get the partial degree of the monomial in integral form.
			integer degree;
			const symbol_set::positions pos(this->m_symbol_set,symbol_set{s});
			try {
				degree = safe_cast<integer>(term.m_key.degree(pos,this->m_symbol_set));
			} catch (const std::invalid_argument &) {
				piranha_throw(std::invalid_argument,
					"unable to perform polynomial integration: cannot extract the integral form of an exponent");
			}
			// If the degree is negative, integration by parts won't terminate.
			if (degree.sign() < 0) {
				piranha_throw(std::invalid_argument,
					"unable to perform polynomial integration: negative integral exponent");
			}
			polynomial tmp;
			tmp.set_symbol_set(this->m_symbol_set);
			key_type tmp_key = term.m_key;
			tmp.insert(term_type(cf_type(1),tmp_key));
			i_cf_type_p<T> i_cf(math::integrate(term.m_cf,s.get_name()));
			integrate_type<T> retval(i_cf * tmp);
			for (integer i(1); i <= degree; ++i) {
				// Update coefficient and key. These variables are persistent across loop iterations.
				auto partial_key = tmp_key.partial(pos,this->m_symbol_set);
				i_cf = math::integrate(i_cf,s.get_name()) * std::move(partial_key.first);
				// Account for (-1)**i.
				math::negate(i_cf);
				// Build the other factor from the derivative of the monomial.
				tmp = polynomial{};
				tmp.set_symbol_set(this->m_symbol_set);
				tmp_key = std::move(partial_key.second);
				// NOTE: don't move tmp_key, as it needs to hold a valid value
				// for the next loop iteration.
				tmp.insert(term_type(cf_type(1),tmp_key));
				retval += i_cf * tmp;
			}
			return retval;
		}
		// Integration with non-integrable coefficient.
		template <typename T = polynomial>
		integrate_type<T> integrate_impl(const symbol &, const typename base::term_type &,
			const std::false_type &) const
		{
			piranha_throw(std::invalid_argument,"unable to perform polynomial integration: coefficient type is not integrable");
		}
		// Template alias for use in pow() overload. Will check via SFINAE that the base pow() method can be called with argument T
		// and that exponentiation of key type is legal.
		template <typename T, typename Series>
		using pow_ret_type = typename std::enable_if<
			detail::true_tt<decltype(std::declval<typename Series::term_type::key_type const &>().pow(std::declval<const T &>(),std::declval<const symbol_set &>()))>::value,
			decltype(std::declval<series<Cf,Key,polynomial<Cf,Key>> const &>().pow(std::declval<const T &>()))>::type;
		PIRANHA_SERIALIZE_THROUGH_BASE(base)
		// Invert utils.
		template <typename Series>
		using inverse_type = decltype(std::declval<const Series &>().pow(-1));
	public:
		/// Series rebind alias.
		template <typename Cf2>
		using rebind = polynomial<Cf2,Key>;
		/// Defaulted default constructor.
		/**
		 * Will construct a polynomial with zero terms.
		 */
		polynomial() = default;
		/// Defaulted copy constructor.
		polynomial(const polynomial &) = default;
		/// Defaulted move constructor.
		polynomial(polynomial &&) = default;
		/// Constructor from symbol name.
		/**
		 * Will construct a univariate polynomial made of a single term with unitary coefficient and exponent, representing
		 * the symbolic variable \p name. The type of \p name must be a string type (either C or C++).
		 * 
		 * @param[in] name name of the symbolic variable that the polynomial will represent.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - piranha::symbol_set::add(),
		 * - the constructor of piranha::symbol from string,
		 * - the invoked constructor of the coefficient type,
		 * - the invoked constructor of the key type,
		 * - the constructor of the term type from coefficient and key,
		 * - piranha::series::insert().
		 */
		template <typename Str>
		explicit polynomial(Str &&name, typename std::enable_if<
			std::is_same<typename std::decay<Str>::type,std::string>::value ||
			std::is_same<typename std::decay<Str>::type,char *>::value ||
			std::is_same<typename std::decay<Str>::type,const char *>::value>::type * = nullptr) : base()
		{
			construct_from_string(std::forward<Str>(name));
		}
		PIRANHA_FORWARDING_CTOR(polynomial,base)
		/// Trivial destructor.
		~polynomial()
		{
			PIRANHA_TT_CHECK(is_cf,polynomial);
			PIRANHA_TT_CHECK(is_series,polynomial);
		}
		/// Defaulted copy assignment operator.
		polynomial &operator=(const polynomial &) = default;
		/// Defaulted move assignment operator.
		polynomial &operator=(polynomial &&) = default;
		/// Assignment from symbol name (C string).
		/**
		 * Equivalent to invoking the constructor from symbol name and assigning the result to \p this.
		 * 
		 * @param[in] name name of the symbolic variable that the polynomial will represent.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the constructor from symbol name.
		 */
		polynomial &operator=(const char *name)
		{
			operator=(polynomial(name));
			return *this;
		}
		/// Assignment from symbol name (C++ string).
		/**
		 * Equivalent to invoking the constructor from symbol name and assigning the result to \p this.
		 * 
		 * @param[in] name name of the symbolic variable that the polynomial will represent.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the constructor from symbol name.
		 */
		polynomial &operator=(const std::string &name)
		{
			operator=(polynomial(name));
			return *this;
		}
		PIRANHA_FORWARDING_ASSIGNMENT(polynomial,base)
		/// Override default exponentiation method.
		/**
		 * \note
		 * This method is enabled only if piranha::series::pow() can be called with exponent \p x
		 * and the key type can be raised to the power of \p x via its exponentiation method.
		 * 
		 * This exponentiation override will check if the polynomial consists of a single-term with non-unitary
		 * key. In that case, the return polynomial will consist of a single term with coefficient computed via
		 * piranha::math::pow() and key computed via the monomial exponentiation method.
		 * 
		 * Otherwise, the base (i.e., default) exponentiation method will be used.
		 * 
		 * @param[in] x exponent.
		 * 
		 * @return \p this to the power of \p x.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - the <tt>is_unitary()</tt> and exponentiation methods of the key type,
		 * - piranha::math::pow(),
		 * - construction of coefficient, key and term,
		 * - the copy assignment operator of piranha::symbol_set,
		 * - piranha::series::insert() and piranha::series::pow().
		 */
		template <typename T, typename Series = polynomial>
		pow_ret_type<T,Series> pow(const T &x) const
		{
			using ret_type = pow_ret_type<T,Series>;
			typedef typename ret_type::term_type term_type;
			typedef typename term_type::cf_type cf_type;
			typedef typename term_type::key_type key_type;
			if (this->size() == 1u && !this->m_container.begin()->m_key.is_unitary(this->m_symbol_set)) {
				cf_type cf(math::pow(this->m_container.begin()->m_cf,x));
				key_type key(this->m_container.begin()->m_key.pow(x,this->m_symbol_set));
				ret_type retval;
				retval.set_symbol_set(this->m_symbol_set);
				retval.insert(term_type(std::move(cf),std::move(key)));
				return retval;
			}
			return static_cast<series<Cf,Key,polynomial<Cf,Key>> const *>(this)->pow(x);
		}
		/// Inversion.
		/**
		 * @return the calling polynomial raised to -1 using piranha::polynomial::pow().
		 *
		 * @throws unspecified any exception thrown by piranha::polynomial::pow().
		 */
		template <typename Series = polynomial>
		inverse_type<Series> invert() const
		{
			return this->pow(-1);
		}
		/// Integration.
		/**
		 * \note
		 * This method is enabled only if the algorithm described below is supported by all the involved types.
		 *
		 * This method will attempt to compute the antiderivative of the polynomial term by term. If the term's coefficient does not depend on
		 * the integration variable, the result will be calculated via the integration of the corresponding monomial.
		 * Integration with respect to a variable appearing to the power of -1 will fail.
		 * 
		 * Otherwise, a strategy of integration by parts is attempted, its success depending on the integrability
		 * of the coefficient and on the value of the exponent of the integration variable. The integration will
		 * fail if the exponent is negative or non-integral.
		 * 
		 * @param[in] name integration variable.
		 * 
		 * @return the antiderivative of \p this with respect to \p name.
		 * 
		 * @throws std::invalid_argument if the integration procedure fails.
		 * @throws unspecified any exception thrown by:
		 * - piranha::symbol construction,
		 * - piranha::math::partial(), piranha::math::is_zero(), piranha::math::integrate(), piranha::safe_cast()
		 *   and piranha::math::negate(),
		 * - piranha::symbol_set::add(),
		 * - term construction,
		 * - coefficient construction, assignment and arithmetics,
		 * - integration, construction, assignment, differentiation and degree querying methods of the key type,
		 * - insert(),
		 * - series arithmetics.
		 */
		template <typename T = polynomial>
		integrate_type<T> integrate(const std::string &name) const
		{
			typedef typename base::term_type term_type;
			typedef typename term_type::cf_type cf_type;
			// Turn name into symbol.
			const symbol s(name);
			integrate_type<T> retval(0);
			const auto it_f = this->m_container.end();
			for (auto it = this->m_container.begin(); it != it_f; ++it) {
				// If the derivative of the coefficient is null, we just need to deal with
				// the integration of the key.
				if (math::is_zero(math::partial(it->m_cf,name))) {
					polynomial tmp;
					symbol_set sset = this->m_symbol_set;
					// If the variable does not exist in the arguments set, add it.
					if (!std::binary_search(sset.begin(),sset.end(),s)) {
						sset.add(s);
					}
					tmp.set_symbol_set(sset);
					auto key_int = it->m_key.integrate(s,this->m_symbol_set);
					tmp.insert(term_type(cf_type(1),std::move(key_int.second)));
					retval += (tmp * it->m_cf) / key_int.first;
				} else {
					retval += integrate_impl(s,*it,std::integral_constant<bool,is_integrable<cf_type>::value>());
				}
			}
			return retval;
		}
};

namespace detail
{

template <typename Series>
struct kronecker_enabler
{
	PIRANHA_TT_CHECK(is_series,Series);
	template <typename Key>
	struct is_kronecker_monomial
	{
		static const bool value = false;
	};
	template <typename T>
	struct is_kronecker_monomial<kronecker_monomial<T>>
	{
		static const bool value = true;
	};
	using key_type = typename Series::term_type::key_type;
	static const bool value = std::is_base_of<detail::polynomial_tag,Series>::value &&
		is_kronecker_monomial<key_type>::value;
};

}

/// Series multiplier specialisation for polynomials with Kronecker monomials.
/**
 * This specialisation of piranha::series_multiplier is enabled when \p Series is an instance of
 * piranha::polynomial with monomials represented as piranha::kronecker_monomial.
 * This multiplier will employ optimized algorithms that take advantage of the properties of Kronecker monomials.
 * It will also take advantage of piranha::math::multiply_accumulate() in place of plain coefficient multiplication
 * when possible.
 * 
 * ## Exception safety guarantee ##
 * 
 * This class provides the same guarantee as the non-specialised piranha::series_multiplier.
 * 
 * ## Move semantics ##
 * 
 * Move semantics is equivalent to piranha::series_multiplier's move semantics.
 */
// \todo optimize task list in single thread and maybe also for small operands -> make it a vector I guess, instead of a set.
template <typename Series>
class series_multiplier<Series,typename std::enable_if<detail::kronecker_enabler<Series>::value>::type>:
	public series_multiplier<Series,int>
{
		PIRANHA_TT_CHECK(is_series,Series);
		using value_type = typename Series::term_type::key_type::value_type;
		typedef kronecker_array<value_type> ka;
	public:
		/// Base multiplier type.
		typedef series_multiplier<Series,int> base;
		/// Constructor.
		/**
		 * Will call the base constructor and additionally check that the result of the multiplication will not overflow
		 * the representation limits of piranha::kronecker_monomial. In such a case, a runtime error will be produced.
		 * 
		 * @param[in] s1 first series operand.
		 * @param[in] s2 second series operand.
		 * 
		 * @throws std::invalid_argument if the the result of the multiplication overflows the representation limits of
		 * piranha::kronecker_monomial.
		 * @throws unspecified any exception thrown by the base constructor.
		 */
		explicit series_multiplier(const Series &s1, const Series &s2):base(s1,s2)
		{
			if (unlikely(this->m_s1->empty() || this->m_s2->empty())) {
				return;
			}
			// NOTE: here we are sure about this since the symbol set in a series should never
			// overflow the size of the limits, as the check for compatibility in Kronecker monomial
			// would kick in.
			piranha_assert(this->m_s1->m_symbol_set.size() < ka::get_limits().size());
			piranha_assert(this->m_s1->m_symbol_set == this->m_s2->m_symbol_set);
			const auto &limits = ka::get_limits()[this->m_s1->m_symbol_set.size()];
			// NOTE: We need to check that the exponents of the monomials in the result do not
			// go outside the bounds of the Kronecker codification. We need to unpack all monomials
			// in the operands and examine them, we cannot operate on the codes for this.
			auto it1 = this->m_v1.begin();
			auto it2 = this->m_v2.begin();
			// Initialise minmax values.
			std::vector<std::pair<value_type,value_type>> minmax_values1;
			std::vector<std::pair<value_type,value_type>> minmax_values2;
			auto tmp_vec1 = (*it1)->m_key.unpack(this->m_s1->m_symbol_set);
			auto tmp_vec2 = (*it2)->m_key.unpack(this->m_s1->m_symbol_set);
			// Bounds of the Kronecker representation for each component.
			const auto &minmax_vec = std::get<0u>(limits);
			std::transform(tmp_vec1.begin(),tmp_vec1.end(),std::back_inserter(minmax_values1),[](const value_type &v) {
				return std::make_pair(v,v);
			});
			std::transform(tmp_vec2.begin(),tmp_vec2.end(),std::back_inserter(minmax_values2),[](const value_type &v) {
				return std::make_pair(v,v);
			});
			// Find the minmaxs.
			for (; it1 != this->m_v1.end(); ++it1) {
				tmp_vec1 = (*it1)->m_key.unpack(this->m_s1->m_symbol_set);
				piranha_assert(tmp_vec1.size() == minmax_values1.size());
				std::transform(minmax_values1.begin(),minmax_values1.end(),tmp_vec1.begin(),minmax_values1.begin(),
					[](const std::pair<value_type,value_type> &p, const value_type &v) {
						return std::make_pair(
							v < p.first ? v : p.first,
							v > p.second ? v : p.second
						);
				});
			}
			for (; it2 != this->m_v2.end(); ++it2) {
				tmp_vec2 = (*it2)->m_key.unpack(this->m_s2->m_symbol_set);
				piranha_assert(tmp_vec2.size() == minmax_values2.size());
				std::transform(minmax_values2.begin(),minmax_values2.end(),tmp_vec2.begin(),minmax_values2.begin(),
					[](const std::pair<value_type,value_type> &p, const value_type &v) {
						return std::make_pair(
							v < p.first ? v : p.first,
							v > p.second ? v : p.second
						);
				});
			}
			// Compute the sum of the two minmaxs, using multiprecision to avoid overflow.
			// NOTE: first store in m_minmax_values the ranges of the result only, below we will update this with the range
			// of the operands.
			std::transform(minmax_values1.begin(),minmax_values1.end(),minmax_values2.begin(),
				std::back_inserter(m_minmax_values),[](const std::pair<value_type,value_type> &p1,
				const std::pair<value_type,value_type> &p2) {
					return std::make_pair(integer(p1.first) + integer(p2.first),integer(p1.second) + integer(p2.second));
			});
			piranha_assert(m_minmax_values.size() == minmax_vec.size());
			piranha_assert(m_minmax_values.size() == minmax_values1.size());
			piranha_assert(m_minmax_values.size() == minmax_values2.size());
			for (decltype(m_minmax_values.size()) i = 0u; i < m_minmax_values.size(); ++i) {
				if (unlikely(m_minmax_values[i].first < -minmax_vec[i] || m_minmax_values[i].second > minmax_vec[i])) {
					piranha_throw(std::overflow_error,"Kronecker monomial components are out of bounds");
				}
				// Update with the ranges of the operands.
				m_minmax_values[i] = std::minmax({m_minmax_values[i].first,
					integer(minmax_values1[i].first),integer(minmax_values2[i].first),m_minmax_values[i].second,
					integer(minmax_values1[i].second),integer(minmax_values2[i].second)});
			}
		}
		/// Perform multiplication.
		/**
		 * @return the result of the multiplication of the input series operands.
		 * 
		 * @throws std::overflow_error in case of (unlikely) overflow errors.
		 * @throws unspecified any exception thrown by:
		 * - (unlikely) conversion errors between numeric types,
		 * - the public interface of piranha::hash_set,
		 * - piranha::series_multiplier::estimate_final_series_size(),
		 * - piranha::math::multiply_accumulate() on the coefficient types,
		 * - threading primitives,
		 * - memory allocation errors in standard containers,
		 * - piranha::thread_pool::enqueue(),
		 * - piranha::future_list::push_back().
		 */
		Series operator()() const
		{
			return execute();
		}
	private:
		using term_type = typename Series::term_type;
		using index_type = typename std::vector<term_type const *>::size_type;
		using bucket_size_type = typename Series::size_type;
		// This is a bucket region, i.e., a _closed_ interval [a,b] of bucket indices in a hash set.
		using region_type = std::pair<bucket_size_type,bucket_size_type>;
		// Block-by-block multiplication task.
		struct task_type
		{
			// First block: semi-open range [a,b[ of indices in first input series.
			std::pair<index_type,index_type>	m_b1;
			// Second block (indices in second input series).
			std::pair<index_type,index_type>	m_b2;
			// First region memory region involved in the multiplication.
			region_type				m_r1;
			// Second memory region.
			region_type				m_r2;
			// Boolean flag to signal if the second region is present or not.
			bool					m_second_region;
		};
		// Create task from indices i in first series, j in second series (semi-open intervals).
		task_type task_from_indices(const index_type &i_start, const index_type &i_end,
			const index_type &j_start, const index_type &j_end, const Series &retval) const
		{
			const auto &v1 = this->m_v1;
			const auto &v2 = this->m_v2;
			piranha_assert(i_start < i_end && j_start < j_end);
			piranha_assert(i_end <= v1.size() && j_end <= v2.size());
			const auto b_count = retval.m_container.bucket_count();
			piranha_assert(b_count > 0u);
			region_type r1{0u,0u}, r2{0u,0u};
			bool second_region = false;
			// Addition is safe because of the limits on the max bucket count of hash_set.
			const auto a = retval.m_container._bucket_from_hash(v1[i_start]->hash()) +
				retval.m_container._bucket_from_hash(v2[j_start]->hash()),
				// NOTE: we are sure that the tasks are not empty, so the -1 is safe.
				b = retval.m_container._bucket_from_hash(v1[i_end - 1u]->hash()) +
				retval.m_container._bucket_from_hash(v2[j_end - 1u]->hash());
			// NOTE: using <= here as [a,b] is now a closed interval.
			piranha_assert(a <= b);
			piranha_assert(b <= b_count * 2u - 2u);
			if (b < b_count || a >= b_count) {
				r1.first = a % b_count;
				r1.second = b % b_count;
			} else {
				second_region = true;
				r1.first = a;
				r1.second = b_count - 1u;
				r2.first = 0u;
				r2.second = b % b_count;
			}
			// Correct the case in which a second region reaches the first one (thus
			// covering the whole range of buckets).
			if (second_region && r2.second >= r1.first) {
				second_region = false;
				r1.first = 0u;
				r1.second = b_count - 1u;
				r2.first = 0u;
				r2.second = 0u;
			}
			piranha_assert(r1.first <= r1.second);
			piranha_assert(!second_region || (r2.first <= r2.second && r2.second < r1.first));
			return task_type{std::make_pair(i_start,i_end),std::make_pair(j_start,j_end),r1,r2,second_region};
		}
		// Functor to check if region r does not overlap any of the busy ones.
		template <typename RegionSet>
		static bool region_checker(const region_type &r, const RegionSet &busy_regions)
		{
			if (busy_regions.empty()) {
				return true;
			}
			// NOTE: lower bound means that it_b->first >= r.first.
			auto it_b = busy_regions.lower_bound(r);
			// Handle end().
			if (it_b == busy_regions.end()) {
				// NOTE: safe because busy_regions is not empty.
				--it_b;
				// Now check that the region in it_b does not overlap with
				// the current one.
				piranha_assert(it_b->first < r.first);
				return it_b->second < r.first;
			}
			if (r.second >= it_b->first) {
				// The end point of r1 overlaps the start point of *it_b, no good.
				return false;
			}
			// Lastly, we have to check that the previous element (if any) does not
			// overlap r.first.
			if (it_b != busy_regions.begin()) {
				--it_b;
				if (it_b->second >= r.first) {
					return false;
				}
			}
			return true;
		}
		// Check that a set of busy regions is well-formed, for debug purposes.
		template <typename RegionSet>
		static bool region_set_checker(const RegionSet &busy_regions)
		{
			if (busy_regions.empty()) {
				return true;
			}
			const auto it_f = busy_regions.end();
			auto it = busy_regions.begin();
			auto prev_it(it);
			++it;
			for (; it != it_f; ++it, ++prev_it) {
				if (it->first > it->second || prev_it->first > prev_it->second) {
					return false;
				}
				if (prev_it->second >= it->first) {
					return false;
				}
			}
			return true;
		}
		Series execute() const
		{
			const index_type size1 = this->m_v1.size(), size2 = this->m_v2.size();
			// Do not do anything if one of the two series is empty, just return an empty series.
			if (unlikely(!size1 || !size2)) {
				return Series{};
			}
			// This check is done here to avoid controlling the number of elements of the output series
			// at every iteration of the functor.
			const auto max_size = integer(size1) * size2;
			if (unlikely(max_size > std::numeric_limits<bucket_size_type>::max())) {
				piranha_throw(std::overflow_error,"possible overflow in series size");
			}
			// First, let's get the estimation on the size of the final series.
			Series retval;
			retval.m_symbol_set = this->m_s1->m_symbol_set;
			typename Series::size_type estimate;
			// Use the sparse functor for the estimation.
			estimate = base::estimate_final_series_size(sparse_functor<>(&this->m_v1[0u],size1,&this->m_v2[0u],size2,retval));
			// Correct the unlikely case of zero estimate.
			if (unlikely(!estimate)) {
				estimate = 1u;
			}
			// Get the number of threads to use.
			const unsigned n_threads = thread_pool::use_threads(
				integer(size1) * size2,integer(settings::get_min_work_per_thread())
			);
			// Rehash the retun value's container accordingly. Check the tuning flag to see if we want to use
			// multiple threads for initing the return value.
			// NOTE: it is important here that we use the same n_threads for multiplication and memset as
			// we tie together pinned threads with potentially different NUMA regions.
			const unsigned n_threads_rehash = tuning::get_parallel_memory_set() ? n_threads : 1u;
			// NOTE: if something goes wrong here, no big deal as retval is still empty.
			retval.m_container.rehash(boost::numeric_cast<typename Series::size_type>(std::ceil(static_cast<double>(estimate) /
				retval.m_container.max_load_factor())),n_threads_rehash);
			piranha_assert(retval.m_container.bucket_count());
			// NOTE: tuning parameter.
			if ((integer(size1) * integer(size2)) / estimate > 200) {
				dense_multiplication(retval,n_threads);
			} else {
				sparse_multiplication<sparse_functor<>>(retval,n_threads);
			}
			// Trace the result of estimation.
			this->trace_estimates(retval.size(),estimate);
			return retval;
		}
		// Utility function to determine block sizes.
		static std::pair<integer,integer> get_block_sizes(const index_type &size1, const index_type &size2)
		{
			const integer block_size(512u), job_size = block_size.pow(2u);
			// Rescale the block sizes according to the relative sizes of the input series.
			auto block_size1 = (block_size * size1) / size2,
				block_size2 = (block_size * size2) / size1;
			// Avoid having zero block sizes, or block sizes exceeding job_size.
			if (!block_size1) {
				block_size1 = 1u;
			} else if (block_size1 > job_size) {
				block_size1 = job_size;
			}
			if (!block_size2) {
				block_size2 = 1u;
			} else if (block_size2 > job_size) {
				block_size2 = job_size;
			}
			return std::make_pair(std::move(block_size1),std::move(block_size2));
		}
		// Compare regions by lower bound.
		struct region_comparer
		{
			bool operator()(const region_type &r1, const region_type &r2) const
			{
				return r1.first < r2.first;
			}
		};
		// Function to remove the regions associated to a task from the set of busy regions.
		template <typename RegionSet>
		static void cleanup_regions(RegionSet &busy_regions, const task_type &task)
		{
			auto tmp_it = busy_regions.find(task.m_r1);
			if (tmp_it != busy_regions.end()) {
				busy_regions.erase(tmp_it);
			}
			// Deal with second region only if present.
			if (!task.m_second_region) {
				return;
			}
			tmp_it = busy_regions.find(task.m_r2);
			if (tmp_it != busy_regions.end()) {
				busy_regions.erase(tmp_it);
			}
			piranha_assert(region_set_checker(busy_regions));
		}
		// Dense task sorter.
		template <typename NKType>
		struct dts_type
		{
			explicit dts_type(const std::vector<NKType> &nk1, const std::vector<NKType> &nk2):
				m_new_keys1(nk1),m_new_keys2(nk2)
			{}
			template <typename Task>
			bool operator()(const Task &t1, const Task &t2) const
			{
				return m_new_keys1[t1.m_b1.first].first + m_new_keys2[t1.m_b2.first].first <
					m_new_keys1[t2.m_b1.first].first + m_new_keys2[t2.m_b2.first].first;
			}
			const std::vector<NKType>	&m_new_keys1;
			const std::vector<NKType>	&m_new_keys2;
		};
		// Dense multiplication method.
		void dense_multiplication(Series &retval, const unsigned &n_threads) const
		{
			// Vectors of minimum / maximum values, cast to hardware int.
			std::vector<value_type> mins;
			std::transform(m_minmax_values.begin(),m_minmax_values.end(),std::back_inserter(mins),[](const std::pair<integer,integer> &p) {
				return static_cast<value_type>(p.first);
			});
			std::vector<value_type> maxs;
			std::transform(m_minmax_values.begin(),m_minmax_values.end(),std::back_inserter(maxs),[](const std::pair<integer,integer> &p) {
				return static_cast<value_type>(p.second);
			});
			// Build the encoding vector.
			std::vector<value_type> c_vec;
			integer f_delta(1);
			std::transform(m_minmax_values.begin(),m_minmax_values.end(),std::back_inserter(c_vec),
				[&f_delta](const std::pair<integer,integer> &p) -> value_type {
					auto old(f_delta);
					f_delta *= p.second - p.first + 1;
					return static_cast<value_type>(old);
			});
			// Try casting final delta.
			(void)static_cast<value_type>(f_delta);
			// Compute hmax and hmin.
			piranha_assert(m_minmax_values.size() == c_vec.size());
			const auto h_minmax = std::inner_product(m_minmax_values.begin(),m_minmax_values.end(),c_vec.begin(),
				std::make_pair(integer(0),integer(0)),
				[](const std::pair<integer,integer> &p1, const std::pair<integer,integer> &p2) {
					return std::make_pair(p1.first + p2.first,p1.second + p2.second);
				},[](const std::pair<integer,integer> &p, const value_type &value) {
					return std::make_pair(p.first * value,p.second * value);
				}
			);
			piranha_assert(f_delta == h_minmax.second - h_minmax.first + 1);
			// Try casting hmax and hmin.
			const auto hmin = static_cast<value_type>(h_minmax.first);
			const auto hmax = static_cast<value_type>(h_minmax.second);
			// Encoding functor.
			typedef typename term_type::key_type::v_type unpack_type;
			auto encoder = [&c_vec,hmin,&mins](const unpack_type &v) -> value_type {
				piranha_assert(c_vec.size() == v.size());
				piranha_assert(c_vec.size() == mins.size());
				decltype(mins.size()) i = 0u;
				value_type retval = std::inner_product(c_vec.begin(),c_vec.end(),v.begin(),value_type(0),
					std::plus<value_type>(),[&i,&mins](const value_type &c, const value_type &n) -> value_type {
						const decltype(mins.size()) old_i = i;
						++i;
						piranha_assert(n >= mins[old_i]);
						return static_cast<value_type>(c * (n - mins[old_i]));
					}
				);
				return static_cast<value_type>(retval + hmin);
			};
			// Build copies of the input keys repacked according to the new Kronecker substitution. Attach
			// also a pointer to the term.
			typedef std::pair<value_type,term_type const *> new_key_type;
			std::vector<new_key_type> new_keys1, new_keys2;
			std::transform(this->m_v1.begin(),this->m_v1.end(),std::back_inserter(new_keys1),
				[this,encoder](term_type const *ptr) {
				return std::make_pair(encoder(ptr->m_key.unpack(this->m_s1->m_symbol_set)),ptr);
			});
			std::transform(this->m_v2.begin(),this->m_v2.end(),std::back_inserter(new_keys2),
				[this,encoder](term_type const *ptr) {
				return std::make_pair(encoder(ptr->m_key.unpack(this->m_s1->m_symbol_set)),ptr);
			});
			// Sort the the new keys.
			std::stable_sort(new_keys1.begin(),new_keys1.end(),[](const new_key_type &p1, const new_key_type &p2) {
				return p1.first < p2.first;
			});
			std::stable_sort(new_keys2.begin(),new_keys2.end(),[](const new_key_type &p1, const new_key_type &p2) {
				return p1.first < p2.first;
			});
			// Store the sizes and compute the block sizes.
			const index_type size1 = boost::numeric_cast<index_type>(new_keys1.size()),
				size2 = boost::numeric_cast<index_type>(new_keys2.size());
			piranha_assert(size1 == this->m_s1->size());
			piranha_assert(size2 == this->m_s2->size());
			const auto bsizes = get_block_sizes(size1,size2);
			// Cast to hardware integers.
			const auto bsize1 = static_cast<index_type>(bsizes.first), bsize2 = static_cast<index_type>(bsizes.second);
			// Build the list of tasks.
			dts_type<new_key_type> dense_task_sorter(new_keys1,new_keys2);
			std::multiset<task_type,dts_type<new_key_type>> task_list(dense_task_sorter);
			decltype(task_list.insert(std::declval<task_type>())) ins_result;
			auto dense_task_from_indices = [hmin,hmax,&new_keys1,&new_keys2](const index_type &i_start, const index_type &i_end,
				const index_type &j_start, const index_type &j_end) -> task_type
			{
				piranha_assert(i_end <= new_keys1.size() && j_end <= new_keys2.size());
				piranha_assert(i_start < i_end && j_start < j_end);
				const bucket_size_type a = boost::numeric_cast<bucket_size_type>((new_keys1[i_start].first + new_keys2[j_start].first) - hmin);
				const bucket_size_type b = boost::numeric_cast<bucket_size_type>((new_keys1[i_end - 1u].first + new_keys2[j_end - 1u].first) - hmin);
				piranha_assert(a <= b);
				piranha_assert(b <= boost::numeric_cast<bucket_size_type>(hmax - hmin));
				return task_type{std::make_pair(i_start,i_end),std::make_pair(j_start,j_end),
					std::make_pair(a,b),std::make_pair(bucket_size_type(0u),bucket_size_type(0u)),false};
			};
			for (index_type i = 0u; i < size1 / bsize1; ++i) {
				for (index_type j = 0u; j < size2 / bsize2; ++j) {
					ins_result = task_list.insert(dense_task_from_indices(i * bsize1,(i + 1u) * bsize1,j * bsize2,(j + 1u) * bsize2));
					piranha_assert(ins_result->m_b1.first != ins_result->m_b1.second && ins_result->m_b2.first != ins_result->m_b2.second);
				}
				if (size2 % bsize2) {
					ins_result = task_list.insert(dense_task_from_indices(i * bsize1,(i + 1u) * bsize1,(size2 / bsize2) * bsize2,size2));
					piranha_assert(ins_result->m_b1.first != ins_result->m_b1.second && ins_result->m_b2.first != ins_result->m_b2.second);
				}
			}
			if (size1 % bsize1) {
				for (index_type j = 0u; j < size2 / bsize2; ++j) {
					ins_result = task_list.insert(dense_task_from_indices((size1 / bsize1) * bsize1,size1,j * bsize2,(j + 1u) * bsize2));
					piranha_assert(ins_result->m_b1.first != ins_result->m_b1.second && ins_result->m_b2.first != ins_result->m_b2.second);
				}
				if (size2 % bsize2) {
					ins_result = task_list.insert(dense_task_from_indices((size1 / bsize1) * bsize1,size1,(size2 / bsize2) * bsize2,size2));
					piranha_assert(ins_result->m_b1.first != ins_result->m_b1.second && ins_result->m_b2.first != ins_result->m_b2.second);
				}
			}
			// Prepare the storage for multiplication.
			// NOTE: init everything explicitly to zero, as we make no assumption about the value of a default-cted
			// coefficient.
			using cf_vector_type = std::vector<typename term_type::cf_type>;
			cf_vector_type cf_vector(boost::numeric_cast<typename cf_vector_type::size_type>((hmax - hmin) + 1),
				typename term_type::cf_type(0));
			if (n_threads == 1u) {
				// Single-thread multiplication.
				const auto it_f = task_list.end();
				for (auto it = task_list.begin(); it != it_f; ++it) {
					const index_type i_start = it->m_b1.first, j_start = it->m_b2.first,
						i_end = it->m_b1.second, j_end = it->m_b2.second;
					piranha_assert(i_end > i_start && j_end > j_start);
					for (index_type i = i_start; i < i_end; ++i) {
						for (index_type j = j_start; j < j_end; ++j) {
							const auto idx = (new_keys1[i].first + new_keys2[j].first) - hmin;
							piranha_assert(idx < boost::numeric_cast<value_type>(cf_vector.size()));
							math::multiply_accumulate(cf_vector[static_cast<decltype(cf_vector.size())>(idx)],
								new_keys1[i].second->m_cf,new_keys2[j].second->m_cf);
						}
					}
				}
			} else {
				// Set of busy bucket regions, ordered by starting point.
				// NOTE: we do not need a multiset, as regions that compare equal (i.e., same starting point) will not exist
				// at the same time in the set by design.
				region_comparer rc;
				std::set<region_type,region_comparer> busy_regions(rc);
				// Synchronization.
				std::mutex m;
				std::condition_variable cond;
				// Thread function.
				auto thread_function = [&cond,&m,&task_list,&busy_regions,&new_keys1,&new_keys2,hmin,&cf_vector,this] () {
					task_type task;
					while (true) {
						{
							// First, lock down everything.
							std::unique_lock<std::mutex> lock(m);
							if (task_list.empty()) {
								break;
							}
							// Look for a suitable task.
							const auto it_f = task_list.end();
							auto it = task_list.begin();
							for (; it != it_f; ++it) {
								piranha_assert(!it->m_second_region);
								// Check the region.
								if (this->region_checker(it->m_r1,busy_regions)) {
									try {
										// The region is ok, insert it and break the cycle.
										auto tmp = busy_regions.insert(it->m_r1);
										(void)tmp;
										piranha_assert(tmp.second);
										piranha_assert(this->region_set_checker(busy_regions));
									} catch (...) {
										// NOTE: the idea here is that in case of errors
										// we want to restore the original situation
										// as if nothing happened.
										this->cleanup_regions(busy_regions,*it);
										throw;
									}
									break;
								}
							}
							// We might have identified a suitable task, check it is not end().
							if (it == it_f) {
								// The thread can't do anything, will have to wait until something happens
								// and then re-identify a good task.
								cond.wait(lock);
								continue;
							}
							// Now we have a good task, pop it from the task list.
							task = *it;
							task_list.erase(it);
						}
						try {
							// Perform the multiplication on the selected task.
							const index_type i_start = task.m_b1.first, j_start = task.m_b2.first,
								i_end = task.m_b1.second, j_end = task.m_b2.second;
							piranha_assert(i_end > i_start && j_end > j_start);
							for (index_type i = i_start; i < i_end; ++i) {
								for (index_type j = j_start; j < j_end; ++j) {
									const auto idx = (new_keys1[i].first + new_keys2[j].first) - hmin;
									piranha_assert(idx < boost::numeric_cast<value_type>(cf_vector.size()));
									math::multiply_accumulate(cf_vector[static_cast<decltype(cf_vector.size())>(idx)],
										new_keys1[i].second->m_cf,new_keys2[j].second->m_cf);
								}
							}
						} catch (...) {
							// Re-acquire the lock.
							std::lock_guard<std::mutex> lock(m);
							// Cleanup the regions.
							this->cleanup_regions(busy_regions,task);
							// Notify all waiting threads that a region was removed from the busy set.
							cond.notify_all();
							throw;
						}
						{
							// Re-acquire the lock.
							std::lock_guard<std::mutex> lock(m);
							// Take out the regions in which we just wrote from the set of busy regions.
							this->cleanup_regions(busy_regions,task);
							// Notify all waiting threads that a region was removed from the busy set.
							cond.notify_all();
						}
					}
				};
				// NOTE: one of the fundamental requirements here is that we wait for pending
				// tasks, in case of errors, before getting out of this function: thread_function
				// contains references to local variables, if we get out of here before the tasks are finished
				// memory corruption will occur.
				future_list<decltype(thread_pool::enqueue(0u,thread_function))> f_list;
				try {
					for (unsigned i = 0u; i < n_threads; ++i) {
						// NOTE: enqueue() will either happen or it won't, we only care
						// about memory allocation errors in push_back() here. In such case,
						// push_back() will wait on the temporary future from enqueue
						// before returning the exception.
						f_list.push_back(thread_pool::enqueue(i,thread_function));
					}
					// First let's wait for everything to finish.
					f_list.wait_all();
					// Then, let's handle the exceptions.
					f_list.get_all();
				} catch (...) {
					// Make sure any pending task is finished -> this is for
					// the case the exception was thrown in the future creation
					// loop. It is safe to call this again in case the exception
					// being handled is generated by get_all(), as wait_all() will check
					// the validity of the future before calling wait().
					f_list.wait_all();
					throw;
				}
			}
			// Build the return value.
			// Append the final delta to the coding vector for use in the decoding routine.
			c_vec.push_back(static_cast<value_type>(f_delta));
			// Temp vector for decoding.
			std::vector<value_type> tmp_v;
			tmp_v.resize(boost::numeric_cast<decltype(tmp_v.size())>(this->m_s1->m_symbol_set.size()));
			piranha_assert(c_vec.size() - 1u == tmp_v.size());
			piranha_assert(mins.size() == tmp_v.size());
			auto decoder = [&tmp_v,&c_vec,&mins,&maxs](const value_type &n) {
				decltype(c_vec.size()) i = 0u;
				std::generate(tmp_v.begin(),tmp_v.end(),
					[&n,&i,&mins,&maxs,&c_vec]() -> value_type
				{
					auto retval = (n % c_vec[i + 1u]) / c_vec[i] + mins[i];
					piranha_assert(retval >= mins[i] && retval <= maxs[i]);
					++i;
					return static_cast<value_type>(retval);
				});
			};
			const auto cf_size = cf_vector.size();
			term_type tmp_term;
			for (decltype(cf_vector.size()) i = 0u; i < cf_size; ++i) {
				if (!math::is_zero(cf_vector[i])) {
					tmp_term.m_cf = std::move(cf_vector[i]);
					decoder(boost::numeric_cast<value_type>(i));
					tmp_term.m_key = decltype(tmp_term.m_key)(tmp_v.begin(),tmp_v.end());
					retval.insert(std::move(tmp_term));
				}
			}
		}
		// Struct for extracting bucket indices.
		// NOTE: use this instead of a lambda, since boost transform iterator needs the function
		// object to be assignable.
		struct sparse_bi_extractor
		{
			// NOTE: in some setups Boost is apparently unable to deduce the result type
			// of the functor and needs this typedef in the transform iterator.
			using result_type = bucket_size_type;
			explicit sparse_bi_extractor(const Series *retval) : m_retval(retval) {}
			template <typename Term>
			result_type operator()(const Term *t) const
			{
				return m_retval->m_container._bucket_from_hash(t->hash());
			}
			const Series *m_retval;
		};
		template <typename Functor>
		void sparse_multiplication(Series &retval,const unsigned &n_threads) const
		{
			// Type representing multiplication tasks.
			using task_type = std::tuple<term_type const *,term_type const **,term_type const **>;
			// A couple of handy shortcuts.
			using diff_type = std::ptrdiff_t;
			// NOTE: this is always legal as ptrdiff_t is a signed integer.
			using udiff_type = typename std::make_unsigned<diff_type>::type;
			// Fast functor type.
			using fast_functor_type = typename Functor::fast_rebind;
			// Block size. Tasks will be split into chunks with this max size.
			const diff_type block_size = boost::numeric_cast<diff_type>(tuning::get_multiplication_block_size());
			// Sort input terms according to bucket positions in retval.
			auto cmp = [&retval](term_type const *p1, term_type const *p2)
			{
				return retval.m_container._bucket_from_hash(p1->hash()) <
					retval.m_container._bucket_from_hash(p2->hash());
			};
			std::stable_sort(this->m_v1.begin(),this->m_v1.end(),cmp);
			std::stable_sort(this->m_v2.begin(),this->m_v2.end(),cmp);
			// Variable used to keep track of total unique insertions in retval.
			bucket_size_type insertion_count = 0u;
			// Number of buckets in retval.
			const bucket_size_type bucket_count = retval.m_container.bucket_count();
			// Special casing for single-thread.
			if (n_threads == 1u) {
				// Reduced version of the algorithm in single-threaded mode. See below for comments.
				try {
					integer n_mults(0);
					auto &v1 = this->m_v1;
					auto &v2 = this->m_v2;
					const auto size1 = v1.size();
					const auto size2 = v2.size();
					std::vector<task_type> tasks;
					term_type const **start, **end;
					for (decltype(v1.size()) i = 0u; i < size1; ++i) {
						start = &v2[0u];
						end = &v2[0u] + size2;
						while (end - start > block_size) {
							tasks.emplace_back(v1[i],start,start + block_size);
							start += block_size;
						}
						if (end != start) {
							tasks.emplace_back(v1[i],start,end);
						}
					}
					std::stable_sort(tasks.begin(),tasks.end(),[&retval](const task_type &t1, const task_type &t2) {
						return retval.m_container._bucket_from_hash(std::get<0u>(t1)->hash()) + retval.m_container._bucket_from_hash((*std::get<1u>(t1))->hash()) <
							retval.m_container._bucket_from_hash(std::get<0u>(t2)->hash()) + retval.m_container._bucket_from_hash((*std::get<1u>(t2))->hash());
					});
					for (const auto &t: tasks) {
						auto t1_ptr = std::get<0u>(t);
						auto start = std::get<1u>(t), end = std::get<2u>(t);
						const auto size = static_cast<index_type>(end - start);
						piranha_assert(size != 0u);
						fast_functor_type f(&t1_ptr,1u,start,size,retval);
						for (index_type i = 0u; i < size; ++i) {
							f(0u,i);
							f.insert();
						}
						piranha_assert((n_mults += size,true));
						insertion_count = static_cast<bucket_size_type>(insertion_count + f.m_insertion_count);
					}
					sanitize_series(retval,insertion_count,n_threads);
					piranha_assert(n_mults == integer(this->m_v1.size()) * this->m_v2.size());
					return;
				} catch (...) {
					retval.m_container.clear();
					throw;
				}
			}
			// Determine buckets per thread.
			const auto bpt = static_cast<bucket_size_type>(bucket_count / n_threads);
			// Will use to sync access to common vars.
			std::mutex m;
			// Debug variable.
			using vi_size_type = std::vector<integer>::size_type;
			std::vector<integer> n_mults(boost::numeric_cast<vi_size_type>(n_threads));
			auto thread_function = [n_threads,bpt,bucket_count,this,&retval,&m,&n_mults,&insertion_count,block_size] (const unsigned &idx) {
				// Cache some quantities from this.
				auto &v1 = this->m_v1;
				auto &v2 = this->m_v2;
				const auto size1 = v1.size();
				const auto size2 = v2.size();
				// Range of bucket indices into which the thread is allowed to write.
				const auto a = static_cast<bucket_size_type>(bpt * idx),
					b = (idx == n_threads - 1u) ? bucket_count : static_cast<bucket_size_type>(bpt * (idx + 1u));
				// Bucket index extractor.
				const sparse_bi_extractor bi_ex{&retval};
				// Start and end of task in v2, to be used in the loop.
				term_type const **start, **end;
				// Check the safety of computing "end - start" in the loop.
				if (unlikely(v2.size() > static_cast<udiff_type>(std::numeric_limits<diff_type>::max()))) {
					piranha_throw(std::overflow_error,"the second operand in a sparse polynomial multiplication is too large");
				}
				// Vector of term multiplication tasks to be undertaken by the thread.
				std::vector<task_type> tasks;
				// Transform iterators that will compute the bucket indices from the terms in v2.
				const auto t_start2 = boost::make_transform_iterator(&v2[0u],bi_ex),
					t_end2 = boost::make_transform_iterator(&v2[0u] + size2,bi_ex);
				for (decltype(v1.size()) i = 0u; i < size1; ++i) {
					const bucket_size_type n = bi_ex(v1[i]);
					start = (a < n) ? &v2[0u] :
						std::lower_bound(t_start2,t_end2,bucket_size_type(a - n)).base();
					end = (b < n) ? &v2[0u] :
						std::lower_bound(t_start2,t_end2,bucket_size_type(b - n)).base();
					// Split into smaller blocks.
					while (end - start > block_size) {
						tasks.emplace_back(v1[i],start,start + block_size);
						start += block_size;
					}
					if (end != start) {
						tasks.emplace_back(v1[i],start,end);
					}
					// Second batch.
					// NOTE: a (or b) + bucket_count is always in the range of bucket_size_type as the maximum bucket size
					// of a hash_set is 2**(n-1), where bucket_size_type has a bit width of n.
					start = ((a + bucket_count) < n) ? &v2[0u] :
						std::lower_bound(t_start2,t_end2,bucket_size_type((a + bucket_count) - n)).base();
					end = ((b + bucket_count) < n) ? &v2[0u] :
						std::lower_bound(t_start2,t_end2,bucket_size_type((b + bucket_count) - n)).base();
					while (end - start > block_size) {
						tasks.emplace_back(v1[i],start,start + block_size);
						start += block_size;
					}
					if (end != start) {
						tasks.emplace_back(v1[i],start,end);
					}
				}
				// Sort the tasks in ascending order for the first write bucket index.
				std::stable_sort(tasks.begin(),tasks.end(),[&retval](const task_type &t1, const task_type &t2) {
					return retval.m_container._bucket_from_hash(std::get<0u>(t1)->hash()) + retval.m_container._bucket_from_hash((*std::get<1u>(t1))->hash()) <
						retval.m_container._bucket_from_hash(std::get<0u>(t2)->hash()) + retval.m_container._bucket_from_hash((*std::get<1u>(t2))->hash());
				});
				// Perform the multiplications.
				bucket_size_type ins_count(0);
				for (const auto &t: tasks) {
					auto t1_ptr = std::get<0u>(t);
					auto start = std::get<1u>(t), end = std::get<2u>(t);
					// NOTE: cast is safe, end - start cannot be larger than the size of v2.
					const auto size = static_cast<index_type>(end - start);
					piranha_assert(size != 0u);
					// NOTE: here we will need to re-evaluate the use of this functor, as now the blocks have
					// always a size of 1 term in the first series. There might be some performance gains to be had
					// by switching to a simpler implementation.
					fast_functor_type f(&t1_ptr,1u,start,size,retval);
					for (index_type i = 0u; i < size; ++i) {
						f(0u,i);
						piranha_assert(retval.m_container._bucket_from_hash(f.m_tmp[0u].hash()) >= a &&
							retval.m_container._bucket_from_hash(f.m_tmp[0u].hash()) < b);
						f.insert();
					}
					piranha_assert((n_mults[static_cast<vi_size_type>(idx)] += size,true));
					ins_count = static_cast<bucket_size_type>(ins_count + f.m_insertion_count);
				}
				// Final update of the insertion count, must be protected.
				std::lock_guard<std::mutex> lock(m);
				insertion_count += ins_count;
			};
			// Go with the threads.
			future_list<decltype(thread_pool::enqueue(0u,thread_function,0u))> f_list;
			try {
				for (unsigned i = 0u; i < n_threads; ++i) {
					f_list.push_back(thread_pool::enqueue(i,thread_function,i));
				}
				// First let's wait for everything to finish.
				f_list.wait_all();
				// Then, let's handle the exceptions.
				f_list.get_all();
				// Finally, fix the series.
				sanitize_series(retval,insertion_count,n_threads);
			} catch (...) {
				f_list.wait_all();
				// Clean up and re-throw.
				retval.m_container.clear();
				throw;
			}
			// Check that we performed all the multiplications.
			piranha_assert(std::accumulate(n_mults.begin(),n_mults.end(),integer(0)) == integer(this->m_v1.size()) * this->m_v2.size());
		}
		// Sanitize series after completion of sparse multiplication.
		static void sanitize_series(Series &retval, const bucket_size_type &insertion_count, unsigned n_threads = 1u)
		{
			// Here we have to do the following things:
			// - check ignorability of terms,
			// - cope with excessive load factor,
			// - update the size of the series.
			// Compatibility is not a concern for polynomials.
			// First, let's fix the size of inserted terms.
			retval.m_container._update_size(insertion_count);
			// Second, erase the ignorable terms.
			if (n_threads == 1u) {
				const auto it_f = retval.m_container.end();
				for (auto it = retval.m_container.begin(); it != it_f;) {
					if (unlikely(it->is_ignorable(retval.m_symbol_set))) {
						it = retval.m_container.erase(it);
					} else {
						++it;
					}
				}
			} else {
				const auto b_count = retval.m_container.bucket_count();
				piranha_assert(b_count);
				// Adjust the number of threads if they are more than the bucket count.
				const unsigned nt = (n_threads <= b_count) ? n_threads : static_cast<unsigned>(b_count);
				std::mutex m;
				auto eraser = [b_count,&retval,&m](const bucket_size_type &start, const bucket_size_type &end) {
					piranha_assert(start < end && end <= b_count);
					bucket_size_type erase_count = 0u;
					std::vector<term_type> term_list;
					for (bucket_size_type i = start; i != end; ++i) {
						term_list.clear();
						const auto &bl = retval.m_container._get_bucket_list(i);
						const auto it_f = bl.end();
						for (auto it = bl.begin(); it != it_f; ++it) {
							if (unlikely(it->is_ignorable(retval.m_symbol_set))) {
								term_list.push_back(*it);
							}
						}
						for (auto it = term_list.begin(); it != term_list.end(); ++it) {
							// NOTE: must use _erase to avoid concurrent modifications
							// to the number of elements in the table.
							retval.m_container._erase(retval.m_container._find(*it,i));
							++erase_count;
						}
					}
					if (erase_count) {
						std::lock_guard<std::mutex> lock(m);
						piranha_assert(erase_count <= retval.m_container.size());
						retval.m_container._update_size(retval.m_container.size() - erase_count);
					}
				};
				future_list<decltype(thread_pool::enqueue(0u,eraser,bucket_size_type(),bucket_size_type()))> f_list;
				try {
					for (unsigned i = 0u; i < nt; ++i) {
						const auto start = static_cast<bucket_size_type>((b_count / nt) * i),
							end = static_cast<bucket_size_type>((i == nt - 1u) ? b_count : (b_count / nt) * (i + 1u));
						f_list.push_back(thread_pool::enqueue(i,eraser,start,end));
					}
					// First let's wait for everything to finish.
					f_list.wait_all();
					// Then, let's handle the exceptions.
					f_list.get_all();
				} catch (...) {
					f_list.wait_all();
					// Clean up and re-throw.
					retval.m_container.clear();
					throw;
				}
			}
			// Finally, cope with excessive load factor.
			if (unlikely(retval.m_container.load_factor() > retval.m_container.max_load_factor())) {
				retval.m_container.rehash(
					boost::numeric_cast<bucket_size_type>(std::ceil(static_cast<double>(retval.m_container.size()) / retval.m_container.max_load_factor())),
					n_threads
				);
			}
		}
		// Functor for use in sparse multiplication.
		template <bool FastMode = false>
		struct sparse_functor: base::default_functor
		{
			// Fast version of functor.
			typedef sparse_functor<true> fast_rebind;
			// NOTE: here the coefficient of m_tmp gets default-inited explicitly by the default constructor of base term.
			explicit sparse_functor(term_type const **ptr1, const index_type &s1,
				term_type const **ptr2, const index_type &s2, Series &retval):
				base::default_functor(ptr1,s1,ptr2,s2,retval),
				m_cached_i(0u),m_cached_j(0u),m_insertion_count(0u)
			{}
			void operator()(const index_type &i, const index_type &j) const
			{
				using int_type = decltype(this->m_ptr1[i]->m_key.get_int());
				piranha_assert(i < this->m_s1 && j < this->m_s2);
				this->m_tmp[0u].m_key.set_int(static_cast<int_type>(this->m_ptr1[i]->m_key.get_int() + this->m_ptr2[j]->m_key.get_int()));
				m_cached_i = i;
				m_cached_j = j;
			}
			void insert() const
			{
				// NOTE: be very careful: every kind of optimization in here must involve only the key part,
				// as the coefficient part is still generic.
				auto &container = this->m_retval.m_container;
				auto &tmp = this->m_tmp[0u];
				const auto &cf1 = this->m_ptr1[m_cached_i]->m_cf;
				const auto &cf2 = this->m_ptr2[m_cached_j]->m_cf;
				const auto &args = this->m_retval.m_symbol_set;
				// Prepare the return series.
				piranha_assert(!FastMode || container.bucket_count());
				if (!FastMode && unlikely(!container.bucket_count())) {
					container._increase_size();
				}
				// Try to locate the term into retval.
				auto bucket_idx = container._bucket(tmp);
				const auto it = container._find(tmp,bucket_idx);
				if (it == container.end()) {
					// NOTE: the check here is done outside.
					piranha_assert(container.size() < std::numeric_limits<bucket_size_type>::max());
					// Term is new. Handle the case in which we need to rehash because of load factor.
					if (!FastMode && unlikely(static_cast<double>(container.size() + bucket_size_type(1u)) / static_cast<double>(container.bucket_count()) >
						container.max_load_factor()))
					{
						container._increase_size();
						// We need a new bucket index in case of a rehash.
						bucket_idx = container._bucket(tmp);
					}
					// TODO optimize this in case of series and integer (?), now it is optimized for simple coefficients.
					// Note that the best course of action here for integer multiplication would seem to resize tmp.m_cf appropriately
					// and then use something like mpz_mul. On the other hand, it seems like in the insertion below we need to perform
					// a copy anyway, so insertion with move seems ok after all? Mmmh...
					// TODO: other important thing: for coefficient series, we probably want to insert with move() below,
					// as we are not going to re-use the allocated resources in tmp.m_cf -> in other words, optimize this
					// as much as possible.
					// Take care of multiplying the coefficient.
					tmp.m_cf = cf1;
					tmp.m_cf *= cf2;
					// Insert and update size.
					// NOTE: in fast mode, the check will be done at the end.
					// NOTE: the counters are protected from overflows by the check done in the operator() of the multiplier.
					if (FastMode) {
						container._unique_insert(tmp,bucket_idx);
						++m_insertion_count;
					} else if (likely(!tmp.is_ignorable(args))) {
						container._unique_insert(tmp,bucket_idx);
						container._update_size(container.size() + 1u);
					}
				} else {
					// Assert the existing term is not ignorable, in non-fast mode.
					piranha_assert(FastMode || !it->is_ignorable(args));
					if (FastMode) {
						// In fast mode we do not care if this throws or produces a null coefficient,
						// as we will be dealing with that from outside.
						math::multiply_accumulate(it->m_cf,cf1,cf2);
					} else {
						// Cleanup function.
						auto cleanup = [&it,&args,&container]() {
							if (unlikely(it->is_ignorable(args))) {
								container.erase(it);
							}
						};
						try {
							math::multiply_accumulate(it->m_cf,cf1,cf2);
							// Check if the term has become ignorable or incompatible after the modification.
							cleanup();
						} catch (...) {
							// In case of exceptions, do the check before re-throwing.
							cleanup();
							throw;
						}
					}
				}
			}
			mutable index_type		m_cached_i;
			mutable index_type		m_cached_j;
			mutable bucket_size_type	m_insertion_count;
		};
	private:
		// Vector of closed ranges of the exponents in both the operands and the result.
		std::vector<std::pair<integer,integer>> m_minmax_values;
};

}

#endif
