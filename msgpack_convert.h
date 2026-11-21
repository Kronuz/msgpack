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

// Conversions, size, scalar accessors, as_* and the type predicates.
#pragma once

inline MsgPack::operator bool() const {
	return as_boolean();
}


inline MsgPack::operator unsigned char() const {
	return as_u64();
}


inline MsgPack::operator unsigned short() const {
	return as_u64();
}


inline MsgPack::operator unsigned int() const {
	return as_u64();
}


inline MsgPack::operator unsigned long() const {
	return as_u64();
}


inline MsgPack::operator unsigned long long() const {
	return as_u64();
}


inline MsgPack::operator char() const {
	return as_i64();
}


inline MsgPack::operator short() const {
	return as_i64();
}


inline MsgPack::operator int() const {
	return as_i64();
}


inline MsgPack::operator long() const {
	return as_i64();
}


inline MsgPack::operator long long() const {
	return as_i64();
}

inline MsgPack::operator float() const {
	return as_f64();
}


inline MsgPack::operator double() const {
	return as_f64();
}


inline MsgPack::operator std::string() const {
	return as_str();
}


inline void MsgPack::reserve(size_t n) {
	switch (_body->get_type()) {
		case Type::MAP:
			return _reserve_map(n);
		case Type::ARRAY:
			return _reserve_array(n);
		default:
			return;
	}
}


inline size_t MsgPack::capacity() const noexcept {
	return _const_body->_capacity;
}


inline size_t MsgPack::size() const noexcept {
	return _const_body->size();
}


inline bool MsgPack::empty() const noexcept {
	return size() == 0;
}


inline uint64_t MsgPack::u64() const {
	switch (_const_body->get_type()) {
		case Type::NEGATIVE_INTEGER: {
			auto val = _const_body->_obj->via.i64;
			if (val < 0) {
				THROW(msgpack::type_error, "Negative value of NEGATIVE_INTEGER is not a valid POSITIVE_INTEGER");
			}
			return val;
		}
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64;
		default:
			THROW(msgpack::type_error, "Value of {} is not a valid POSITIVE_INTEGER", enum_name(_const_body->get_type()));
	}
}


inline int64_t MsgPack::i64() const {
	switch (_const_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64;
		case Type::POSITIVE_INTEGER: {
			auto val = _const_body->_obj->via.u64;
			if (val > INT64_MAX) {
				THROW(msgpack::type_error, "Value of POSITIVE_INTEGER is not a valid NEGATIVE_INTEGER");
			}
			return val;
		}
		default:
			THROW(msgpack::type_error, "Value of {} is not a valid NEGATIVE_INTEGER", enum_name(_const_body->get_type()));
	}
}


inline double MsgPack::f64() const {
	switch (_const_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64;
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64;
		case Type::FLOAT:
			return _const_body->_obj->via.f64;
		default:
			THROW(msgpack::type_error, "Value of {} is not a valid FLOAT", enum_name(_const_body->get_type()));
	}
}


inline std::string_view MsgPack::str_view() const {
	if (_const_body->get_type() == Type::STR) {
		return std::string_view(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size);
	}
	THROW(msgpack::type_error, "Value of {} is not a valid STR", enum_name(_const_body->get_type()));
}


inline std::string MsgPack::str() const {
	return std::string(str_view());
}


inline bool MsgPack::boolean() const {
	if (_const_body->get_type() == Type::BOOLEAN) {
		return _const_body->_obj->via.boolean;
	}
	THROW(msgpack::type_error, "Value of {} is not a valid BOOLEAN", enum_name(_const_body->get_type()));
}


inline uint64_t MsgPack::as_u64() const {
	switch (_const_body->get_type()) {
		case Type::NIL:
			return 0;
		case Type::BOOLEAN:
			return _const_body->_obj->via.boolean ? 1 : 0;
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64;
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64;
		case Type::FLOAT:
			return _const_body->_obj->via.f64;
		case Type::STR:
			return strict_stoull(nullptr, std::string_view(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size));
		case Type::BIN:
			return strict_stoull(nullptr, std::string_view(_const_body->_obj->via.bin.ptr, _const_body->_obj->via.bin.size));
		default:
			return 0;
	}
}


