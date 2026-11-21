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

// The MsgPack class declaration.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "msgpack_types.h"

class MsgPack {
	struct Body;

	template <typename T>
	class Iterator;

	std::shared_ptr<Body> _body;
	const Body* _const_body;

	MsgPack(const std::shared_ptr<Body>& b);
	MsgPack(std::shared_ptr<Body>&& b);

	void _initializer_array(std::initializer_list<MsgPack> list);
	void _initializer_map(std::initializer_list<MsgPack> list);
	void _initializer(std::initializer_list<MsgPack> list);

	template <typename T>
	void _assignment(T&& v);

	static msgpack::object _undefined() {
		msgpack::object obj;
		obj.type = msgpack::type::EXT;
		obj.via.ext.ptr = ::undefined;
		obj.via.ext.size = 1;
		return obj;
	}

	struct undefined_ctor {};
	MsgPack(undefined_ctor);

public:
	struct Data {};

	using Type = MsgPackType;

	using out_of_range = OutOfRange;
	using invalid_argument = InvalidArgument;

	using iterator = Iterator<MsgPack>;
	using const_iterator = Iterator<const MsgPack>;

	iterator begin();
	const_iterator begin() const;
	const_iterator cbegin() const;
	iterator end();
	const_iterator end() const;
	const_iterator cend() const;

	MsgPack();
	MsgPack(const MsgPack& other);
	MsgPack(MsgPack&& other) noexcept;
	MsgPack(std::initializer_list<MsgPack> list);

	template <typename T, typename = std::enable_if_t<not std::is_same<std::decay_t<T>, std::shared_ptr<Body>>::value>>
	MsgPack(T&& v);

	MsgPack& operator=(const MsgPack& other);
	MsgPack& operator=(MsgPack&& other);
	MsgPack& operator=(std::initializer_list<MsgPack> list);

	template <typename T>
	MsgPack& operator=(T&& v);

	// Shortcuts:
	static MsgPack NIL() { return MsgPack(Type::NIL); }
	static MsgPack BOOLEAN() { return MsgPack(Type::BOOLEAN); }
	static MsgPack POSITIVE_INTEGER() { return MsgPack(Type::POSITIVE_INTEGER); }
	static MsgPack NEGATIVE_INTEGER() { return MsgPack(Type::NEGATIVE_INTEGER); }
	static MsgPack FLOAT() { return MsgPack(Type::FLOAT); }
	static MsgPack STR() { return MsgPack(Type::STR); }
	static MsgPack ARRAY() { return MsgPack(Type::ARRAY); }
	static MsgPack MAP() { return MsgPack(Type::MAP); }
	static MsgPack BIN() { return MsgPack(Type::BIN); }

	static MsgPack& undefined() {
		// MsgPack::undefined() always returns a reference to the const "undefined" object
		static MsgPack undefined(undefined_ctor{});
		return undefined;
	}

private:
	MsgPack(Type type);

	MsgPack* _init_map(size_t pos);
	void _update_map(size_t pos);

	MsgPack* _init_array(size_t pos);
	void _update_array(size_t pos, bool other_objects = true);

	void _initialize();
	void _deinitialize();

	void _init_nil();
	void _init_negative_integer();
	void _init_positive_integer();
	void _init_float();
	void _init_boolean();
	void _init_str();
	void _init_bin();
	void _init_array();
	void _init_map();
	void _init_undefined();

	void _init_type(const int&);
	void _init_type(const long&);
	void _init_type(const long long&);
	void _init_type(const unsigned&);
	void _init_type(const unsigned long&);
	void _init_type(const unsigned long long&);
	void _init_type(const float&);
	void _init_type(const double&);
	void _init_type(const long double&);
	void _init_type(const bool&);
	void _init_type(const MsgPack& val);
	void _init_type(std::string_view val);

	void _reserve_str(size_t rsize);
	void _reserve_map(size_t rsize);
	void _reserve_array(size_t rsize);

