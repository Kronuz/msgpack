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

// Initializing, deinitializing and reserving a value of each type.
#pragma once

inline MsgPack* MsgPack::_init_map(size_t pos) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	MsgPack* ret = nullptr;
	// A key that is not a string cannot be indexed. It is found before anything changes, so that
	// a fill that throws leaves the map as it was: with the keys already indexed, the next fill
	// would take them for repeats and drop them from the tree.
	for (auto k = &_body->_obj->via.map.ptr[pos], kend = &_body->_obj->via.map.ptr[_body->_obj->via.map.size]; k < kend; ++k) {
		if (k->key.type != msgpack::type::STR) {
			THROW(msgpack::type_error, "MAP keys must be of type STR");
		}
	}
	_body->map.reserve(_body->_capacity);
	auto pend = &_body->_obj->via.map.ptr[_body->_obj->via.map.size];
	auto first = &_body->_obj->via.map.ptr[pos];
	auto p = first;
	try {
		for (; p < pend; ++p) {
			assert(p->key.type == msgpack::type::STR);
			std::string_view str_key(p->key.via.str.ptr, p->key.via.str.size);
			auto pit = _body->map.find(str_key);
			if (pit == _body->map.end()) {
				auto last_key = MsgPack(std::make_shared<Body>(_body->_zone, _body->_base, _body, true, 0, nullptr, &p->key));
				auto last_val = MsgPack(std::make_shared<Body>(_body->_zone, _body->_base, _body, false, pos++, last_key._body, &p->val));
				pit = _body->map.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(std::move(str_key)),
					std::forward_as_tuple(std::move(last_key), std::move(last_val))).first;
			} else {
				// A repeated key: the last value wins, in the position of the first. The entry
				// moves into the value of the first one, and the entries after it move up by one.
				auto& value_body = *pit->second.second._body;
				value_body._initialized = false;
				value_body.map.clear();
				value_body.array.clear();
				*value_body._obj = p->val;
				value_body._capacity = value_body.size();
				--pend;
				--_body->_obj->via.map.size;
				for (auto q = p; q < pend; ++q) {
					*q = *(q + 1);
				}
				--p;  // the entry that moved into this position is next
			}
			ret = &pit->second.second;
		}
	} catch (...) {
		// An allocation failed: what this call indexed goes again, so the index is as it was.
		for (auto q = first; q < p; ++q) {
			_body->map.erase(std::string_view(q->key.via.str.ptr, q->key.via.str.size));
		}
		throw;
	}
	assert(_body->_obj->via.map.size == _body->map.size());
	_body->_initialized = true;
	return ret;
}


inline void MsgPack::_update_map(size_t pos) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	const auto pend = &_body->_obj->via.map.ptr[_body->_obj->via.map.size];
	for (auto p = &_body->_obj->via.map.ptr[pos]; p < pend; ++p, ++pos) {
		std::string_view str_key(p->key.via.str.ptr, p->key.via.str.size);
		auto it = _body->map.find(str_key);
		assert(it != _body->map.end());
		auto& elem = it->second;
		elem.first._body->_obj = &p->key;
		elem.second._body->_obj = &p->val;
		elem.second._body->_capacity = elem.second._body->size();
		elem.second._body->_pos = pos;
	}
}


inline MsgPack* MsgPack::_init_array(size_t pos) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (pos < _body->array.size()) {
		// Destroy the previous objects to update
		_body->array.resize(pos);
	}

	MsgPack* ret = nullptr;
	const size_t start = pos;
	try {
		_body->array.reserve(_body->_capacity);
		const auto pend = &_body->_obj->via.array.ptr[_body->_obj->via.array.size];
		for (auto p = &_body->_obj->via.array.ptr[pos]; p < pend; ++p, ++pos) {
			auto last_val = MsgPack(std::make_shared<Body>(_body->_zone, _body->_base, _body, false, pos, nullptr, p));
			ret = &*_body->array.emplace(_body->array.end(), std::move(last_val));
		}
	} catch (...) {
		// An allocation failed: what this call indexed goes again, so the index is as it was.
		_body->array.resize(start);
		throw;
	}
	assert(_body->_obj->via.array.size == _body->array.size());
	_body->_initialized = true;
	return ret;
}


