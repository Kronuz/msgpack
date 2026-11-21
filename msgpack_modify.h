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

// Fill, lock, and the public erase, replace, emplace, insert, update, put, add and append.
#pragma once

inline void MsgPack::_fill(bool recursive, bool lock) {
	if (_body->_lock) {
		return;
	}
	if (!_body->_initialized) {
		_initialize();
	}
	_body->_lock = lock;
	if (recursive) {
		switch (_body->get_type()) {
			case Type::MAP:
				for (auto& item : _body->map) {
					item.second.second._fill(recursive, lock);
				}
				break;
			case Type::ARRAY:
				for (auto& item : _body->array) {
					item._fill(recursive, lock);
				}
				break;
			default:
				break;
		}
	}
}


inline void MsgPack::_fill(bool recursive, bool lock) const {
	const_cast<MsgPack*>(this)->_fill(recursive, lock);
}


template <typename M, typename>
inline MsgPack::iterator MsgPack::_find(M&& o) {
	switch (o._body->get_type()) {
		case Type::STR:
			return _find(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return _find(static_cast<size_t>(o._body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return _find(static_cast<size_t>(o._body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


template <typename M, typename>
inline MsgPack::const_iterator MsgPack::_find(M&& o) const {
	switch (o._const_body->get_type()) {
		case Type::STR:
			return _find(std::string_view(o._const_body->_obj->via.str.ptr, o._const_body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._const_body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
			}
			return _find(static_cast<size_t>(o._const_body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return _find(static_cast<size_t>(o._const_body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
	}
}


template <typename T>
inline auto MsgPack::external(std::function<T(const msgpack::object&)> f) const {
	return f(*_body->_obj);
}


inline void MsgPack::lock() const {
	// Everything is filled before anything is locked: filling throws for a map with a key that is not
	// a string, and a lock() that throws leaves nothing locked.
	_fill(true, false);
	_fill(true, true);
}


inline bool MsgPack::locked() const {
	return _body->_lock;
}


template <typename M, typename>
inline size_t MsgPack::erase(M&& o) {
	_fill(false, false);
	return _erase(std::forward<M>(o)).first;
}


inline size_t MsgPack::erase(std::string_view s) {
	_fill(false, false);
	return _erase(s).first;
}


inline size_t MsgPack::erase(size_t pos) {
	_fill(false, false);
	return _erase(pos).first;
}


inline MsgPack::iterator MsgPack::erase(MsgPack::iterator it) {
	_fill(false, false);
	return _erase(it._off).second;
}


inline void MsgPack::clear() {
	// no need to _fill something that will be cleared.
	_clear();
}


template <typename M, typename T, typename>
inline MsgPack::iterator MsgPack::replace(M&& o, T&& val) {
	_fill(false, false);
	auto pair = _put(std::forward<M>(o), std::forward<T>(val), true);
	return MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos);
}


template <typename T>
inline MsgPack::iterator MsgPack::replace(std::string_view s, T&& val) {
	_fill(false, false);
	auto pair = _put(s, std::forward<T>(val), true);
	return MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos);
}


template <typename T>
inline MsgPack::iterator MsgPack::replace(size_t pos, T&& val) {
	_fill(false, false);
	auto pair = _put(pos, std::forward<T>(val), true);
	return MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos);
}


template <typename M, typename T, typename>
inline std::pair<MsgPack::iterator, bool> MsgPack::emplace(M&& o, T&& val) {
	_fill(false, false);
	auto pair = _emplace(std::forward<M>(o), std::forward<T>(val), false);
	return std::make_pair(MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos), pair.second);
}


template <typename T>
inline std::pair<MsgPack::iterator, bool> MsgPack::emplace(std::string_view s, T&& val) {
	_fill(false, false);
	auto pair = _put(s, std::forward<T>(val), false);
	return std::make_pair(MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos), pair.second);
}


template <typename T>
inline std::pair<MsgPack::iterator, bool> MsgPack::emplace(size_t pos, T&& val) {
	_fill(false, false);
	auto pair = _insert(pos, std::forward<T>(val), false);
	return std::make_pair(MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos), pair.second);
}


template <typename K, typename V>
inline std::pair<MsgPack::iterator, bool> MsgPack::insert(const std::pair<K&&, V&&>& val) {
	return emplace(std::forward<K>(val.first), std::forward<V>(val.second));
}


inline std::pair<MsgPack::iterator, bool> MsgPack::insert(std::string_view key) {
	_fill(false, false);
	auto pair = _put(key, MsgPack::undefined(), false);
	return std::make_pair(MsgPack::Iterator<MsgPack>(this, pair.first->_body->_pos), pair.second);
}


template <typename T>
inline void MsgPack::push_back(T&& v) {
	_fill(false, false);
	_put(size(), std::forward<T>(v), false);
}


template <typename M, std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value, int>>
inline void MsgPack::update(M&& o) {
	// Everything that can throw for a reason of the arguments (a locked or constant value, a map that has
	// a key that is not a string, a value that is not a container) is met in the first pass, which
	// changes nothing, so a throw leaves the value as it was. Only an allocation that fails in the
	// second pass can leave the entries before it applied.
	_update_check(o);
	_update_apply(o);
}


inline void MsgPack::_update_check(const MsgPack& o) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	if (o._body->get_type() != Type::MAP) {
		return;   // `*this = o`: checked above, and the object is built before anything changes
	}
	// The same calls, in the same order, as _update_apply() makes, without the ones that change the tree
	for (const auto& key : o) {
		const auto& val = o.at(key);
		auto it = find(key);   // erase() of a key and replace() of a new one go through the same fill and type check
		if (!val.is_undefined() && it != end()) {
			auto& item = at(key);
			if (item.is_map()) {
				item._update_check(val);
			} else {
				if (item._body->_const) {
					THROW(msgpack::const_error, "Constant object");
				}
				if (item._body->_lock) {
					assert(!item._body->_lock);
					THROW(msgpack::const_error, "Locked object");
				}
			}
		}
	}
}


inline void MsgPack::_update_apply(const MsgPack& o) {
	switch (o._body->get_type()) {
		case Type::MAP:
			if (_body->get_type() == Type::UNDEFINED) {
				_init_map();
			}
			for (const auto& key : o) {
				const auto& val = o.at(key);
				if (val.is_undefined()) {
					erase(key);
				} else if (find(key) == end()) {
					replace(key, val);
				} else {
					auto& item = at(key);
					if (item.is_map()) {
						item._update_apply(val);
					} else {
						item = val;
					}
				}
			}
			break;
		default:
			*this = o;
			break;
	}
}

template <typename M, std::enable_if_t<std::is_convertible<M, MsgPack>::value and not std::is_same<std::decay_t<M>, MsgPack>::value, int>>
inline void MsgPack::update(M&& o) {
	update(MsgPack(std::forward<M>(o)));
}

// Following are helper functions to directly return references:

template <typename M, typename T, typename>
inline MsgPack& MsgPack::put(M&& o, T&& val) {
	_fill(false, false);
	auto pair = _put(std::forward<M>(o), std::forward<T>(val), true);
	return *pair.first;
}


template <typename T>
inline MsgPack& MsgPack::put(std::string_view s, T&& val) {
	_fill(false, false);
	auto pair = _put(s, std::forward<T>(val), true);
	return *pair.first;
}


template <typename T>
inline MsgPack& MsgPack::put(size_t pos, T&& val) {
	_fill(false, false);
	auto pair = _put(pos, std::forward<T>(val), true);
	return *pair.first;
}


template <typename M, typename T, typename>
inline MsgPack& MsgPack::add(M&& o, T&& val) {
	_fill(false, false);
	auto pair = _emplace(std::forward<M>(o), std::forward<T>(val), false);
	return *pair.first;
}


template <typename T>
inline MsgPack& MsgPack::add(std::string_view s, T&& val) {
	_fill(false, false);
	auto pair = _put(s, std::forward<T>(val), false);
	return *pair.first;
}


template <typename T>
inline MsgPack& MsgPack::add(size_t pos, T&& val) {
	_fill(false, false);
	auto pair = _insert(pos, std::forward<T>(val), false);
	return *pair.first;
}


template <typename T>
inline MsgPack& MsgPack::append(T&& v) {
	_fill(false, false);
	switch (_body->get_type()) {
		case Type::STR:
			_append(std::forward<T>(v));
			return *this;
		default: {
			auto pair = _put(size(), std::forward<T>(v), false);
			return *pair.first;
		}
	}
}
