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

// Comparison and the arithmetic operators.
#pragma once

#include "msgpack_body.h"
#include "msgpack_iterator.h"

inline bool MsgPack::operator==(const MsgPack& other) const {
	return this == &other || *_const_body->_obj == *other._const_body->_obj;
}


inline bool MsgPack::operator!=(const MsgPack& other) const {
	return !operator==(other);
}


inline MsgPack& MsgPack::operator++() {
	return *this += 1;
}


inline MsgPack MsgPack::operator++(int) {
	MsgPack val = *this;
	val += 1;
	return val;
}


inline MsgPack& MsgPack::operator--() {
	return *this -= 1;
}


inline MsgPack MsgPack::operator--(int) {
	MsgPack val = *this;
	val -= 1;
	return val;
}


template <typename T>
inline MsgPack MsgPack::operator+(T&& o) const {
	MsgPack val = *this;
	val += std::forward<T>(o);
	return val;
}


template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator+=(T&& o) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->get_type() == Type::UNDEFINED) {
		_init_type(o);
	}
	switch (_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			_body->_obj->via.i64 += static_cast<long long>(o);
			return *this;
		case Type::POSITIVE_INTEGER:
			_body->_obj->via.u64 += static_cast<unsigned long long>(o);
			return *this;
		case Type::FLOAT:
			_body->_obj->via.f64 += static_cast<double>(o);
			return *this;
		case Type::STR:
			_append(std::to_string(o));
			return *this;
		default:
			THROW(msgpack::type_error, "Cannot add to value of {}", enum_name(_body->get_type()));
	}
}


template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator+=(T&& o) {
	return operator+=(MsgPack(std::forward<T>(o)));
}


inline MsgPack& MsgPack::operator+=(std::string_view o) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->get_type() == Type::UNDEFINED) {
		_init_type(o);
	}
	switch (_body->get_type()) {
		case Type::STR:
			_append(o);
			return *this;
		default:
			THROW(msgpack::type_error, "Cannot add to value of {}", enum_name(_body->get_type()));
	}
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T operator+(const T& o, const M& m) {
	auto val = o;
	val += static_cast<T>(m);
	return val;
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T& operator+=(T& o, const M& m) {
	return o += static_cast<T>(m);
}


template <typename T>
inline MsgPack MsgPack::operator-(T&& o) const {
	MsgPack val = *this;
	val -= std::forward<T>(o);
	return val;
}


template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator-=(T&& o) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->get_type() == Type::UNDEFINED) {
		_init_type(o);
	}
	switch (_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			_body->_obj->via.i64 -= static_cast<long long>(o);
			return *this;
		case Type::POSITIVE_INTEGER:
			_body->_obj->via.u64 -= static_cast<unsigned long long>(o);
			return *this;
		case Type::FLOAT:
			_body->_obj->via.f64 -= static_cast<double>(o);
			return *this;
		default:
			THROW(msgpack::type_error, "Cannot subtract from value of {}", enum_name(_body->get_type()));
	}
}


template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator-=(T&& o) {
	return operator-=(MsgPack(std::forward<T>(o)));
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T operator-(const T& o, const M& m) {
	auto val = o;
	val -= static_cast<T>(m);
	return val;
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T& operator-=(T& o, const M& m) {
	return o -= static_cast<T>(m);
}


template <typename T>
inline MsgPack MsgPack::operator*(T&& o) const {
	MsgPack val = *this;
	val *= std::forward<T>(o);
	return val;
}


template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator*=(T&& o) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->get_type() == Type::UNDEFINED) {
		_init_type(o);
	}
	switch (_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			_body->_obj->via.i64 *= static_cast<long long>(o);
			return *this;
		case Type::POSITIVE_INTEGER:
			_body->_obj->via.u64 *= static_cast<unsigned long long>(o);
			return *this;
		case Type::FLOAT:
			_body->_obj->via.f64 *= static_cast<double>(o);
			return *this;
		default:
			THROW(msgpack::type_error, "Cannot multiply by value of {}", enum_name(_body->get_type()));
	}
}


template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator*=(T&& o) {
	return operator*=(MsgPack(std::forward<T>(o)));
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T operator*(const T& o, const M& m) {
	auto val = o;
	val *= static_cast<T>(m);
	return val;
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T& operator*=(T& o, const M& m) {
	return o *= static_cast<T>(m);
}


template <typename T>
inline MsgPack MsgPack::operator/(T&& o) const {
	MsgPack val = *this;
	val /= std::forward<T>(o);
	return val;
}


template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator/=(T&& o) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->get_type() == Type::UNDEFINED) {
		_init_type(o);
	}
	switch (_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			_body->_obj->via.i64 /= static_cast<long long>(o);
			return *this;
		case Type::POSITIVE_INTEGER:
			_body->_obj->via.u64 /= static_cast<unsigned long long>(o);
			return *this;
		case Type::FLOAT:
			_body->_obj->via.f64 /= static_cast<double>(o);
			return *this;
		default:
			THROW(msgpack::type_error, "Cannot divide value of {}", enum_name(_body->get_type()));
	}
}


template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline MsgPack& MsgPack::operator/=(T&& o) {
	return operator/=(MsgPack(std::forward<T>(o)));
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T operator/(const T& o, const M& m) {
	auto val = o;
	val /= static_cast<T>(m);
	return val;
}


template <typename T, typename M, std::enable_if_t<not std::is_same<std::decay_t<T>, MsgPack>::value and std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
inline T& operator/=(T& o, const M& m) {
	return o /= static_cast<T>(m);
}
