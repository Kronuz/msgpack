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

// MsgPack constants, the undefined marker and the MsgPackType enum.
#pragma once

constexpr size_t  MSGPACK_STR_INIT_SIZE   = 4;
constexpr size_t  MSGPACK_MAP_INIT_SIZE   = 4;
constexpr size_t  MSGPACK_ARRAY_INIT_SIZE = 4;
constexpr double  MSGPACK_GROWTH_FACTOR   = 1.5;  // Choosing 1.5 as the factor allows memory reuse after 4 reallocations (https://github.com/facebook/folly/blob/master/folly/docs/FBVector.md)
constexpr uint8_t MSGPACK_EXT_BEGIN       = 0x80;
constexpr uint8_t MSGPACK_EXT_MASK        = 0x7f;
constexpr uint8_t MSGPACK_UNDEFINED       = MSGPACK_EXT_BEGIN;

static const char* undefined = "\0";


ENUM_CLASS(MsgPackType, uint8_t,
	NIL                 = msgpack::type::NIL,               //0x00
	BOOLEAN             = msgpack::type::BOOLEAN,           //0x01
	POSITIVE_INTEGER    = msgpack::type::POSITIVE_INTEGER,  //0x02
	NEGATIVE_INTEGER    = msgpack::type::NEGATIVE_INTEGER,  //0x03
	FLOAT               = msgpack::type::FLOAT,             //0x04
	STR                 = msgpack::type::STR,               //0x05
	ARRAY               = msgpack::type::ARRAY,             //0x06
	MAP                 = msgpack::type::MAP,               //0x07
	BIN                 = msgpack::type::BIN,               //0x08

	// Custom external types follow:
	UNDEFINED           = MSGPACK_UNDEFINED
)
