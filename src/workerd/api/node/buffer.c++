// Copyright (c) 2017-2022 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0
// Copyright Joyent and Node contributors. All rights reserved. MIT license.

#include "buffer.h"
#include <workerd/jsg/buffersource.h>
#include <workerd/api/crypto-impl.h>
#include <kj/encoding.h>
#include <algorithm>

// These are defined by <sys/byteorder.h> or <netinet/in.h> on some systems.
// To avoid warnings, undefine them before redefining them.
#ifdef BSWAP_2
# undef BSWAP_2
#endif
#ifdef BSWAP_4
# undef BSWAP_4
#endif
#ifdef BSWAP_8
# undef BSWAP_8
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#define BSWAP_2(x) _byteswap_ushort(x)
#define BSWAP_4(x) _byteswap_ulong(x)
#define BSWAP_8(x) _byteswap_uint64(x)
#else
#define BSWAP_2(x) ((x) << 8) | ((x) >> 8)
#define BSWAP_4(x)                                                            \
  (((x) & 0xFF) << 24)  |                                                     \
  (((x) & 0xFF00) << 8) |                                                     \
  (((x) >> 8) & 0xFF00) |                                                     \
  (((x) >> 24) & 0xFF)
#define BSWAP_8(x)                                                            \
  (((x) & 0xFF00000000000000ull) >> 56) |                                     \
  (((x) & 0x00FF000000000000ull) >> 40) |                                     \
  (((x) & 0x0000FF0000000000ull) >> 24) |                                     \
  (((x) & 0x000000FF00000000ull) >> 8)  |                                     \
  (((x) & 0x00000000FF000000ull) << 8)  |                                     \
  (((x) & 0x0000000000FF0000ull) << 24) |                                     \
  (((x) & 0x000000000000FF00ull) << 40) |                                     \
  (((x) & 0x00000000000000FFull) << 56)
#endif

