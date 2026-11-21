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

// MsgPack::Body, the shared state behind a value.
#pragma once

struct MsgPack::Body {
	std::unordered_map<std::string_view, std::pair<MsgPack, MsgPack>> map;
	std::vector<MsgPack> array;

	mutable atomic_shared_ptr<const Data> _data;
	mutable std::atomic<unsigned long long> _flags;

	std::atomic_bool _lock;
	bool _initialized;
	bool _const;

	const std::shared_ptr<msgpack::zone> _zone;
	const std::shared_ptr<msgpack::object> _base;
	std::weak_ptr<Body> _parent;
	bool _is_key;
	size_t _pos;
	MsgPack _key;
	msgpack::object* _obj;
	size_t _capacity;

	Body(const std::shared_ptr<msgpack::zone>& zone,
		 const std::shared_ptr<msgpack::object>& base,
		 const std::shared_ptr<Body>& parent,
		 bool is_key,
		 size_t pos,
		 const std::shared_ptr<Body>& key,
		 msgpack::object* obj
		) : _flags(0),
			_lock(false),
			_initialized(false),
			_const(false),
			_zone(zone),
			_base(base),
			_parent(parent),
			_is_key(is_key),
			_pos(pos),
			_key(key),
			_obj(obj),
			_capacity(size()) { }

	Body(const std::shared_ptr<msgpack::zone>& zone,
		 const std::shared_ptr<msgpack::object>& base,
		 const std::shared_ptr<Body>& parent,
		 msgpack::object* obj
		) : _flags(0),
			_lock(false),
			_initialized(false),
			_const(false),
			_zone(zone),
			_base(base),
			_parent(parent),
			_is_key(false),
			_pos(-1),
			_key(std::shared_ptr<Body>()),
			_obj(obj),
			_capacity(size()) { }

	template <typename T>
	Body(T&& v)
		: _flags(0),
		  _lock(false),
		  _initialized(false),
		  _const(false),
		  _zone(std::make_shared<msgpack::zone>()),
		  _base(std::make_shared<msgpack::object>(std::forward<T>(v), *_zone)),
		  _parent(std::shared_ptr<Body>()),
		  _is_key(false),
		  _pos(-1),
		  _key(std::shared_ptr<Body>()),
		  _obj(_base.get()),
		  _capacity(size()) { }

	// Reads one object from `data` straight into this body's zone: the unpacked tree is the
	// body's tree, with no second copy.
	struct unpack_tag {};
	Body(unpack_tag, const char* data, std::size_t len, std::size_t& off)
		: _flags(0),
		  _lock(false),
		  _initialized(false),
		  _const(false),
		  _zone(std::make_shared<msgpack::zone>()),
		  _base(std::make_shared<msgpack::object>(msgpack::unpack(*_zone, data, len, off))),
		  _parent(std::shared_ptr<Body>()),
		  _is_key(false),
		  _pos(-1),
		  _key(std::shared_ptr<Body>()),
		  _obj(_base.get()),
		  _capacity(size()) { }

	MsgPack& at(size_t pos) {
		return array.at(pos);
	}

	const MsgPack& at(size_t pos) const {
		return array.at(pos);
	}

	MsgPack& at(std::string_view key) {
		return map.at(key).second;
	}

	const MsgPack& at(std::string_view key) const {
		return map.at(key).second;
	}

	size_t size() const noexcept {
		switch (_obj->type) {
			case msgpack::type::MAP:
				return _obj->via.map.size;
			case msgpack::type::ARRAY:
				return _obj->via.array.size;
			case msgpack::type::STR:
				return _obj->via.str.size;
			default:
				return 0;
		}
	}

	Type get_type() const noexcept {
		return _obj->type == msgpack::type::EXT ? (Type)(_obj->via.ext.type() | MSGPACK_EXT_BEGIN) : (Type)_obj->type;
	}
};
