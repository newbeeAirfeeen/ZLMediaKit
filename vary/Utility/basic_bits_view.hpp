#ifndef OAHO_CODEC_BITS_VIEW_HPP
#define OAHO_CODEC_BITS_VIEW_HPP

/*
* @file_name: basic_bits_view-inl.hpp
* @date: 2021/09/07
* @author: oaho
* Copyright @ hz oaho, All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <type_traits>
#include "basic_string_view.hpp"
/*
* Note: basic_byte_bit_stream is unsigned shift(<<, >>) 
*       which means that most siginificant bit is fill by 0 to save signed
*/
template<typename CharT>
class basic_byte_bit_stream {
public:
    using value_type = CharT;
    using size_type =  size_t;
public:
    explicit basic_byte_bit_stream(value_type v = 0) :val(v), index(0) {}
public:
    inline basic_byte_bit_stream& reset(value_type val = 0) { this->val = val; index = 0; return *this; }
    inline size_t size() const { return sizeof(value_type) * 8 - index; }
    inline basic_byte_bit_stream& flip() { val = ~val; return *this;}
    inline basic_byte_bit_stream& set(value_type val = 0) { (this->val) &= val; return *this;}
    template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
    auto get_bits(size_t N) -> typename std::remove_cv<typename std::remove_reference<T>::type>::type
    {
        using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
        if (N == 0)return 0;
        if (N > size())throw std::out_of_range("the bit stream out of stream");
        /* get the most significant bits */
        /* Note: */
        auto move_bit_n = (sizeof(value_type) * 8 - N);
        /* Note: 这里的移位如果是有符号的,那么会算数移位 比如 (int)(0x80 >> 7) = 0xffffffff*/
        type val_ = ((unsigned char)val >> move_bit_n);
        index += (value_type)N;
        /* update new data */
        val <<= N;
        return val_;
    }
private:
    value_type val;
    value_type index;
};



template<typename CharT>
class basic_bits_view: protected basic_string_view<CharT>{
public:
    using value_type = typename basic_string_view<CharT>::value_type;
    using base_type = basic_string_view<CharT>;
public:
    constexpr basic_bits_view() noexcept:basic_string_view<CharT>(nullptr, 0){}
    /** Constructs a string reference object from a C string and a size. */
    BASIC_STRING_VIEW_CONSTEXPR basic_bits_view(const CharT* s, size_t count):basic_string_view<CharT>(s, count){
        if (count == 0)
            throw std::logic_error("the str length must greater than zero");
        bits.reset(this->operator[](0));
    }

    /* get the most significant bits to codenum */
    template<typename T>
    BASIC_STRING_VIEW_CONSTEXPR auto get_nbits(size_t N) -> typename std::remove_cv<typename std::remove_reference<T>::type>::type
    {
        //using type = typename std::remove_all_extents<typename std::decay<T>::type>::type;
        using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
        type val = 0;
        /*  
        *  check N bits whether enough 
        *  if N bits >= current, get the remaining of current;
        *  Note:
        **/
        check_status(N);
        while (N > 0)
        {
            /* there, current bits set is remaining */
            if (N <= bits.size())
            {
                val = bits.template get_bits<type>(N);
                /* reset prefix */
                if (!bits.size()) {
                    base_type::remove_prefix(1);
                    bits.reset(this->operator[](0));
                }
                break;
            }
            /* Note: may be bits.size() is not enough by caller */
            if (N <= 8 && N > bits.size())
            {
                //get the remaining
                auto remaning_size = bits.size();
                val = bits.template get_bits<type>(remaning_size);
                base_type::remove_prefix(1);
                bits.reset(this->operator[](0));
                val = (val << remaning_size) | (bits.template get_bits<type>(N - remaning_size));
                break;
            }
            /* N > bits.size()! fix bug*/
            auto original_size = bits.size();
            val = (val << bits.size()) | (bits.template get_bits<type>(bits.size()));
            N -= original_size;
            base_type::remove_prefix(1);
            /* get the current byte and remove prefix-index*/
            /* get 1 bytes to bit stream */
            bits.reset(this->operator[](0));
        }
        return val;

    }
    BASIC_STRING_VIEW_CONSTEXPR inline void remove_prefix_bits(size_t N){ get_nbits<char>(N); }
    BASIC_STRING_VIEW_CONSTEXPR inline size_t bits_size()
    {
        return bits.size() + (base_type::size() - 1) * 8 * sizeof(CharT);
    }
private:
    BASIC_STRING_VIEW_CONSTEXPR inline void check_status(size_t N)
    {
        if (N > sizeof(size_t) * 8)throw std::out_of_range("the N bits greater than size_t * 8 bits");
        /* remaning of current + size() - 1 */
        if (base_type::size() == 0 || N > bits_size())
            throw std::out_of_range("there is not enough bits in bit stream");
    }
private:
    basic_byte_bit_stream<CharT> bits;
};

using bits_view = basic_bits_view<char>;
#endif