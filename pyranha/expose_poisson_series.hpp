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

#ifndef PYRANHA_EXPOSE_POISSON_SERIES_HPP
#define PYRANHA_EXPOSE_POISSON_SERIES_HPP

#include <boost/python/class.hpp>
#include <type_traits>
#include <utility>

#include "../src/detail/sfinae_types.hpp"

namespace pyranha
{

namespace bp = boost::python;

// Custom hook for Poisson series.
struct ps_custom_hook
{
	// Detect and enable t_integrate() conditionally.
	template <typename T>
	struct has_t_integrate: piranha::detail::sfinae_types
	{
		template <typename T1>
		static auto test(const T1 &x) -> decltype(x.t_integrate(),void(),yes());
		static no test(...);
		static const bool value = std::is_same<decltype(test(std::declval<T>())),yes>::value;
	};
	template <typename S>
	static auto t_integrate_wrapper(const S &s) -> decltype(s.t_integrate())
	{
		return s.t_integrate();
	}
	template <typename S, typename std::enable_if<has_t_integrate<S>::value,int>::type = 0>
	static void expose_t_integrate(bp::class_<S> &series_class)
	{
		series_class.def("t_integrate",t_integrate_wrapper<S>);
	}
	template <typename S, typename std::enable_if<!has_t_integrate<S>::value,int>::type = 0>
	static void expose_t_integrate(bp::class_<S> &)
	{}
	template <typename T>
	void operator()(bp::class_<T> &series_class) const
	{
		expose_t_integrate(series_class);
	}
};

void expose_poisson_series_0();
void expose_poisson_series_1();
void expose_poisson_series_2();
void expose_poisson_series_3();
void expose_poisson_series_4();
void expose_poisson_series_5();

}

#endif
