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

#pragma once

#include "likely.h"              // for likely, unlikely

#include <cassert>               // for assert
#include <cstdint>               // for std::uint32_t
#include <cstdlib>               // for std::malloc, std::realloc, std::free
#include <cstring>               // for std::memcmp, std::memcpy
#include <functional>            // for std::function
#include <memory>
#include <new>                   // for std::bad_alloc
#include <sstream>
#include <string_view>           // for std::string_view
#include <unordered_map>         // for std::unordered_map

#include "atomic_shared_ptr.h"
#include "enum.h"                // for ENUM_CLASS
#ifdef MSGPACK_EXCEPTION_HEADER
#include MSGPACK_EXCEPTION_HEADER
#else
#include "msgpack_exception.h"
#endif
#include "msgpack.hpp"
#include "strict_stox.hh"
#ifndef WITHOUT_RAPIDJSON
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "xchange/rapidjson.hpp"
#endif
#include "xchange/string_view.hpp"


#include "msgpack_types.h"
#include "msgpack_class.h"
#include "msgpack_iterator.h"
#include "msgpack_body.h"
#include "msgpack_ctor.h"
#include "msgpack_init.h"
#include "msgpack_find_erase.h"
#include "msgpack_put.h"
#include "msgpack_modify.h"
#include "msgpack_access.h"
#include "msgpack_hash.h"
#include "msgpack_convert.h"
#include "msgpack_arithmetic.h"
#include "msgpack_serialize.h"