inline void MsgPack::_update_array(size_t pos, bool other_objects) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	const auto pend = &_body->_obj->via.array.ptr[_body->_obj->via.array.size];
	for (auto p = &_body->_obj->via.array.ptr[pos]; p < pend; ++p, ++pos) {
		auto& mobj = _body->at(pos);
		if (other_objects) {
			// The element stands for the object now at this position (an erase moved the later ones
			// up), so what it indexed of the object that was here is stale: it rebuilds the index of
			// its children when next used. When the array only moved to a larger buffer the elements
			// are the same objects, and references to their children stay valid.
			mobj._body->_initialized = false;
			mobj._body->map.clear();
			mobj._body->array.clear();
			mobj._body->_capacity = mobj._body->size();
		}
		mobj._body->_pos = pos;
		mobj._body->_obj = p;
	}
}


inline void MsgPack::_initialize() {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::MAP:
			_init_map(0);
			break;
		case Type::ARRAY:
			_init_array(0);
			break;
		default:
			_body->_initialized = true;
			break;
	}
}


inline void MsgPack::_deinitialize() {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	_body->_initialized = false;

	switch (_body->get_type()) {
		case Type::MAP:
			_body->map.clear();
			break;
		case Type::ARRAY:
			_body->array.clear();
			break;
		default:
			break;
	}
}


inline void MsgPack::_init_nil() {
	_body->_obj->type = msgpack::type::NIL;
}


inline void MsgPack::_init_negative_integer() {
	_body->_obj->type = msgpack::type::NEGATIVE_INTEGER;
	_body->_obj->via.i64 = 0;
}


inline void MsgPack::_init_positive_integer() {
	_body->_obj->type = msgpack::type::POSITIVE_INTEGER;
	_body->_obj->via.u64 = 0;
}


inline void MsgPack::_init_float() {
	_body->_obj->type = msgpack::type::FLOAT;
	_body->_obj->via.f64 = 0;
}


inline void MsgPack::_init_boolean() {
	_body->_obj->type = msgpack::type::NEGATIVE_INTEGER;
	_body->_obj->via.i64 = 0;
}


inline void MsgPack::_init_str() {
	_body->_obj->type = msgpack::type::STR;
	_body->_obj->via.str.ptr = nullptr;
	_body->_obj->via.str.size = 0;
}


inline void MsgPack::_init_bin() {
	_body->_obj->type = msgpack::type::BIN;
	_body->_obj->via.bin.ptr = nullptr;
	_body->_obj->via.bin.size = 0;
}


inline void MsgPack::_init_array() {
	_body->_obj->type = msgpack::type::ARRAY;
	_body->_obj->via.array.ptr = nullptr;
	_body->_obj->via.array.size = 0;
}


inline void MsgPack::_init_map() {
	_body->_obj->type = msgpack::type::MAP;
	_body->_obj->via.map.ptr = nullptr;
	_body->_obj->via.map.size = 0;
}


inline void MsgPack::_init_undefined() {
	_body->_obj->type = msgpack::type::EXT;
	_body->_obj->via.ext.ptr = ::undefined;
	_body->_obj->via.ext.size = 1;
}


inline void MsgPack::_init_type(const int&) {
	_init_negative_integer();
}


inline void MsgPack::_init_type(const long&) {
	_init_negative_integer();
}


inline void MsgPack::_init_type(const long long&) {
	_init_negative_integer();
}


inline void MsgPack::_init_type(const unsigned&) {
	_init_positive_integer();
}


inline void MsgPack::_init_type(const unsigned long&) {
	_init_positive_integer();
}


