/*
 * This vector implementation is adapted from MiniSat's minisat/mtl/Vec.h.
 *
 * Copyright (c) 2003-2007, Niklas Een, Niklas Sorensson
 * Copyright (c) 2007-2010, Niklas Sorensson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _vec_hpp_INCLUDED
#define _vec_hpp_INCLUDED

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

template<class T>
class vec {
    public:
        T* data;
        int sz, cap;
        vec()                   :  data(NULL), sz(0), cap(0) {}
        vec(int size, const T& pad) : data(NULL) , sz(0)   , cap(0)    { growTo(size, pad); }
        explicit vec(int size)  :  data(NULL), sz(0), cap(0) { growTo(size); }
        ~vec()                                               { clear(true); }

        operator T*      (void)         { return data; }
        
        int     size     (void) const   { return sz;   }
        int     capacity (void) const   { return cap;  }
        void    capacity (int min_cap);

        void    setsize  (int v)        { sz = v;} 
        void    push  (void)            { if (sz == cap) capacity(sz + 1); new (&data[sz]) T(); sz++; }
        void    push   (const T& elem)  { if (sz == cap) capInc(sz + 1); data[sz++] = elem;}
        void    push_  (const T& elem)  { assert(sz < cap); data[sz++] = elem; }
        void    pop    (void)           { assert(sz > 0), sz--, data[sz].~T(); } 
        void    copyTo (vec<T>& copy)   { copy.clear(); copy.growTo(sz); for (int i = 0; i < sz; i++) copy[i] = data[i]; }
        
        void    growTo   (int size);
        void    growTo   (int size, const T& pad);
        void    clear    (bool dealloc = false);
        void    capInc   (int to_cap);


        T&       operator [] (int index)       { return data[index]; }
        const T& operator [] (int index) const { return data[index]; }

        T&       last        (void)            { return data[sz - 1]; }
        const T& last        (void)      const { return data[sz - 1]; }
                    
};


class OutOfMemoryException{};

template<class T>
void vec<T>::clear(bool dealloc) {
    if (data != NULL) {
        sz = 0;
        if (dealloc) free(data), data = NULL, cap = 0;
    }
}

template<class T>
void vec<T>::capInc(int to_cap) {
    if (cap >= to_cap) return;
    long long add = static_cast<long long>(to_cap) - cap;
    const long long geometric_add = ((cap >> 1) + 2) & ~1;
    if (add < geometric_add)
        add = geometric_add;
    if (add & 1)
        ++add;
    if (add > std::numeric_limits<int>::max() - cap)
        throw OutOfMemoryException();
    const int new_cap = cap + static_cast<int>(add);
    if (static_cast<std::size_t>(new_cap) >
        std::numeric_limits<std::size_t>::max() / sizeof(T))
        throw OutOfMemoryException();
    void* resized =
        ::realloc(data, static_cast<std::size_t>(new_cap) * sizeof(T));
    if (resized == NULL)
        throw OutOfMemoryException();
    data = static_cast<T*>(resized);
    cap = new_cap;
}

template<class T>
void vec<T>::capacity(int min_cap) {
    capInc(min_cap);
}

template<class T>
void vec<T>::growTo(int size) {
    if (sz >= size) return;
    capInc(size);
    for (int i = sz; i < size; i++) new (&data[i]) T();
    sz = size;
}

template<class T>
void vec<T>::growTo(int size, const T& pad) {
    if (sz >= size) return;
    capacity(size);
    for (int i = sz; i < size; i++) data[i] = pad;
    sz = size; }


#endif
