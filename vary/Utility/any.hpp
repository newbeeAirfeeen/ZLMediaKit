/*
* @file_name: any.hpp
* @date: 2021/07/11
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
#ifndef OAHO_UTIL_ANY_HPP
#define OAHO_UTIL_ANY_HPP
class Value_type {
public:
    using value_type = Value_type;
public:
    virtual ~Value_type() {}
    inline virtual const std::type_info& typeinfo()const = 0;
    inline virtual Value_type* clone()const = 0;
    inline virtual void* get() = 0;
};

template<typename T>
class value_type_brige :public Value_type {
public:
    using value_type = typename std::remove_const<T>::type;
public:
    value_type_brige(const T& args) :val(args) {}
    value_type& operator *() { return val; }
    inline virtual const std::type_info& typeinfo()const override { return typeid(val); }
    inline virtual Value_type* clone()const override { return new value_type_brige<value_type>(val); }
    //never delete it
    inline virtual void* get() override { return &val; }
private:
    value_type val;
};

/*
    使用了桥接和类型擦除,以来保存任何对象
*/
class Any {
public:
    Any() :val(nullptr) {}

    template<typename T>
    Any(T&& other) : val(nullptr) { set(std::forward<T>(other)); }

    Any(const Any& other) :val(nullptr) {
        if (other.val != nullptr)val = other.val->clone();
    }
    Any(Any&& other) noexcept {
        val = other.val;
        other.val = nullptr;
    }

    ~Any() { if (val != nullptr)delete val; }

    template<typename T>
    inline void set(const T& other) {
        if (val != nullptr)delete val;
        val = new value_type_brige<T>(other);
    }
    inline void* get() { return val == nullptr ? val : val->get(); }

    template<typename T> Any& operator = (const T& other)
    {
        set(std::forward<T>(other));
        return *this;
    }
    const std::type_info& typeinfo()const { if (val == nullptr) return typeid(nullptr); return val->typeinfo(); }

    template<typename T>
    T* any_cast_get()
    {
        if (typeid(T) == typeinfo())
            return reinterpret_cast<T*>(get());
        return nullptr;
    }
private:
    Value_type* val;
};


#endif // !OAHO_UTIL_ANY
