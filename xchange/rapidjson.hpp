/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2018 Dubalu LLC
 * Copyright (c) 2015 xpol xpolife@gmail.com
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

#pragma once

#include <cmath>                 // for std::isfinite, std::isnan
#include <string>                // for std::string

#include "msgpack.hpp"           // for msgpack::object
#include "rapidjson/document.h"  // for GenericDocument
#include "rapidjson/stringbuffer.h"  // for StringBuffer
#include "rapidjson/writer.h"    // for Writer


namespace msgpack_wrapper {

	// The text a map key is written as in JSON, which has string keys only: the compact JSON text of
	// the key. A number, boolean or null key `5`, `-1.5`, `true`, `null` is written as that text, an
	// array or map key as its own JSON text, a float key that is not finite as the name of the number,
	// `NaN`, `Infinity` or `-Infinity`, and an ext key (which JSON has no value for) as `null`. A float
	// that is not finite inside an array or map key is `null`, as it is in any value. Two keys can give
	// the same text (`5` and `"5"`), and then the JSON repeats the key. A str or bin key is written as
	// it is and does not come here.
	inline std::string json_key(const msgpack::object& key);

	// Makes the handler calls that `rapidjson::Document::Accept` makes for the document that
	// `as_document()` builds from `o`, and stops at the first one that returns false. JSON has no NaN
	// or Infinity, and the Writer refuses them (it stops the text there unless it was told to write the
	// names, which is not JSON), so a float that is not finite is made as a `Null()` call: the text of
	// `to_string()` is always JSON and does not stop at the number. `as_document()` keeps the number.
	template <typename Handler>
	inline bool accept(const msgpack::object& o, Handler& handler);

	// The handler call for the key of a map entry.
	template <typename Handler>
	inline bool accept_key(const msgpack::object& key, Handler& handler) {
		switch (key.type) {
			case msgpack::type::STR:
				return handler.Key(key.via.str.ptr, key.via.str.size);
			case msgpack::type::BIN:
				return handler.Key(key.via.bin.ptr, key.via.bin.size);
			default: {
				const std::string text = json_key(key);
				return handler.Key(text.data(), static_cast<rapidjson::SizeType>(text.size()));
			}
		}
	}

	template <typename Handler>
	inline bool accept(const msgpack::object& o, Handler& handler) {
		switch (o.type) {
			case msgpack::type::BOOLEAN:
				return handler.Bool(o.via.boolean);
			case msgpack::type::POSITIVE_INTEGER:
				return handler.Uint64(o.via.u64);
			case msgpack::type::NEGATIVE_INTEGER:
				return handler.Int64(o.via.i64);
			case msgpack::type::FLOAT:
				if (!std::isfinite(o.via.f64)) {
					return handler.Null();
				}
				return handler.Double(o.via.f64);
			case msgpack::type::STR:
				return handler.String(o.via.str.ptr, o.via.str.size);
			case msgpack::type::BIN:
				return handler.String(o.via.bin.ptr, o.via.bin.size);
			case msgpack::type::ARRAY: {
				if (!handler.StartArray()) {
					return false;
				}
				const msgpack::object* const pend = o.via.array.ptr + o.via.array.size;
				for (const msgpack::object* p = o.via.array.ptr; p < pend; ++p) {
					if (!accept(*p, handler)) {
						return false;
					}
				}
				return handler.EndArray(o.via.array.size);
			}
			case msgpack::type::MAP: {
				if (!handler.StartObject()) {
					return false;
				}
				const msgpack::object_kv* const pend = o.via.map.ptr + o.via.map.size;
				for (const msgpack::object_kv* p = o.via.map.ptr; p < pend; ++p) {
					if (!accept_key(p->key, handler)) {
						return false;
					}
					if (!accept(p->val, handler)) {
						return false;
					}
				}
				return handler.EndObject(o.via.map.size);
			}
			case msgpack::type::NIL: // fall through
			default:
				return handler.Null();
		}
	}

	inline std::string json_key(const msgpack::object& key) {
		if (key.type == msgpack::type::FLOAT && !std::isfinite(key.via.f64)) {
			return std::isnan(key.via.f64) ? "NaN" : key.via.f64 > 0 ? "Infinity" : "-Infinity";
		}
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		accept(key, writer);
		return std::string(buffer.GetString(), buffer.GetSize());
	}

}


namespace msgpack { MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) { namespace adaptor {

