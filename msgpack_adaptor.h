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

// The msgpack adaptors of MsgPack (convert, pack, object), which the constructors need.
#pragma once

#include "msgpack_body.h"

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