namespace workerd::api::node {

namespace {
template <typename T>
void SwapBytes(kj::ArrayPtr<kj::byte> bytes) {
  uint32_t len = bytes.size() / sizeof(T);
  T* data = reinterpret_cast<T*>(bytes.begin());
  for (uint32_t i = 0; i < len; i++) {
    if constexpr (kj::isSameType<T, uint16_t>()) {
      data[i] = BSWAP_2(data[i]);
    } else if constexpr (kj::isSameType<T, uint32_t>()) {
      data[i] = BSWAP_4(data[i]);
    } else {
      data[i] = BSWAP_8(data[i]);
    }
  }
}

enum class Encoding {
  ASCII,
  LATIN1,
  UTF8,
  UTF16LE,
  BASE64,
  BASE64URL,
  HEX,
};

Encoding getEncoding(kj::StringPtr encoding) {
  if (encoding == "utf8"_kj) {
    return Encoding::UTF8;
  } else if (encoding == "ascii") {
    return Encoding::ASCII;
  } else if (encoding == "latin1") {
    return Encoding::LATIN1;
  } else if (encoding == "utf16le") {
    return Encoding::UTF16LE;
  } else if (encoding == "base64") {
    return Encoding::BASE64;
  } else if (encoding == "base64url") {
    return Encoding::BASE64URL;
  } else if (encoding == "hex") {
    return Encoding::HEX;
  }

  KJ_UNREACHABLE;
}
}  // namespace

uint32_t BufferUtil::byteLength(jsg::Lock& js, v8::Local<v8::String> str) {
  // Gets the UTF8 byte length for the given input. We use v8::String
  // directly in order to avoid any copying and we just use the V8 API
  // to determine the UTF8 length.
  return str->Utf8Length(js.v8Isolate);
}

int BufferUtil::compare(
    jsg::Lock& js,
    kj::Array<kj::byte> one,
    kj::Array<kj::byte> two,
    jsg::Optional<CompareOptions> maybeOptions) {
  kj::ArrayPtr<kj::byte> ptrOne = one;
  kj::ArrayPtr<kj::byte> ptrTwo = two;

  KJ_IF_MAYBE(options, maybeOptions) {
    auto start = options->aStart.orDefault(0);
    auto end = options->aEnd.orDefault(ptrOne.size());
    ptrOne = ptrOne.slice(start, end);
    start = options->bStart.orDefault(0);
    end = options->bEnd.orDefault(ptrTwo.size());
    ptrTwo = ptrTwo.slice(start, end);
  }

  size_t toCompare = kj::min(ptrOne.size(), ptrTwo.size());
  auto result = toCompare > 0 ? memcmp(ptrOne.begin(), ptrTwo.begin(), toCompare) : 0;

  if (result == 0) {
    if (ptrOne.size() > ptrTwo.size())
      return 1;
    else if (ptrOne.size() < ptrTwo.size())
      return -1;
    else return 0;
  }

  return result > 0 ? 1 : -1;
}

kj::Array<kj::byte> BufferUtil::concat(
    jsg::Lock& js,
    kj::Array<kj::Array<kj::byte>> list,
    uint32_t length) {
  if (length == 0) return kj::Array<kj::byte>();

  auto dest = kj::heapArray<kj::byte>(length);
  uint32_t offset = 0;
  uint32_t remaining = length;
  auto ptr = dest.begin();
  for (auto& src : list) {
    auto amountToCopy = kj::min(src.size(), remaining);
    std::copy(src.begin(), src.begin() + amountToCopy, ptr + offset);
    offset += amountToCopy;
    remaining -= amountToCopy;
  }
  if (length - offset > 0) {
    memset(ptr + offset, 0, length - offset);
  }

  return kj::mv(dest);
}

kj::Array<kj::byte> BufferUtil::decodeString(
    jsg::Lock& js,
    v8::Local<v8::String> string,
    kj::String encoding) {
  if (string->Length() == 0) return kj::Array<kj::byte>();

  int flags = v8::String::HINT_MANY_WRITES_EXPECTED |
              v8::String::NO_NULL_TERMINATION |
              v8::String::REPLACE_INVALID_UTF8;

  switch (getEncoding(encoding)) {
    case Encoding::ASCII:
      // Fall-through
    case Encoding::LATIN1: {
      if (string->IsExternalOneByte()) {
        auto ext = string->GetExternalOneByteStringResource();
        auto dest = kj::heapArray<kj::byte>(ext->length());
        memcpy(dest.begin(), ext->data(), ext->length());
        return kj::mv(dest);
      } else {
        auto length = string->Length();
        auto dest = kj::heapArray<kj::byte>(length);
        string->WriteOneByte(js.v8Isolate, dest.begin(), 0, -1, flags);
        return kj::mv(dest);
      }
      break;
    }
    case Encoding::UTF8: {
      auto length = byteLength(js, string);
      auto dest = kj::heapArray<kj::byte>(length);
      string->WriteUtf8(js.v8Isolate, dest.asChars().begin(), length, nullptr, flags);
      return kj::mv(dest);
    }
    case Encoding::UTF16LE: {
      auto length = string->Length() * sizeof(uint16_t);
      auto dest = kj::heapArray<kj::byte>(length);
      auto ptr = reinterpret_cast<uint16_t*>(dest.begin());
      string->Write(js.v8Isolate, ptr, 0, -1, flags);
      return kj::mv(dest);
    }
    case Encoding::BASE64: {
      return kj::decodeBase64(kj::str(string));
    }
    case Encoding::BASE64URL: {
      return decodeBase64Url(kj::str(string));
    }
    case Encoding::HEX: {
      auto str = kj::str(string);
      if (str.size() % 2 != 0) {
        return kj::decodeHex(str.slice(0, str.size() - 1));
      }
      return kj::decodeHex(str);
    }
  }
  KJ_UNREACHABLE;
}

void BufferUtil::fillImpl(
    jsg::Lock& js,
    kj::Array<kj::byte> buffer,
    kj::OneOf<v8::Local<v8::String>, jsg::BufferSource> value,
    uint32_t start,
    uint32_t end,
    jsg::Optional<kj::String> encoding) {
  const auto fillFromBytes = [&](kj::ArrayPtr<kj::byte> source) {
    auto ptr = buffer.begin() + start;
    auto src = source.begin();
    auto remaining = end - start;
    uint32_t offset = 0;
    while (remaining > 0) {
      auto amountToCopy = kj::min(remaining, source.size());
      std::copy(src, src + amountToCopy, ptr + offset);
      remaining -= amountToCopy;
      offset += amountToCopy;
    }
  };

  KJ_SWITCH_ONEOF(value) {
    KJ_CASE_ONEOF(string, v8::Local<v8::String>) {
      auto decoded = decodeString(js, string, kj::mv(encoding).orDefault(kj::str("utf8")));
      fillFromBytes(decoded);
    }
    KJ_CASE_ONEOF(source, jsg::BufferSource) {
      fillFromBytes(source.asArrayPtr());
    }
  }
}

jsg::Optional<uint32_t> BufferUtil::indexOf(
    jsg::Lock& js,
    kj::Array<kj::byte> buffer,
    kj::OneOf<v8::Local<v8::String>, jsg::BufferSource> value,
    jsg::Optional<int32_t> maybeByteOffset,
    jsg::Optional<kj::String> encoding,
    bool fromEnd) {

  auto offset = maybeByteOffset.orDefault(0);

  // TODO(conform): Implement this
  JSG_REQUIRE(!fromEnd && offset > 0, Error,
      "buffer.indexOf with a negative index is not yet implemented");

  const auto indexOfImpl = [&](kj::ArrayPtr<kj::byte> needle) -> kj::Maybe<uint32_t> {
    if (needle.size() == 0) {
      return offset;
    }

    if (buffer.size() == 0) {
      return nullptr;
    }

    if (fromEnd && (needle.size() + offset) > buffer.size()) {
      return nullptr;
    }

    const std::boyer_moore_searcher searcher(needle.begin(), needle.end());
    auto it = std::search(buffer.begin() + offset, buffer.end(), searcher);
    if (it == buffer.end()) return nullptr;
    return it - buffer.begin();
  };

  KJ_SWITCH_ONEOF(value) {
    KJ_CASE_ONEOF(string, v8::Local<v8::String>) {
      auto decoded = decodeString(js, string, kj::mv(encoding).orDefault(kj::str("utf8"_kj)));
      return indexOfImpl(decoded);
    }
    KJ_CASE_ONEOF(source, jsg::BufferSource) {
      return indexOfImpl(source.asArrayPtr());
    }
  }
  KJ_UNREACHABLE;
}

void BufferUtil::swap(
    jsg::Lock& js,
    kj::Array<kj::byte> buffer, int size) {
  switch (size) {
    case 16: return SwapBytes<uint16_t>(buffer);
    case 32: return SwapBytes<uint32_t>(buffer);
    case 64: return SwapBytes<uint64_t>(buffer);
  }
  KJ_UNREACHABLE;
}

v8::Local<v8::String> BufferUtil::toString(
    jsg::Lock& js,
    kj::Array<kj::byte> bytes,
    uint32_t start,
    uint32_t end,
    kj::String encoding) {
  switch (getEncoding(encoding)) {
    case Encoding::ASCII:
      // While ASCII is not Latin1, Node.js treats these as equivalent cases so
      // we just fall through...
    case Encoding::LATIN1: {
      return jsg::v8StrFromLatin1(js.v8Isolate, bytes.slice(start, end));
    }
    case Encoding::UTF8: {
      return jsg::v8Str(js.v8Isolate, bytes.slice(start, end).asChars());
    }
    case Encoding::UTF16LE: {
      auto length = (end - start) / 2;
      uint16_t* view = reinterpret_cast<uint16_t*>(bytes.begin() + start);
      return jsg::v8Str(js.v8Isolate, kj::ArrayPtr<uint16_t>(view, length));
    }
    case Encoding::BASE64: {
      return jsg::v8Str(js.v8Isolate, kj::encodeBase64(bytes.slice(start, end)));
    }
    case Encoding::BASE64URL: {
      return jsg::v8Str(js.v8Isolate, kj::encodeBase64Url(bytes.slice(start, end)));
    }
    case Encoding::HEX: {
      return jsg::v8Str(js.v8Isolate, kj::encodeHex(bytes.slice(start, end)));
    }
  }
  KJ_UNREACHABLE;
}

uint32_t BufferUtil::write(
    jsg::Lock& js,
    kj::Array<kj::byte> buffer,
    v8::Local<v8::String> string,
    uint32_t offset,
    uint32_t length,
    kj::String encoding) {
  auto decoded = decodeString(js, string, kj::mv(encoding));
  auto amountToCopy = kj::min(kj::min(length, decoded.size()), buffer.size() - offset);
  auto ptr = decoded.begin();
  std::copy(ptr, ptr + amountToCopy, buffer.begin() + offset);
  return amountToCopy;
}

}  // namespace workerd::api::node {

