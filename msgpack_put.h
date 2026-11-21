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

// The internal append, put, emplace and insert.
#pragma once

template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline void MsgPack::_append(T&& o) {
	_append(std::string_view(std::to_string(std::forward<T>(o))));
}


template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int>>
inline void MsgPack::_append(T&& o) {
	_append(MsgPack(std::forward<T>(o)));
}


inline void MsgPack::_append(std::string_view val) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			_init_str();
			[[fallthrough]];
		case Type::STR: {
			auto val_size = static_cast<uint32_t>(val.size());
			_reserve_str(_body->_obj->via.str.size + val_size);
			if (val_size) {
				auto ptr = const_cast<char*>(_body->_obj->via.str.ptr + _body->_obj->via.str.size);
				std::memcpy(ptr, val.data(), val_size);
				_body->_obj->via.str.size += val_size;
			}
			break;
		}
		default:
			THROW(msgpack::type_error, "Cannot append STR to {}", enum_name(_body->get_type()));
	}
}


template <typename M, typename T, typename>
inline std::pair<MsgPack*, bool> MsgPack::_put(M&& o, T&& val, bool overwrite) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (o._body->get_type()) {
		case Type::STR:
			return _put(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size), std::forward<T>(val), overwrite);
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return _put(static_cast<size_t>(o._body->_obj->via.i64), std::forward<T>(val), overwrite);
		case Type::POSITIVE_INTEGER:
			return _put(static_cast<size_t>(o._body->_obj->via.u64), std::forward<T>(val), overwrite);
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


template <typename T>
inline std::pair<MsgPack*, bool> MsgPack::_put(std::string_view key, T&& val, bool overwrite) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			_init_map();
			[[fallthrough]];
		case Type::MAP: {
			auto it = _body->map.find(key);
			if (it == _body->map.end()) {
				_reserve_map(_body->_obj->via.map.size + 1);
				auto p = &_body->_obj->via.map.ptr[_body->_obj->via.map.size];
				p->key.type = msgpack::type::STR;
				p->key.via.str.size = static_cast<uint32_t>(key.size());
				auto ptr = static_cast<char*>(_body->_zone->allocate_align(p->key.via.str.size));
				std::memcpy(ptr, key.data(), p->key.via.str.size);
				p->key.via.str.ptr = ptr;
				p->val = msgpack::object(std::forward<T>(val), *_body->_zone);
				++_body->_obj->via.map.size;
				try {
					return std::make_pair(_init_map(_body->map.size()), true);
				} catch (...) {
					--_body->_obj->via.map.size;   // an allocation failed: the entry is not added
					throw;
				}
			} else {
				auto pos = it->second.second._body->_pos;
				if (overwrite) {
					// The old value is replaced: what its body indexed is not on the new one. The new
					// object is built first, because that can throw, and a locked value stays as it is.
					auto& old_value = it->second.second;
					if (old_value._body->_lock) {
						THROW(msgpack::const_error, "Locked object");
					}
					msgpack::object obj(std::forward<T>(val), *_body->_zone);
					old_value._deinitialize();
					_body->_obj->via.map.ptr[pos].val = obj;
					old_value._body->_capacity = old_value._body->size();
					return std::make_pair(&_body->at(key), true);
				}
				return std::make_pair(&_body->at(key), false);
			}
		}
		case Type::ARRAY:
			THROW(msgpack::type_error, "{} cannot have string keys", enum_name(_body->get_type()));
		default:
			THROW(msgpack::type_error, "{} is not a container that can have keys", enum_name(_body->get_type()));
	}
}


