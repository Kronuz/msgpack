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

// The internal find, erase and clear.
#pragma once

#include "msgpack_body.h"
#include "msgpack_iterator.h"

inline MsgPack::iterator MsgPack::_find(std::string_view key) {
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			return end();
		case Type::MAP: {
			auto it = _body->map.find(key);
			if (it == _body->map.end()) {
				return end();
			}
			return MsgPack::iterator(this, it->second.second._body->_pos);
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


inline MsgPack::const_iterator MsgPack::_find(std::string_view key) const {
	switch (_const_body->get_type()) {
		case Type::UNDEFINED:
			return cend();
		case Type::MAP: {
			auto it = _const_body->map.find(key);
			if (it == _const_body->map.end()) {
				return cend();
			}
			return MsgPack::const_iterator(this, it->second.second._const_body->_pos);
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_const_body->get_type()));
	}
}


inline MsgPack::iterator MsgPack::_find(size_t pos) {
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			return end();
		case Type::MAP: {
			if (pos >= _body->_obj->via.map.size) {
				return end();
			}
			auto it = _body->map.find(std::string_view(_body->_obj->via.map.ptr[pos].key.via.str.ptr, _body->_obj->via.map.ptr[pos].key.via.str.size));
			if (it == _body->map.end()) {
				return end();
			}
			return MsgPack::iterator(this, it->second.second._body->_pos);
		}
		case Type::ARRAY: {
			if (pos >= _body->_obj->via.array.size) {
				return end();
			}
			auto it = _body->array.begin() + pos;
			if (it >= _body->array.end()) {
				return end();
			}
			return MsgPack::iterator(this, it->_body->_pos);
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


inline MsgPack::const_iterator MsgPack::_find(size_t pos) const {
	switch (_const_body->get_type()) {
		case Type::UNDEFINED:
			return cend();
		case Type::MAP: {
			if (pos >= _const_body->_obj->via.map.size) {
				return cend();
			}
			auto it = _const_body->map.find(std::string_view(_const_body->_obj->via.map.ptr[pos].key.via.str.ptr, _const_body->_obj->via.map.ptr[pos].key.via.str.size));
			if (it == _const_body->map.end()) {
				return cend();
			}
			return MsgPack::const_iterator(this, it->second.second._const_body->_pos);
		}
		case Type::ARRAY: {
			if (pos >= _const_body->_obj->via.array.size) {
				return cend();
			}
			auto it = _const_body->array.begin() + pos;
			if (it >= _const_body->array.end()) {
				return cend();
			}
			return MsgPack::const_iterator(this, it->_const_body->_pos);
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_const_body->get_type()));
	}
}


template <typename M, typename>
inline std::pair<size_t, MsgPack::iterator> MsgPack::_erase(M&& o) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (o._body->get_type()) {
		case Type::STR:
			return _erase(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return _erase(static_cast<size_t>(o._body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return _erase(static_cast<size_t>(o._body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


inline std::pair<size_t, MsgPack::iterator> MsgPack::_erase(std::string_view key) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			return std::make_pair(0, end());
		case Type::MAP: {
			auto it = _body->map.find(key);
			if (it == _body->map.end()) {
				return std::make_pair(0, end());
			}

			auto& mobj = it->second.second;
			auto pos_ = mobj._body->_pos;
			assert(pos_ < _body->_obj->via.map.size);
			auto p = &_body->_obj->via.map.ptr[pos_];
			std::memmove(p, p + 1, (_body->_obj->via.map.size - pos_ - 1) * sizeof(msgpack::object_kv));
			--_body->_obj->via.map.size;
			// Erase from map:
			_body->map.erase(it);
			_update_map(pos_);

			auto next = _body->map.find(std::string_view(p->key.via.str.ptr, p->key.via.str.size));
			if (next == _body->map.end()) {
				return std::make_pair(1, end());   // the last entry went: one erased, nothing after it
			}

			return std::make_pair(1, MsgPack::iterator(this, next->second.second._body->_pos));
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


inline std::pair<size_t, MsgPack::iterator> MsgPack::_erase(size_t pos) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			return std::make_pair(0, end());
		case Type::MAP: {
			if (pos >= _body->_obj->via.map.size) {
				return std::make_pair(0, end());
			}
			return _erase(std::string_view(_body->_obj->via.map.ptr[pos].key.via.str.ptr, _body->_obj->via.map.ptr[pos].key.via.str.size));
		}
		case Type::ARRAY: {
			if (pos >= _body->_obj->via.array.size) {
				return std::make_pair(0, end());
			}
			auto it = _body->array.begin() + pos;
			if (it >= _body->array.end()) {
				return std::make_pair(0, end());
			}

			auto& mobj = *it;
			auto pos_ = mobj._body->_pos;
			assert(pos_ < _body->_obj->via.array.size);
			auto p = &_body->_obj->via.array.ptr[pos_];
			std::memmove(p, p + 1, (_body->_obj->via.array.size - pos_ - 1) * sizeof(msgpack::object));
			--_body->_obj->via.array.size;
			// Erase from array:
			_body->array.pop_back();
			_update_array(pos_);

			auto next = _body->array.begin() + pos_;
			if (next >= _body->array.end()) {
				return std::make_pair(1, end());   // the last element went: one erased, nothing after it
			}

			return std::make_pair(1, MsgPack::iterator(this, next->_body->_pos));
		}
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


inline void MsgPack::_clear() {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	_body->_initialized = false;

	switch (_body->get_type()) {
		case Type::MAP:
			_body->_obj->via.map.size = 0;
			_body->map.clear();
			break;
		case Type::ARRAY:
			_body->_obj->via.array.size = 0;
			_body->array.clear();
			break;
		case Type::STR:
			_body->_obj->via.str.size = 0;
			break;
		default:
			break;
	}
}
