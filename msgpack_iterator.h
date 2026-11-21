/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// MsgPack::Iterator, the iterator over arrays and maps.
#pragma once

#include <cstddef>
#include <iterator>
#include "msgpack_class.h"

template <typename T>
class MsgPack::Iterator : public std::iterator<std::input_iterator_tag, MsgPack> {
	friend MsgPack;

	T* _mobj;
	mutable off_t _off;

	Iterator(T* o, off_t off)
		: _mobj(o),
		  _off(off)
	{
		switch (_mobj->_const_body->get_type()) {
			case MsgPack::Type::MAP:
			case MsgPack::Type::ARRAY:
			case MsgPack::Type::UNDEFINED:
				return;
			default:
				THROW(msgpack::type_error, "{} is not iterable", enum_name(_mobj->_const_body->get_type()));
		}
	}

public:
	Iterator(const Iterator& it)
		: _mobj(it._mobj),
		  _off(it._off)	{ }

	Iterator& operator++() {
		++_off;
		return *this;
	}

	const Iterator& operator++() const {
		++_off;
		return *this;
	}

	Iterator operator++(int) {
		Iterator tmp(*this);
		++_off;
		return tmp;
	}

	const Iterator operator++(int) const {
		Iterator tmp(*this);
		++_off;
		return tmp;
	}

	Iterator& operator+=(int off) {
		_off += off;
		return *this;
	}

	const Iterator& operator+=(int off) const {
		_off += off;
		return *this;
	}

	Iterator operator+(int off) {
		Iterator tmp(*this);
		tmp._off += off;
		return tmp;
	}

	const Iterator operator+(int off) const {
		Iterator tmp(*this);
		tmp._off += off;
		return tmp;
	}

	Iterator operator=(const Iterator& other) {
		_mobj = other._mobj;
		_off = other._off;
		return *this;
	}

	T& operator*() {
		if (_mobj->_const_body->get_type() == MsgPack::Type::MAP) {
			return _mobj->at(_off)._body->_key;
		} else {
			return _mobj->at(_off);
		}
	}

	T* operator->() {
		return &operator*();
	}

	T& operator*() const {
		if (_mobj->_const_body->get_type() == MsgPack::Type::MAP) {
			return _mobj->at(_off)._const_body->_key;
		} else {
			return _mobj->at(_off);
		}
	}

	T* operator->() const {
		return &operator*();
	}

	T& value() {
		return _mobj->at(_off);
	}

	T& value() const {
		return _mobj->at(_off);
	}

	bool operator==(const Iterator& other) const {
		return this == &other || (*_mobj == *other._mobj && _off == other._off);
	}

	bool operator!=(const Iterator& other) const {
		return !operator==(other);
	}
};