inline int64_t MsgPack::as_i64() const {
	switch (_const_body->get_type()) {
		case Type::NIL:
			return 0;
		case Type::BOOLEAN:
			return _const_body->_obj->via.boolean ? 1 : 0;
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64;
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64;
		case Type::FLOAT:
			return _const_body->_obj->via.f64;
		case Type::STR:
			return strict_stoll(nullptr, std::string_view(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size));
		case Type::BIN:
			return strict_stoll(nullptr, std::string_view(_const_body->_obj->via.bin.ptr, _const_body->_obj->via.bin.size));
		default:
			return 0;
	}
}


inline double MsgPack::as_f64() const {
	switch (_const_body->get_type()) {
		case Type::NIL:
			return 0;
		case Type::BOOLEAN:
			return _const_body->_obj->via.boolean ? 1 : 0;
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64;
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64;
		case Type::FLOAT:
			return _const_body->_obj->via.f64;
		case Type::STR:
			return strict_stod(nullptr, std::string_view(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size));
		case Type::BIN:
			return strict_stod(nullptr, std::string_view(_const_body->_obj->via.bin.ptr, _const_body->_obj->via.bin.size));
		default:
			return 0;
	}
}


inline std::string MsgPack::as_str() const {
	switch (_const_body->get_type()) {
		case Type::NIL:
			return "<null>";
		case Type::BOOLEAN:
			return _const_body->_obj->via.boolean ? "true" : "false";
		case Type::POSITIVE_INTEGER:
			return std::to_string(_const_body->_obj->via.u64);
		case Type::NEGATIVE_INTEGER:
			return std::to_string(_const_body->_obj->via.i64);
		case Type::FLOAT:
			return std::to_string(_const_body->_obj->via.f64);
		case Type::STR:
			return std::string(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size);
		case Type::ARRAY:
		case Type::MAP:
			return "<" + to_string() + ">";
		case Type::BIN:
			return "<" + std::string(_const_body->_obj->via.str.ptr, _const_body->_obj->via.str.size) + ">";
		default:
		case Type::UNDEFINED:
			return "<undefined>";
	}
}


inline bool MsgPack::as_boolean() const {
	switch (_const_body->get_type()) {
		case Type::MAP:
		case Type::ARRAY:
		case Type::STR:
			return size() > 0;
		case Type::NEGATIVE_INTEGER:
			return _const_body->_obj->via.i64 != 0;
		case Type::POSITIVE_INTEGER:
			return _const_body->_obj->via.u64 != 0;
		case Type::FLOAT:
			return _const_body->_obj->via.f64 != 0;
		case Type::BOOLEAN:
			return _const_body->_obj->via.boolean;
		default:
			return false;
	}
}


#ifndef WITHOUT_RAPIDJSON
inline rapidjson::Document MsgPack::as_document() const {
	rapidjson::Document doc;
	_const_body->_obj->convert(&doc);
	return doc;
}
#endif


inline bool MsgPack::is_undefined() const noexcept {
	return _const_body->get_type() == Type::UNDEFINED;
}


inline bool MsgPack::is_null() const noexcept {
	return _const_body->get_type() == Type::NIL;
}


inline bool MsgPack::is_boolean() const noexcept {
	return _const_body->get_type() == Type::BOOLEAN;
}


inline bool MsgPack::is_number() const noexcept {
	switch (_const_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
		case Type::POSITIVE_INTEGER:
		case Type::FLOAT:
			return true;
		default:
			return false;
	}
}


inline bool MsgPack::is_integer() const noexcept {
	switch (_const_body->get_type()) {
		case Type::NEGATIVE_INTEGER:
		case Type::POSITIVE_INTEGER:
			return true;
		default:
			return false;
	}
}


inline bool MsgPack::is_float() const noexcept {
	return _const_body->get_type() == Type::FLOAT;
}


inline bool MsgPack::is_map() const noexcept {
	return _const_body->get_type() == Type::MAP;
}


inline bool MsgPack::is_array() const noexcept {
	return _const_body->get_type() == Type::ARRAY;
}


inline bool MsgPack::is_string() const noexcept {
	return _const_body->get_type() == Type::STR;
}


inline MsgPack::Type MsgPack::get_type() const noexcept {
	return _const_body->get_type();
}