template <typename T>
inline std::pair<MsgPack*, bool> MsgPack::_put(size_t pos, T&& val, bool overwrite) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			_init_array();
			[[fallthrough]];
		case Type::ARRAY:
			if (pos >= _body->_obj->via.array.size) {
				_reserve_array(pos + 1);

				// Initialize new elements.
				const auto plast = &_body->_obj->via.array.ptr[pos];
				auto p = &_body->_obj->via.array.ptr[_body->_obj->via.array.size];
				for (; p != plast; ++p) {
					p->type = msgpack::type::NIL;
					++_body->_obj->via.array.size;
				}

				try {
					*p = msgpack::object(std::forward<T>(val), *_body->_zone);
					++_body->_obj->via.array.size;
					return std::make_pair(_init_array(_body->array.size()), true);
				} catch (...) {
					_body->_obj->via.array.size = _body->array.size();   // an allocation failed: nothing is added
					throw;
				}
			} else {
				if (overwrite) {
					// As for a map: build the new object first, then drop the index of the old value.
					auto& old_value = _body->at(pos);
					if (old_value._body->_lock) {
						THROW(msgpack::const_error, "Locked object");
					}
					msgpack::object obj(std::forward<T>(val), *_body->_zone);
					old_value._deinitialize();
					_body->_obj->via.array.ptr[pos] = obj;
					old_value._body->_capacity = old_value._body->size();
					return std::make_pair(&_body->at(pos), true);
				}
				return std::make_pair(&_body->at(pos), false);
			}
		case Type::MAP:
			THROW(msgpack::type_error, "{} cannot have numeric keys", enum_name(_body->get_type()));
		default:
			THROW(msgpack::type_error, "{} is not a container that can have keys", enum_name(_body->get_type()));
	}
}


template <typename M, typename T, typename>
inline std::pair<MsgPack*, bool> MsgPack::_emplace(M&& o, T&& val, bool overwrite) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (o._body->get_type()) {
		case Type::STR:
			return _put(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size), std::forward<T>(val), overwrite);
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return _insert(static_cast<size_t>(o._body->_obj->via.i64), std::forward<T>(val), overwrite);
		case Type::POSITIVE_INTEGER:
			return _insert(static_cast<size_t>(o._body->_obj->via.u64), std::forward<T>(val), overwrite);
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


template <typename T>
inline std::pair<MsgPack*, bool> MsgPack::_insert(size_t pos, T&& val, bool overwrite) {
	if (_body->_const) {
		THROW(msgpack::const_error, "Constant object");
	}
	if (_body->_lock) {
		assert(!_body->_lock);
		THROW(msgpack::const_error, "Locked object");
	}
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			_init_array();
			[[fallthrough]];
		case Type::ARRAY:
			if (pos >= _body->_obj->via.array.size) {
				_reserve_array(pos + 1);

				// Initialize new elements.
				const auto plast = &_body->_obj->via.array.ptr[pos];
				auto p = &_body->_obj->via.array.ptr[_body->_obj->via.array.size];
				for (; p != plast; ++p) {
					p->type = msgpack::type::NIL;
					++_body->_obj->via.array.size;
				}

				try {
					*p = msgpack::object(std::forward<T>(val), *_body->_zone);
					++_body->_obj->via.array.size;
					return std::make_pair(_init_array(_body->array.size()), true);
				} catch (...) {
					_body->_obj->via.array.size = _body->array.size();   // an allocation failed: nothing is added
					throw;
				}
			} else {
				if (overwrite) {
					_reserve_array(_body->_obj->via.array.size + 1);

					auto p = &_body->_obj->via.array.ptr[pos];
					std::memmove(p + 1, p, (_body->_obj->via.array.size - pos) * sizeof(msgpack::object));
					*p = msgpack::object(std::forward<T>(val), *_body->_zone);
					++_body->_obj->via.array.size;
					return std::make_pair(_init_array(_body->array.size()), true);
				} else {
					return std::make_pair(&_body->at(pos), false);
				}
			}
			break;
		case Type::MAP:
			THROW(msgpack::type_error, "{} cannot have numeric keys", enum_name(_body->get_type()));
		default:
			THROW(msgpack::type_error, "{} is not a container that can have keys", enum_name(_body->get_type()));
	}
}