	template <typename Encoding, typename Allocator, typename StackAllocator>
	struct convert<rapidjson::GenericDocument<Encoding, Allocator, StackAllocator>> {
		msgpack::object const& operator()(msgpack::object const& o, rapidjson::GenericDocument<Encoding, Allocator, StackAllocator>& v) const {
			switch (o.type) {
				case msgpack::type::BOOLEAN:
					v.SetBool(o.via.boolean);
					break;
				case msgpack::type::POSITIVE_INTEGER:
					v.SetUint64(o.via.u64);
					break;
				case msgpack::type::NEGATIVE_INTEGER:
					v.SetInt64(o.via.i64);
					break;
				case msgpack::type::FLOAT:
					v.SetDouble(o.via.f64);
					break;
				case msgpack::type::STR:
					// Copied: msgpack's bytes are not NUL-terminated and do not outlive the object.
					v.SetString(o.via.str.ptr, o.via.str.size, v.GetAllocator());
					break;
				case msgpack::type::BIN:
					v.SetString(o.via.bin.ptr, o.via.bin.size, v.GetAllocator());
					break;
				case msgpack::type::ARRAY: {
					v.SetArray();
					v.Reserve(o.via.array.size, v.GetAllocator());
					msgpack::object* ptr = o.via.array.ptr;
					msgpack::object* END = ptr + o.via.array.size;
					for (; ptr < END; ++ptr) {
						rapidjson::GenericDocument<Encoding, Allocator, StackAllocator> element(&v.GetAllocator());
						ptr->convert(&element);
						v.PushBack(static_cast<rapidjson::GenericValue<Encoding, Allocator>&>(element), v.GetAllocator());
					}
					break;
				}
				case msgpack::type::MAP: {
					v.SetObject();
					msgpack::object_kv* ptr = o.via.map.ptr;
					msgpack::object_kv* END = ptr + o.via.map.size;
					for (; ptr < END; ++ptr) {
						// A key that is not a string is written as its JSON text (see msgpack_wrapper::json_key).
						rapidjson::GenericValue<Encoding, Allocator> key;
						if (ptr->key.type == msgpack::type::STR) {
							key.SetString(ptr->key.via.str.ptr, ptr->key.via.str.size, v.GetAllocator());
						} else if (ptr->key.type == msgpack::type::BIN) {
							key.SetString(ptr->key.via.bin.ptr, ptr->key.via.bin.size, v.GetAllocator());
						} else {
							const std::string text = msgpack_wrapper::json_key(ptr->key);
							key.SetString(text.data(), static_cast<rapidjson::SizeType>(text.size()), v.GetAllocator());
						}
						rapidjson::GenericDocument<Encoding, Allocator, StackAllocator> val(&v.GetAllocator());
						ptr->val.convert(&val);

						v.AddMember(key, val, v.GetAllocator());
					}
					break;
				}
				case msgpack::type::NIL:
				default:
					v.SetNull();
					break;
			}
			return o;
		}
	};


	template <typename Encoding, typename Allocator>
	struct convert<rapidjson::GenericValue<Encoding, Allocator>> {
		msgpack::object const& operator()(msgpack::object const& o, rapidjson::GenericValue<Encoding, Allocator>& v) const {
			rapidjson::GenericDocument<Encoding, Allocator> d;
			o >> d;
			return v = d;
		}
	};


	template <typename Encoding, typename Allocator>
	struct pack<rapidjson::GenericValue<Encoding, Allocator>> {
		template <typename Stream>
		msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, rapidjson::GenericValue<Encoding, Allocator> const& v) const {
			switch (v.GetType()) {
				case rapidjson::kNullType:
					return o.pack_nil();
				case rapidjson::kFalseType:
					return o.pack_false();
				case rapidjson::kTrueType:
					return o.pack_true();
				case rapidjson::kObjectType: {
					o.pack_map(v.MemberCount());
					typename rapidjson::GenericValue<Encoding, Allocator>::ConstMemberIterator i = v.MemberBegin(), END = v.MemberEnd();
					for (; i != END; ++i) {
						o.pack_str(i->name.GetStringLength()).pack_str_body(i->name.GetString(), i->name.GetStringLength());
						o.pack(i->value);
					}
					return o;
				}
				case rapidjson::kArrayType: {
					o.pack_array(v.Size());
					typename rapidjson::GenericValue<Encoding, Allocator>::ConstValueIterator i = v.Begin(), END = v.End();
					for (;i < END; ++i) {
						o.pack(*i);
					}
					return o;
				}
				case rapidjson::kStringType:
					return o.pack_str(v.GetStringLength()).pack_str_body(v.GetString(), v.GetStringLength());
				case rapidjson::kNumberType:
					if (v.IsInt())
						return o.pack_int(v.GetInt());
					if (v.IsUint())
						return o.pack_unsigned_int(v.GetUint());
					if (v.IsInt64())
						return o.pack_int64(v.GetInt64());
					if (v.IsUint64())
						return o.pack_uint64(v.GetUint64());
					if (v.IsDouble() || v.IsNumber())
						return o.pack_double(v.GetDouble());
				default:
					return o;
			}
		}
	};


