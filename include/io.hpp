#ifndef DCGP_IO_H
#define DCGP_IO_H

#include <iostream>
#include <utility>
#include <vector>

#define PAGMO_MAX_OUTPUT_LENGTH 5u

// Implementation of a "pythonic" print finction. Use as dcgp::stream(s, a1, a2,....) where s is an std::ostream and
// a1, a2, variables (also std vectors .... )

namespace dcgp
{

/// Forward declaration
template <typename ... Args>
void stream(std::ostream &, const Args & ...);

namespace detail {

template <typename T>
inline void stream_impl(std::ostream &os, const T &x)
{
    os << x;
}

inline void stream_impl(std::ostream &os, const bool &b)
{
    if (b) {
        os << "true";
    } else {
        os << "false";
    }
}

template <typename T>
inline void stream_impl(std::ostream &os, const std::vector<T> &v)
{
    auto len = v.size();
    if (len <= PAGMO_MAX_OUTPUT_LENGTH) {
        os << '[';
        for (decltype(v.size()) i = 0u; i < v.size(); ++i) {
            stream(os, v[i]);
            if (i != v.size() - 1u) {
                os << ", ";
            }
        }
        os << ']';
    } else {
        os << '[';
        for (decltype(v.size()) i = 0u; i < PAGMO_MAX_OUTPUT_LENGTH; ++i) {
            stream(os, v[i], ", ");
        }
        os << "... ]";
    }
}

template <typename T, typename U>
inline void stream_impl(std::ostream &os, const std::pair<T,U> &p)
{
    stream(os,'(',p.first,',',p.second,')');
}

template <typename T, typename ... Args>
inline void stream_impl(std::ostream &os, const T &x, const Args & ... args)
{
    stream_impl(os,x);
    stream_impl(os,args...);
}

} // end of namespace detail

/// The PaGMO streaming function
template <typename ... Args>
inline void stream(std::ostream &os, const Args & ... args)
{
    detail::stream_impl(os,args...);
}

/// The PaGMO print function
template <typename ... Args>
inline void print(const Args & ... args)
{
    stream(std::cout,args...);
}

} // end of namespace dcgp

#undef PAGMO_MAX_OUTPUT_LENGTH

#endif
