//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

#ifndef BOOST_BEAST_HTTP_IMPL_FILE_BODY_LINUX_HPP
#define BOOST_BEAST_HTTP_IMPL_FILE_BODY_LINUX_HPP

//#if BOOST_BEAST_USE_LINUX_FILE

#include <boost/beast/core/async_base.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/core/buffers_range.hpp>
#include <boost/beast/core/detail/clamp.hpp>
#include <boost/beast/core/detail/is_invocable.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/make_unique.hpp>
#include <boost/smart_ptr/make_shared_array.hpp>
#include <algorithm>
#include <cstring>
#include <csignal>
#include <sys/sendfile.h>
#include <unistd.h>
#include <iostream>

namespace boost {
namespace beast {
namespace http {

namespace detail {
template<class, class, bool, class, class>
class write_some_linux_op;
} // detail

template<>
struct basic_file_body<file_linux>
{
    using file_type = file_linux;

    class writer;
    class reader;

    //--------------------------------------------------------------------------

    class value_type
    {
        friend class writer;
        friend class reader;
        friend struct basic_file_body<file_linux>;

        template<class, class, bool, class, class>
        friend class detail::write_some_linux_op;
        template<
            class Protocol, class Executor,
            bool isRequest, class Fields>
        friend
        std::size_t
        write_some(
            net::basic_stream_socket<Protocol, Executor>& sock,
            serializer<isRequest,
                basic_file_body<file_linux>, Fields>& sr,
            error_code& ec);

        file_linux file_;
        std::uint64_t size_ = 0;    // cached file size
        std::uint64_t first_;       // starting offset of the range
        std::uint64_t last_;        // ending offset of the range

    public:
        ~value_type() = default;
        value_type() = default;
        value_type(value_type&& other) = default;
        value_type& operator=(value_type&& other) = default;

        file_linux& file()
        {
            return file_;
        }

        bool
        is_open() const
        {
            return file_.is_open();
        }

        std::uint64_t
        size() const
        {
            return size_;
        }

        void
        close();

        void
        open(char const* path, file_mode mode, error_code& ec);

        void
        reset(file_linux&& file, error_code& ec);
    };

    //--------------------------------------------------------------------------

    class writer
    {
        template<class, class, bool, class, class>
        friend class detail::write_some_linux_op;
        template<
            class Protocol, class Executor,
            bool isRequest, class Fields>
        friend
        std::size_t
        write_some(
            net::basic_stream_socket<Protocol, Executor>& sock,
            serializer<isRequest,
                basic_file_body<file_linux>, Fields>& sr,
            error_code& ec);

        value_type& body_;  // The body we are reading from
        std::uint64_t pos_; // The current position in the file
        char buf_[4096];    // Small buffer for reading

    public:
        using const_buffers_type =
            net::const_buffer;

        template<bool isRequest, class Fields>
        writer(header<isRequest, Fields>&, value_type& b)
            : body_(b)
            , pos_(body_.first_)
        {
            BOOST_ASSERT(body_.file_.is_open());
        }

        void
        init(error_code& ec)
        {
            BOOST_ASSERT(body_.file_.is_open());
            ec.clear();
        }

        boost::optional<std::pair<const_buffers_type, bool>>
        get(error_code& ec)
        {
            std::size_t const n = (std::min)(sizeof(buf_),
                beast::detail::clamp(body_.last_ - pos_));
            if(n == 0)
            {
                ec = {};
                return boost::none;
            }
            auto const nread = body_.file_.read(buf_, n, ec);
            if(ec)
                return boost::none;
            if (nread == 0)
            {
                ec = error::short_read;
                return boost::none;
            }
            BOOST_ASSERT(nread != 0);
            pos_ += nread;
            ec = {};
            return {{
                {buf_, nread},          // buffer to return.
                pos_ < body_.last_}};   // `true` if there are more buffers.
        }
    };

    //--------------------------------------------------------------------------

    class reader
    {
        value_type& body_;

    public:
        template<bool isRequest, class Fields>
        explicit
        reader(header<isRequest, Fields>&, value_type& b)
            : body_(b)
        {
        }

