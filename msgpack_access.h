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

// Element access: get, at, operator[], path, select, find and count.
#pragma once

#include "msgpack_class.h"

template <typename M, typename>
inline MsgPack& MsgPack::get(M&& o) {
	_fill(false, false);
	switch (o._body->get_type()) {
		case Type::STR:
			return get(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return get(static_cast<size_t>(o._body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return get(static_cast<size_t>(o._body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


template <typename M, typename>
inline const MsgPack& MsgPack::get(M&& o) const {
	_fill(false, false);
	switch (o._const_body->get_type()) {
		case Type::STR:
			return get(std::string_view(o._const_body->_obj->via.str.ptr, o._const_body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._const_body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
			}
			return get(static_cast<size_t>(o._const_body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return get(static_cast<size_t>(o._const_body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
	}
}


inline MsgPack& MsgPack::get(std::string_view key) {
	auto it = find(key);
	if (it == end()) {
		return *_put(key, MsgPack::undefined(), false).first;
	}
	return _body->at(key);
}


inline const MsgPack& MsgPack::get(std::string_view key) const {
	auto it = find(key);
	if (it == cend()) {
		return MsgPack::undefined();
	}
	return _const_body->at(key);
}


inline MsgPack& MsgPack::get(size_t pos) {
	auto it = find(pos);
	if (it == end()) {
		return *_put(pos, MsgPack::undefined(), false).first;
	}
	return _body->at(pos);
}


inline const MsgPack& MsgPack::get(size_t pos) const {
	auto it = find(pos);
	if (it == cend()) {
		return MsgPack::undefined();
	}
	return _const_body->at(pos);
}


template <typename M, typename>
inline MsgPack& MsgPack::at(M&& o) {
	_fill(false, false);
	switch (o._body->get_type()) {
		case Type::STR:
			return at(std::string_view(o._body->_obj->via.str.ptr, o._body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
			}
			return at(static_cast<size_t>(o._body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return at(static_cast<size_t>(o._body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._body->get_type()), enum_name(_body->get_type()));
	}
}


template <typename M, typename>
inline const MsgPack& MsgPack::at(M&& o) const {
	_fill(false, false);
	switch (o._const_body->get_type()) {
		case Type::STR:
			return at(std::string_view(o._const_body->_obj->via.str.ptr, o._const_body->_obj->via.str.size));
		case Type::NEGATIVE_INTEGER:
			if (o._const_body->_obj->via.i64 < 0) {
				THROW(msgpack::type_error, "Negative value of {} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
			}
			return at(static_cast<size_t>(o._const_body->_obj->via.i64));
		case Type::POSITIVE_INTEGER:
			return at(static_cast<size_t>(o._const_body->_obj->via.u64));
		default:
			THROW(msgpack::type_error, "{} is not a valid key type for {}", enum_name(o._const_body->get_type()), enum_name(_const_body->get_type()));
	}
}


inline MsgPack& MsgPack::at(std::string_view key) {
	_fill(false, false);
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			THROW(out_of_range, "undefined");
		case Type::MAP:
			return _body->at(key);
		case Type::ARRAY:
			THROW(msgpack::type_error, "{} cannot have string keys", enum_name(_body->get_type()));
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}

inline const MsgPack& MsgPack::at(std::string_view key) const {
	_fill(false, false);
	switch (_const_body->get_type()) {
		case Type::UNDEFINED:
			THROW(out_of_range, "undefined");
		case Type::MAP:
			return _const_body->at(key);
		case Type::ARRAY:
			THROW(msgpack::type_error, "{} cannot have string keys", enum_name(_const_body->get_type()));
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_const_body->get_type()));
	}
}


inline MsgPack& MsgPack::at(size_t pos) {
	_fill(false, false);
	switch (_body->get_type()) {
		case Type::UNDEFINED:
			THROW(out_of_range, "undefined");
		case Type::MAP:
			if (pos >= _body->_obj->via.map.size) {
				THROW(out_of_range, "The map only contains {} elements", _body->_obj->via.map.size);
			}
			return at(std::string_view(_body->_obj->via.map.ptr[pos].key.via.str.ptr, _body->_obj->via.map.ptr[pos].key.via.str.size));
		case Type::ARRAY:
			return _body->at(pos);
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


inline const MsgPack& MsgPack::at(size_t pos) const {
	_fill(false, false);
	switch (_const_body->get_type()) {
		case Type::UNDEFINED:
			THROW(out_of_range, "undefined");
		case Type::MAP:
			if (pos >= _const_body->_obj->via.map.size) {
				THROW(out_of_range, "The map only contains {} elements", _const_body->_obj->via.map.size);
			}
			return at(std::string_view(_const_body->_obj->via.map.ptr[pos].key.via.str.ptr, _const_body->_obj->via.map.ptr[pos].key.via.str.size));
		case Type::ARRAY:
			return _const_body->at(pos);
		default:
			THROW(msgpack::type_error, "{} is not iterable", enum_name(_body->get_type()));
	}
}


template <typename T>
inline MsgPack& MsgPack::operator[](T&& o) {
	return get(std::forward<T>(o));
}

template <typename T>
inline const MsgPack& MsgPack::operator[](T&& o) const {
	return get(std::forward<T>(o));
}


template <typename T>
inline MsgPack& MsgPack::path(const std::vector<T>& path) {
	auto current = this;
	for (const auto& s : path) {
		switch (current->_const_body->get_type()) {
			case Type::MAP: {
				current = &current->at(s);
				break;
			}
			case Type::ARRAY: {
				int errno_save;
				auto pos = strict_stoz(&errno_save, s);
				if (errno_save != 0) {
					THROW(invalid_argument, "The index for the array must be a positive integer");
				}
				current = &current->at(pos);
				break;
			}

			default:
				THROW(invalid_argument, "The container must be a map or an array to access");
		}
	}

	return *current;
}


template <typename T>
inline const MsgPack& MsgPack::path(const std::vector<T>& path) const {
	auto current = this;
	for (const auto& s : path) {
		switch (current->get_type()) {
			case Type::MAP: {
				current = &current->at(s);
				break;
			}
			case Type::ARRAY: {
				int errno_save;
				auto pos = strict_stoz(&errno_save, s);
				if (errno_save != 0) {
					THROW(invalid_argument, "The index for the array must be a positive integer");
				}
				current = &current->at(pos);
				break;
			}

			default:
				THROW(invalid_argument, "The container must be a map or an array to access");
		}
	}

	return *current;
}


inline MsgPack MsgPack::select(std::string_view selector) const {
	// .field.subfield.name
	// {field{subfield{name}other{name}}}

	MsgPack ret;

	std::vector<const MsgPack*> base_stack;
	std::vector<const MsgPack*> input_stack;
	std::vector<MsgPack*> output_stack;

	auto base = this;
	auto input = base;
	auto output = &ret;

	std::string_view name;
	const char* name_off = nullptr;
	const char* off = selector.data();
	const char* end = off + selector.size();

	for (; off != end; ++off) {
		switch (*off) {
			case '.':
				if (name_off) {
					name = std::string_view(name_off, off - name_off);
					name_off = nullptr;
				}
				if (!name.empty()) {
					if (input) {
						auto it = input->find(name);
						if (it != input->end()) {
							input = &it.value();
						} else {
							input = nullptr;
						}
					}
					name = "";
				}
				break;
			case '{':
				if (name_off) {
					name = std::string_view(name_off, off - name_off);
					name_off = nullptr;
				}
				base_stack.push_back(base);
				input_stack.push_back(input);
				output_stack.push_back(output);
				if (!name.empty()) {
					if (input && output) {
						auto it = input->find(name);
						if (it != input->end()) {
							input = &it.value();
							base = input;
							output = &(*output)[name];
						} else {
							input = nullptr;
							output = nullptr;
						}
					}
					name = "";
				}
				break;
			case '}':
				if (name_off) {
					name = std::string_view(name_off, off - name_off);
					name_off = nullptr;
				}
				if (!name.empty()) {
					if (input && output) {
						auto it = input->find(name);
						if (it != input->end()) {
							(*output)[name] = it.value();
						}
					}
					name = "";
				}
				if (!output_stack.size()) {
					THROW(invalid_argument, "Unbalanced braces.");
				}
				base = base_stack.back();
				base_stack.pop_back();
				input = input_stack.back();
				input_stack.pop_back();
				output = output_stack.back();
				output_stack.pop_back();
				break;
			case ',':
			case ';':
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				input = base;
				if (name_off) {
					name = std::string_view(name_off, off - name_off);
					name_off = nullptr;
				}
				break;
			default:
				if (!name.empty()) {
					if (input && output) {
						auto it = input->find(name);
						if (it != input->end()) {
							(*output)[name] = it.value();
						}
					}
					name = "";
				}
				if (!name_off) {
					name_off = off;
				}
				break;
		}
	}

	if (output_stack.size()) {
		THROW(invalid_argument, "Unbalanced braces.");
	}

	if (name_off) {
		name = std::string_view(name_off, off - name_off);
		name_off = nullptr;
	}
	if (!name.empty()) {
		if (input && output) {
			auto it = input->find(name);
			if (it != input->end()) {
				*output = it.value();
			}
		}
		name = "";
	}

	return ret;
}


template <typename M, typename>
inline MsgPack::iterator MsgPack::find(M&& o) {
	_fill(false, false);
	return _find(std::forward<M>(o));
}


template <typename M, typename>
inline MsgPack::const_iterator MsgPack::find(M&& o) const {
	_fill(false, false);
	return _find(std::forward<M>(o));
}


inline MsgPack::iterator MsgPack::find(std::string_view s) {
	_fill(false, false);
	return _find(s);
}


inline MsgPack::const_iterator MsgPack::find(std::string_view s) const {
	_fill(false, false);
	return _find(s);
}


inline MsgPack::iterator MsgPack::find(size_t pos) {
	_fill(false, false);
	return _find(pos);
}


inline MsgPack::const_iterator MsgPack::find(size_t pos) const {
	_fill(false, false);
	return _find(pos);
}


template <typename T>
inline size_t MsgPack::count(T&& v) const {
	return find(std::forward<T>(v)) == end() ? 0 : 1;
}