	template <typename Encoding, typename Allocator, typename StackAllocator>
	struct pack<rapidjson::GenericDocument<Encoding, Allocator, StackAllocator>> {
		template <typename Stream>
		msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, rapidjson::GenericDocument<Encoding, Allocator, StackAllocator> const& v) const {
			o << static_cast<const rapidjson::GenericValue<Encoding, Allocator>&>(v);
			return o;
		}
	};


	template <typename Encoding, typename Allocator>
	struct object_with_zone<rapidjson::GenericValue<Encoding, Allocator>> {
		void operator()(msgpack::object::with_zone& o, rapidjson::GenericValue<Encoding, Allocator> const& v) const {
			switch (v.GetType()) {
				case rapidjson::kNullType:
					o.type = type::NIL;
					break;
				case rapidjson::kFalseType:
					o.type = type::BOOLEAN;
					o.via.boolean = false;
					break;
				case rapidjson::kTrueType:
					o.type = type::BOOLEAN;
					o.via.boolean = true;
					break;
				case rapidjson::kObjectType: {
					o.type = type::MAP;
					if (v.ObjectEmpty()) {
						o.via.map.ptr = nullptr;
						o.via.map.size = 0;
					} else {
						unsigned sz = v.MemberCount();
						object_kv* p = (object_kv*)o.zone.allocate_align(sizeof(object_kv)*sz);
						object_kv* const pend = p + sz;
						o.via.map.ptr = p;
						o.via.map.size = sz;
						typename rapidjson::GenericValue<Encoding, Allocator>::ConstMemberIterator it(v.MemberBegin());
						do {
							p->key = msgpack::object(it->name, o.zone);
							p->val = msgpack::object(it->value, o.zone);
							++p;
							++it;
						} while (p < pend);
					}
					break;
				}
				case rapidjson::kArrayType: {
					o.type = type::ARRAY;
					if (v.Empty()) {
						o.via.array.ptr = nullptr;
						o.via.array.size = 0;
					} else {
						msgpack::object* p = (msgpack::object*)o.zone.allocate_align(sizeof(msgpack::object)*v.Size());
						msgpack::object* const pend = p + v.Size();
						o.via.array.ptr = p;
						o.via.array.size = v.Size();
						typename rapidjson::GenericValue<Encoding, Allocator>::ConstValueIterator it(v.Begin());
						do {
							*p = msgpack::object(*it, o.zone);
							++p;
							++it;
						} while (p < pend);
					}
					break;
				}
				case rapidjson::kStringType: {
					o.type = type::STR;
					unsigned size = v.GetStringLength();
					char* ptr = (char*)o.zone.allocate_align(size);
					memcpy(ptr, v.GetString(), size);
					o.via.str.ptr = ptr;
					o.via.str.size = size;
					break;
				}
				case rapidjson::kNumberType:
					if (v.IsInt()) {
						o.type = type::NEGATIVE_INTEGER;
						o.via.i64 = v.GetInt();
					} else if (v.IsUint()) {
						o.type = type::POSITIVE_INTEGER;
						o.via.u64 = v.GetUint();
					} else if (v.IsInt64()) {
						o.type = type::NEGATIVE_INTEGER;
						o.via.i64 = v.GetInt64();
					} else if (v.IsUint64()) {
						o.type = type::POSITIVE_INTEGER;
						o.via.u64 = v.GetUint64();
					} else if (v.IsDouble()) {
						o.type = type::FLOAT;
						o.via.f64 = v.GetDouble();
					}
					break;
				default:
					break;

			}
		}
	};


	template <typename Encoding, typename Allocator, typename StackAllocator>
	struct object_with_zone<rapidjson::GenericDocument<Encoding, Allocator, StackAllocator>> {
		void operator()(msgpack::object::with_zone& o, rapidjson::GenericDocument<Encoding, Allocator, StackAllocator> const& v) const {
			o << static_cast<const rapidjson::GenericValue<Encoding, Allocator>&>(v);
		}
	};
}}}