	MsgPack::iterator _find(std::string_view key);
	MsgPack::const_iterator _find(std::string_view key) const;
	MsgPack::iterator _find(size_t pos);
	MsgPack::const_iterator _find(size_t pos) const;

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	std::pair<size_t, MsgPack::iterator> _erase(M&& o);
	std::pair<size_t, MsgPack::iterator> _erase(std::string_view key);
	std::pair<size_t, MsgPack::iterator> _erase(size_t pos);

	void _clear();

	template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	inline void _append(T&& o);
	template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	inline void _append(T&& o);
	inline void _append(std::string_view val);

	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	std::pair<MsgPack*, bool> _put(M&& o, T&& val, bool overwrite);
	template <typename T>
	std::pair<MsgPack*, bool> _put(std::string_view key, T&& val, bool overwrite);
	template <typename T>
	std::pair<MsgPack*, bool> _put(size_t pos, T&& val, bool overwrite);

	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	std::pair<MsgPack*, bool> _emplace(M&& o, T&& val, bool overwrite);
	template <typename T>
	std::pair<MsgPack*, bool> _insert(size_t pos, T&& val, bool overwrite);

	void _fill(bool recursive, bool lock);
	void _fill(bool recursive, bool lock) const;

	// update() makes the changes of `_update_apply()` only after `_update_check()` has made the calls
	// that can throw for a reason other than memory, without changing anything.
	void _update_check(const MsgPack& o);
	void _update_apply(const MsgPack& o);

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack::iterator _find(M&& o);
	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack::const_iterator _find(M&& o) const;

public:
	template <typename T>
	auto external(std::function<T(const msgpack::object&)>) const;

	void lock() const;
	bool locked() const;

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	size_t erase(M&& o);
	size_t erase(std::string_view s);
	size_t erase(size_t pos);
	iterator erase(iterator it);

	void clear();

	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack::iterator replace(M&& o, T&& val);
	template <typename T>
	MsgPack::iterator replace(std::string_view s, T&& val);
	template <typename T>
	MsgPack::iterator replace(size_t pos, T&& val);

	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	std::pair<MsgPack::iterator, bool> emplace(M&& o, T&& val);
	template <typename T>
	std::pair<MsgPack::iterator, bool> emplace(std::string_view s, T&& val);
	template <typename T>
	std::pair<MsgPack::iterator, bool> emplace(size_t pos, T&& val);

	template <typename K, typename V>
	std::pair<MsgPack::iterator, bool> insert(const std::pair<K&&, V&&>& val);
	std::pair<MsgPack::iterator, bool> insert(std::string_view s);

	template <typename T>
	void push_back(T&& v);

	template <typename M, std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
	void update(M&& o);
	template <typename M, std::enable_if_t<std::is_convertible<M, MsgPack>::value and not std::is_same<std::decay_t<M>, MsgPack>::value, int> = 0>
	void update(M&& o);

	// Same as replace(), but returning a reference:
	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack& put(M&& o, T&& val);
	template <typename T>
	MsgPack& put(std::string_view s, T&& val);
	template <typename T>
	MsgPack& put(size_t pos, T&& val);

	// Same as emplace(), but returning a reference:
	template <typename M, typename T, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack& add(M&& o, T&& val);
	template <typename T>
	MsgPack& add(std::string_view s, T&& val);
	template <typename T>
	MsgPack& add(size_t pos, T&& val);

	// Same as push_back(), but returning a reference:
	template <typename T>
	MsgPack& append(T&& v);

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack& get(M&& o);
	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	const MsgPack& get(M&& o) const;
	MsgPack& get(std::string_view s);
	const MsgPack& get(std::string_view s) const;
	MsgPack& get(size_t pos);
	const MsgPack& get(size_t pos) const;

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	MsgPack& at(M&& o);
	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	const MsgPack& at(M&& o) const;
	MsgPack& at(std::string_view s);
	const MsgPack& at(std::string_view s) const;
	MsgPack& at(size_t pos);
	const MsgPack& at(size_t pos) const;

	template <typename T>
	MsgPack& operator[](T&& o);
	template <typename T>
	const MsgPack& operator[](T&& o) const;

