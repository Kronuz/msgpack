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

// to_string, serialize, stream output, std::hash and the msgpack adaptors.
#pragma once

#include "msgpack_body.h"
#include "msgpack_iterator.h"

inline std::string MsgPack::to_string([[maybe_unused]] int indent) const {
#ifdef WITHOUT_RAPIDJSON
	std::ostringstream oss;
	oss << *_const_body->_obj;
	return oss.str();
#else
	rapidjson::StringBuffer buffer;
	if (indent >= 0) {
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
		writer.SetIndent(' ', indent);
		msgpack_wrapper::accept(*_const_body->_obj, writer);
	} else {
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		msgpack_wrapper::accept(*_const_body->_obj, writer);
	}
	return std::string(buffer.GetString(), buffer.GetSize());
#endif
}


inline std::ostream& MsgPack::operator<<(std::ostream& s) const {
	s << *_const_body->_obj;
	return s;
}


template <typename B>
inline std::string MsgPack::serialize() const {
	B buf;
	msgpack::pack(&buf, *_const_body->_obj);
	return std::string(buf.data(), buf.size());
}


inline MsgPack MsgPack::unserialize(const char* data, std::size_t len, std::size_t& off) {
	return MsgPack(std::make_shared<Body>(Body::unpack_tag{}, data, len, off));
}


inline MsgPack MsgPack::unserialize(std::string_view s, std::size_t& off) {
	return unserialize(s.data(), s.size(), off);
}


inline MsgPack MsgPack::unserialize(const char* data, std::size_t len) {
	std::size_t off = 0;
	return unserialize(data, len, off);
}


inline MsgPack MsgPack::unserialize(std::string_view s) {
	std::size_t off = 0;
	return unserialize(s, off);
}


inline void MsgPack::set_data(const std::shared_ptr<const MsgPack::Data>& new_data) const {
	std::shared_ptr<const MsgPack::Data> old_data;
	while (!_body->_data.compare_exchange_weak(old_data, new_data));
}


inline const std::shared_ptr<const MsgPack::Data> MsgPack::get_data() const {
	return _body->_data.load();
}


inline void MsgPack::clear_data() const {
	set_data(nullptr);
}


inline unsigned long long MsgPack::get_flags() const {
	return _body->_flags.load();
}


inline unsigned long long MsgPack::set_flags(unsigned long long flags) const {
	return _body->_flags.exchange(flags);
}


inline std::ostream& operator<<(std::ostream& s, const MsgPack& o){
	return o.operator<<(s);
}


namespace std {
	template<>
	struct hash<MsgPack> {
		std::size_t operator()(const MsgPack &m) const {
			return m.hash();
		}
	};
}


namespace msgpack {
	MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
		namespace adaptor {
			////////////////////////////////////////////////////////////////////
			// convert

			template <>
			struct convert<MsgPack> {
				const msgpack::object& operator()(const msgpack::object& o, MsgPack& v) const {
					v = MsgPack(o);
					return o;
				}
			};

			////////////////////////////////////////////////////////////////////
			// pack

			template <>
			struct pack<MsgPack> {
				template <typename Stream>
				msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const MsgPack& v) const {
					o << v;
					return o;
				}
			};

			////////////////////////////////////////////////////////////////////
			// object and object_with_zone

			template <>
			struct object<MsgPack> {
				void operator()(msgpack::object& o, const MsgPack& v) const {
					o = msgpack::object(*v._const_body->_obj);
				}
			};

			template <>
			struct object_with_zone<MsgPack> {
				void operator()(msgpack::object::with_zone& o, const MsgPack& v) const {
					msgpack::object obj(*v._const_body->_obj, o.zone);
					o.type = obj.type;
					o.via = obj.via;
				}
			};

			template <>
			struct object<std::nullptr_t> {
				void operator()(msgpack::object& o, std::nullptr_t) const {
					msgpack::object nil;
					o = msgpack::object(nil);
				}
			};

			template <>
			struct object_with_zone<std::nullptr_t> {
				void operator()(msgpack::object::with_zone& o, std::nullptr_t) const {
					msgpack::object nil;
					msgpack::object obj(nil, o.zone);
					o.type = obj.type;
					o.via = obj.via;
				}
			};
		} // namespace adaptor
	} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack
