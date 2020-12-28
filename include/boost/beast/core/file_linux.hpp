//
// Copyright (c) 2015-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

#ifndef BOOST_BEAST_CORE_FILE_LINUX_HPP
#define BOOST_BEAST_CORE_FILE_LINUX_HPP

#include <boost/beast/core/detail/config.hpp>

#if ! defined(BOOST_BEAST_NO_LINUX_FILE)
# if ! defined(__linux__)
#  define BOOST_BEAST_NO_LINUX_FILE
# endif
#endif

#if ! defined(BOOST_BEAST_USE_LINUX_FILE)
# if ! defined(BOOST_BEAST_NO_LINUX_FILE)
#  if defined(__linux__)
#   define BOOST_BEAST_USE_LINUX_FILE 1
#  else
#   define BOOST_BEAST_USE_LINUX_FILE 0
#  endif
# endif
#endif

#if BOOST_BEAST_USE_LINUX_FILE

#include <boost/beast/core/error.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/file_posix.hpp>
#include <cstdint>

namespace boost {
namespace beast {

/** An implementation of File for LINUX systems.

    This class implements a <em>File</em> using LINUX interfaces.
*/
class file_linux: public file_posix
{
public:
    using file_posix::file_posix;
};

} // beast
} // boost

#ifdef BOOST_BEAST_HEADER_ONLY
#include <boost/beast/core/impl/file_posix.ipp>
#endif

#endif

#endif