	template <typename T>
	MsgPack& path(const std::vector<T>& path);
	template <typename T>
	const MsgPack& path(const std::vector<T>& path) const;

	MsgPack select(std::string_view selector) const;

	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	iterator find(M&& o);
	template <typename M, typename = std::enable_if_t<std::is_same<std::decay_t<M>, MsgPack>::value>>
	const_iterator find(M&& o) const;
	iterator find(std::string_view s);
	const_iterator find(std::string_view s) const;
	iterator find(size_t pos);
	const_iterator find(size_t pos) const;

	template <typename T>
	size_t count(T&& v) const;

	std::size_t hash() const;

	explicit operator bool() const;
	explicit operator unsigned char() const;
	explicit operator unsigned short() const;
	explicit operator unsigned int() const;
	explicit operator unsigned long() const;
	explicit operator unsigned long long() const;
	explicit operator char() const;
	explicit operator short() const;
	explicit operator int() const;
	explicit operator long() const;
	explicit operator long long() const;
	explicit operator float() const;
	explicit operator double() const;
	explicit operator std::string() const;

	void reserve(size_t n);

	size_t capacity() const noexcept;

	size_t size() const noexcept;

	bool empty() const noexcept;

	uint64_t u64() const;
	int64_t i64() const;
	double f64() const;
	std::string_view str_view() const;
	std::string str() const;
	bool boolean() const;

	uint64_t as_u64() const;
	int64_t as_i64() const;
	double as_f64() const;
	std::string as_str() const;
	bool as_boolean() const;

#ifndef WITHOUT_RAPIDJSON
	rapidjson::Document as_document() const;
#endif

	bool is_undefined() const noexcept;
	bool is_null() const noexcept;
	bool is_boolean() const noexcept;
	bool is_number() const noexcept;
	bool is_integer() const noexcept;
	bool is_float() const noexcept;
	bool is_map() const noexcept;
	bool is_array() const noexcept;
	bool is_string() const noexcept;

	MsgPack::Type get_type() const noexcept;

	bool operator==(const MsgPack& other) const;
	bool operator!=(const MsgPack& other) const;

	MsgPack& operator++();
	MsgPack operator++(int);

	MsgPack& operator--();
	MsgPack operator--(int);

	template <typename T>
	MsgPack operator+(T&& o) const;

	template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator+=(T&& o);
	template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator+=(T&& o);

	MsgPack& operator+=(std::string_view o);


	template <typename T>
	MsgPack operator-(T&& o) const;

	template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator-=(T&& o);
	template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator-=(T&& o);


	template <typename T>
	MsgPack operator*(T&& o) const;

	template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator*=(T&& o);
	template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator*=(T&& o);


	template <typename T>
	MsgPack operator/(T&& o) const;

	template <typename T, std::enable_if_t<std::is_arithmetic<std::remove_reference_t<T>>::value or std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator/=(T&& o);
	template <typename T, std::enable_if_t<std::is_convertible<T, MsgPack>::value and not std::is_arithmetic<std::remove_reference_t<T>>::value and not std::is_same<std::decay_t<T>, MsgPack>::value, int> = 0>
	MsgPack& operator/=(T&& o);


	std::ostream& operator<<(std::ostream& s) const;

	std::string to_string(int indent=-1) const;

	template <typename B=msgpack::sbuffer>
	std::string serialize() const;
	static MsgPack unserialize(const char* data, std::size_t len, std::size_t& off);
	static MsgPack unserialize(const char* data, std::size_t len);
	static MsgPack unserialize(std::string_view s, std::size_t& off);
	static MsgPack unserialize(std::string_view s);

	void set_data(const std::shared_ptr<const Data>& new_data) const;
	const std::shared_ptr<const Data> get_data() const;
	void clear_data() const;

	unsigned long long get_flags() const;
	unsigned long long set_flags(unsigned long long flags) const;

	friend msgpack::adaptor::convert<MsgPack>;
	friend msgpack::adaptor::pack<MsgPack>;
	friend msgpack::adaptor::object<MsgPack>;
	friend msgpack::adaptor::object_with_zone<MsgPack>;
};