        void
        init(boost::optional<
            std::uint64_t> const& content_length,
                error_code& ec)
        {
            // VFALCO We could reserve space in the file
            boost::ignore_unused(content_length);
            BOOST_ASSERT(body_.file_.is_open());
            ec = {};
        }

        template<class ConstBufferSequence>
        std::size_t
        put(ConstBufferSequence const& buffers,
            error_code& ec)
        {
            std::size_t nwritten = 0;
            for(auto buffer : beast::buffers_range_ref(buffers))
            {
                nwritten += body_.file_.write(
                    buffer.data(), buffer.size(), ec);
                if(ec)
                    return nwritten;
            }
            ec = {};
            return nwritten;
        }

        void
        finish(error_code& ec)
        {
            ec = {};
        }
    };

    //--------------------------------------------------------------------------

    static
    std::uint64_t
    size(value_type const& body)
    {
        return body.size();
    }
};

//------------------------------------------------------------------------------

inline
void
basic_file_body<file_linux>::
value_type::
close()
{
    error_code ignored;
    file_.close(ignored);
}

inline
void
basic_file_body<file_linux>::
value_type::
open(char const* path, file_mode mode, error_code& ec)
{
    file_.open(path, mode, ec);
    if(ec)
        return;
    size_ = file_.size(ec);
    if(ec)
    {
        close();
        return;
    }
    first_ = 0;
    last_ = size_;
}

inline
void
basic_file_body<file_linux>::
value_type::
reset(file_linux&& file, error_code& ec)
{
    if(file_.is_open())
    {
        error_code ignored;
        file_.close(ignored);
    }
    file_ = std::move(file);
    if(file_.is_open())
    {
        size_ = file_.size(ec);
        if(ec)
        {
            close();
            return;
        }
        first_ = 0;
        last_ = size_;
    }
}

//------------------------------------------------------------------------------

namespace detail {


class null_lambda
{
public:
    template<class ConstBufferSequence>
    void
    operator()(error_code&,
        ConstBufferSequence const&) const
    {
        BOOST_ASSERT(false);
    }
};

inline
error_code
make_linux_error(
    int error) noexcept
{
    return error_code(
        error,
        system_category());
}


template<
    class Protocol, class Executor,
    bool isRequest, class Fields,
    class Handler>
class write_some_linux_op
    : public beast::async_base<Handler, Executor>
{
    net::basic_stream_socket<
        Protocol, Executor>& sock_;
    serializer<isRequest,
        basic_file_body<file_linux>, Fields>& sr_;
    bool header_ = false;

public:
    template<class Handler_>
    write_some_linux_op(
        Handler_&& h,
        net::basic_stream_socket<
            Protocol, Executor>& s,
        serializer<isRequest,
            basic_file_body<file_linux>,Fields>& sr)
        : async_base<
            Handler, Executor>(
            std::forward<Handler_>(h),
            s.get_executor())
        , sock_(s)
        , sr_(sr)
    {
        (*this)();
    }

    void
    operator()()
    {
        if(! sr_.is_header_done())
        {
            header_ = true;
            sr_.split(true);
            return detail::async_write_some_impl(
                sock_, sr_, std::move(*this));
        }
        if(sr_.get().chunked())
        {
            return detail::async_write_some_impl(
                sock_, sr_, std::move(*this));
        }
        auto& w = sr_.writer_impl();
        size_t const bytes_to_write =
            static_cast<size_t>(
                (std::min<size_t>)(
                    (std::min<size_t>)(w.body_.last_ - w.pos_, sr_.limit()),
                    (std::numeric_limits<size_t>::max)()));

        auto const bytes_sent = ::sendfile(
            sock_.native_handle(),
            w.body_.file_.native_handle(),
            nullptr,
            bytes_to_write);
//        auto const dwError = boost::winapi::GetLastError();
//        if(! bSuccess && dwError !=
//                boost::winapi::ERROR_IO_PENDING_)
//        {
//            // VFALCO This needs review, is 0 the right number?
//            // completed immediately (with error?)
//            overlapped.complete(
//                make_win32_error(dwError), 0);
//            return;
//        }
    }

    void
    operator()(
        error_code ec,
        std::size_t bytes_transferred = 0)
    {
        if(ec)
        {
            std::cerr << ec.message() << '\n';
//            ec = make_win32_error(ec);
        }
        else if(! ec && ! header_)
        {
            auto& w = sr_.writer_impl();
            w.pos_ += bytes_transferred;
            BOOST_ASSERT(w.pos_ <= w.body_.last_);
            if(w.pos_ >= w.body_.last_)
            {
                sr_.next(ec, null_lambda{});
                BOOST_ASSERT(! ec);
                BOOST_ASSERT(sr_.is_done());
            }
        }
        this->complete_now(ec, bytes_transferred);
    }
};

struct run_write_some_linux_op
{
    template<
        class Protocol, class Executor,
        bool isRequest, class Fields,
        class WriteHandler>
    void
    operator()(
        WriteHandler&& h,
        net::basic_stream_socket<
            Protocol, Executor>* s,
        serializer<isRequest,
            basic_file_body<file_linux>, Fields>* sr)
    {
        // If you get an error on the following line it means
        // that your handler does not meet the documented type
        // requirements for the handler.

        static_assert(
            beast::detail::is_invocable<WriteHandler,
                void(error_code, std::size_t)>::value,
            "WriteHandler type requirements not met");

        write_some_linux_op<
            Protocol, Executor,
            isRequest, Fields,
            typename std::decay<WriteHandler>::type>(
            std::forward<WriteHandler>(h), *s, *sr);
    }
};



} // detail

//------------------------------------------------------------------------------

template<
    class Protocol, class Executor,
    bool isRequest, class Fields>
std::size_t
write_some(
    net::basic_stream_socket<
        Protocol, Executor>& sock,
    serializer<isRequest,
        basic_file_body<file_linux>, Fields>& sr,
    error_code& ec)
{
    if(! sr.is_header_done())
    {
        sr.split(true);
        auto const bytes_transferred =
            detail::write_some_impl(sock, sr, ec);
        if(ec)
            return bytes_transferred;
        return bytes_transferred;
    }
    if(sr.get().chunked())
    {
        auto const bytes_transferred =
            detail::write_some_impl(sock, sr, ec);
        if(ec)
            return bytes_transferred;
        return bytes_transferred;
    }
    auto& w = sr.writer_impl();
    w.body_.file_.seek(w.pos_, ec);
    if(ec)
        return 0;
    for (;;)
    {
        size_t const bytes_to_write =
            static_cast<size_t>(
            (std::min<size_t>)(
                (std::min<size_t>)(w.body_.last_ - w.pos_, sr.limit()),
                (std::numeric_limits<size_t>::max)()));
        auto const bytes_sent = ::sendfile(
            sock.native_handle(),
            w.body_.file_.native_handle(),
            nullptr,
            bytes_to_write);
        ec.assign(
                bytes_sent == -1 ? errno : 0,
                system_category());
        if(ec == errc::interrupted)
        {
            continue;
        }
        else if (ec)
        {
            //any other error 
            return 0;
        }
        w.pos_ += bytes_sent;
        BOOST_ASSERT(w.pos_ <= w.body_.last_);
        if(w.pos_ < w.body_.last_)
        {
            ec = {};
        }
        else
        {
            sr.next(ec, detail::null_lambda{});
            BOOST_ASSERT(! ec);
            BOOST_ASSERT(sr.is_done());
        }
        return bytes_sent;
    }
}


template<
    class Protocol, class Executor,
    bool isRequest, class Fields,
    BOOST_BEAST_ASYNC_TPARAM2 WriteHandler>
BOOST_BEAST_ASYNC_RESULT2(WriteHandler)
async_write_some(
    net::basic_stream_socket<
        Protocol, Executor>& sock,
    serializer<isRequest,
        basic_file_body<file_linux>, Fields>& sr,
    WriteHandler&& handler)
{
    return net::async_initiate<
        WriteHandler,
        void(error_code, std::size_t)>(
        detail::run_write_some_linux_op{},
        handler,
        &sock,
        &sr);
}


} // http
} // beast
} // boost

//#endif

#endif
