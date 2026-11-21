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

// Iterator entry points, constructors, initializers and assignment.
#pragma once

#include "msgpack_body.h"
#include "msgpack_iterator.h"

namespace std {
	inline auto to_string(const MsgPack& o) {
		return o.as_str();
	}
}


inline MsgPack::iterator MsgPack::begin() {
	return iterator(this, 0);
}


inline MsgPack::const_iterator MsgPack::begin() const {
	return const_iterator(this, 0);
}


inline MsgPack::const_iterator MsgPack::cbegin() const {
	return const_iterator(this, 0);
}


inline MsgPack::iterator MsgPack::end() {
	return iterator(this, size());
}


inline MsgPack::const_iterator MsgPack::end() const {
	return const_iterator(this, size());
}


inline MsgPack::const_iterator MsgPack::cend() const {
	return const_iterator(this, size());
}


inline MsgPack::MsgPack(const std::shared_ptr<MsgPack::Body>& b)
	: _body(b),
	  _const_body(_body.get()) { }


inline MsgPack::MsgPack(std::shared_ptr<MsgPack::Body>&& b)
	: _body(std::move(b)),
	  _const_body(_body.get()) { }


inline void MsgPack::_initializer_array(std::initializer_list<MsgPack> list) {
	if (_body->_obj->type != msgpack::type::ARRAY) {
		_body->_capacity = 0;
	}
	_deinitialize();
	_init_array();
	_reserve_array(list.size());
	for (const auto& val : list) {
		_put(size(), val, true);
	}
}


inline void MsgPack::_initializer_map(std::initializer_list<MsgPack> list) {
	if (_body->_obj->type != msgpack::type::MAP) {
		_body->_capacity = 0;
	}
	_deinitialize();
	_init_map();
	_reserve_map(list.size());
	for (const auto& val : list) {
		_put(val.at(0), val.at(1), true);
	}
}


inline void MsgPack::_initializer(std::initializer_list<MsgPack> list) {
	auto isMap = true;
	for (const auto& val : list) {
		if (!val.is_array() || val.size() != 2 || !val.at(0).is_string()) {
			isMap = false;
			break;
		}
	}
	if (isMap) _initializer_map(list);
	else _initializer_array(list);
}


template <typename T>
inline void MsgPack::_assignment(T&& v) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	msgpack::object obj(std::forward<T>(v), *_body->_zone);
	if (_body->_is_key) {
		// Rename key, if the assignment is acting on a map key...
		// We expect obj to be a string:
		if (obj.type != msgpack::type::STR) {
			THROW(msgpack::type_error, "MAP keys must be of type STR");
		}
		if (auto parent_body = _body->_parent.lock()) {
			// If there's a parent, and it's initialized...
			if (parent_body->_initialized) {
				// Change key in the parent's map:
				auto old_key = std::string_view(_body->_obj->via.str.ptr, _body->_obj->via.str.size);
				auto new_key = std::string_view(obj.via.str.ptr, obj.via.str.size);
				if (new_key != old_key) {
					auto it = parent_body->map.find(old_key);
					if (it != parent_body->map.end()) {
						auto pit = parent_body->map.find(new_key);
						if (pit == parent_body->map.end()) {
							parent_body->map.emplace(new_key, std::move(it->second));
						} else {
							pit->second = std::move(it->second);
						}
						parent_body->map.erase(it);
						assert(parent_body->_obj->via.map.size == parent_body->map.size());
					}
				}
			}
		}
	}
	_deinitialize();
	*_body->_obj = std::move(obj);
	_body->_capacity = _body->size();
}


inline MsgPack::MsgPack()
	: MsgPack(_undefined())
{
}


inline MsgPack::MsgPack(const MsgPack& other)
	: _body(std::make_shared<Body>(other)),
	  _const_body(_body.get())
{
}


inline MsgPack::MsgPack(MsgPack&& other) noexcept
	: _body(std::move(other._body)),
	  _const_body(std::move(other._const_body))
{
}


template <typename T, typename>
inline MsgPack::MsgPack(T&& v)
	: _body(std::make_shared<Body>(std::forward<T>(v))),
	  _const_body(_body.get()) { }


inline MsgPack::MsgPack(undefined_ctor)
	: _body(std::make_shared<Body>(_undefined())),
	  _const_body(_body.get())
{
	_body->_const = true;
}


inline MsgPack::MsgPack(std::initializer_list<MsgPack> list)
	: MsgPack(_undefined())
{
	_initializer(list);
}


inline MsgPack::MsgPack(Type type)
	: MsgPack(_undefined())
{
	_deinitialize();
	switch (type) {
		case Type::NIL:
			_init_nil();
			break;
		case Type::NEGATIVE_INTEGER:
			_init_negative_integer();
			break;
		case Type::POSITIVE_INTEGER:
			_init_positive_integer();
			break;
		case Type::FLOAT:
			_init_float();
			break;
		case Type::BOOLEAN:
			_init_boolean();
			break;
		case Type::STR:
			_init_str();
			break;
		case Type::BIN:
			_init_bin();
			break;
		case Type::ARRAY:
			_init_array();
			break;
		case Type::MAP:
			_init_map();
			break;
		case Type::UNDEFINED:
			_init_undefined();
			break;
		default:
			THROW(msgpack::type_error, "Invalid type");
	}
}


inline MsgPack& MsgPack::operator=(MsgPack&& other) {
	if (!_body) {
		_body = std::move(other._body);
		_const_body = std::move(other._const_body);
	} else {
		_assignment(std::move(other));
	}
	return *this;
}


inline MsgPack& MsgPack::operator=(const MsgPack& other) {
	if (!_body) {
		_body = std::make_shared<Body>(*other._body->_obj);
		_const_body = _body.get();
	} else {
		_assignment(other);
	}
	return *this;
}


template <typename T>
inline MsgPack& MsgPack::operator=(T&& v) {
	if (!_body) {
		_body = std::make_shared<Body>(std::forward<T>(v));
		_const_body = _body.get();
	} else {
		_assignment(std::forward<T>(v));
	}
	return *this;
}


inline MsgPack& MsgPack::operator=(std::initializer_list<MsgPack> list) {
	if (!_body) {
		_body = std::make_shared<Body>(_undefined());
		_const_body = _body.get();
	}
	_initializer(list);
	return *this;
}