inline void MsgPack::_init_type(const unsigned long long&) {
	_init_positive_integer();
}


inline void MsgPack::_init_type(const float&) {
	_init_float();
}


inline void MsgPack::_init_type(const double&) {
	_init_float();
}


inline void MsgPack::_init_type(const long double&) {
	_init_float();
}


inline void MsgPack::_init_type(const bool&) {
	_init_boolean();
}


inline void MsgPack::_init_type(const MsgPack& val) {
	switch (val.get_type()) {
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


inline void MsgPack::_init_type(std::string_view) {
	_init_str();
}


inline void MsgPack::_reserve_str(size_t rsize) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->_capacity <= rsize) {
		size_t nsize = _body->_capacity < MSGPACK_STR_INIT_SIZE ? MSGPACK_STR_INIT_SIZE : _body->_capacity * MSGPACK_GROWTH_FACTOR;
		while (nsize < rsize) {
			nsize *= MSGPACK_GROWTH_FACTOR;
		}
		auto ptr = static_cast<char*>(_body->_zone->allocate_align(nsize));
		if (_body->_obj->via.str.ptr != nullptr && _body->_capacity > 0) {
			std::memcpy(ptr, _body->_obj->via.str.ptr, _body->_obj->via.str.size);
			// std::memset(_body->_obj->via.str.ptr, 0, _body->_obj->via.str.size * sizeof(msgpack::object_kv));  // clear memory for debugging
		}
		_body->_obj->via.str.ptr = ptr;
		_body->_capacity = nsize;
	}
}


inline void MsgPack::_reserve_map(size_t rsize) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->_capacity <= rsize) {
		size_t nsize = _body->_capacity < MSGPACK_MAP_INIT_SIZE ? MSGPACK_MAP_INIT_SIZE : _body->_capacity * MSGPACK_GROWTH_FACTOR;
		while (nsize < rsize) {
			nsize *= MSGPACK_GROWTH_FACTOR;
		}
		auto ptr = static_cast<msgpack::object_kv*>(_body->_zone->allocate_align(nsize * sizeof(msgpack::object_kv)));
		if (_body->_obj->via.map.ptr != nullptr && _body->_capacity > 0) {
			std::memcpy(ptr, _body->_obj->via.map.ptr, _body->_obj->via.map.size * sizeof(msgpack::object_kv));
			// std::memset(_body->_obj->via.map.ptr, 0, _body->_obj->via.map.size * sizeof(msgpack::object_kv));  // clear memory for debugging
		}
		_body->_obj->via.map.ptr = ptr;
		_body->_capacity = nsize;
		if (_body->_initialized) {
			_update_map(0);   // the children point into the new buffer before anything can fail
			_init_map(_body->map.size());
		}
	}
}


inline void MsgPack::_reserve_array(size_t rsize) {
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (_body->_capacity <= rsize) {
		size_t nsize = _body->_capacity >= MSGPACK_ARRAY_INIT_SIZE ? _body->_capacity * MSGPACK_GROWTH_FACTOR : MSGPACK_ARRAY_INIT_SIZE;
		while (nsize < rsize) {
			nsize *= MSGPACK_GROWTH_FACTOR;
		}
		auto ptr = static_cast<msgpack::object*>(_body->_zone->allocate_align(nsize * sizeof(msgpack::object)));
		if (_body->_obj->via.array.ptr != nullptr && _body->_capacity > 0) {
			std::memcpy(ptr, _body->_obj->via.array.ptr, _body->_obj->via.array.size * sizeof(msgpack::object));
			// std::memset(_body->_obj->via.array.ptr, 0, _body->_obj->via.array.size * sizeof(msgpack::object));  // clear memory for debugging
		}
		_body->_obj->via.array.ptr = ptr;
		_body->_capacity = nsize;
		if (_body->_initialized) {
			_update_array(0, false);   // the children point into the new buffer before anything can fail
			_init_array(_body->array.size());
		}
	}
}
