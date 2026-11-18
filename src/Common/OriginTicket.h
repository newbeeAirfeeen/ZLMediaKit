#pragma once
#include <algorithm>
#include <stdexcept>
#include "Util/base64.h"
#include "json/json.h"
#include <openssl/hmac.h>
#include <openssl/crypto.h>

// Wire v1. The viewer signature binds the stream, route and control metadata.
// A distinct signature authorizes only the internal RTMP play operation.
namespace origin_ticket {
inline std::string mac(const std::string &secret, const std::string &domain, const std::string &payload) {
    if (secret.empty()) throw std::invalid_argument("missing origin ticket secret");
    const auto input = domain + ":" + payload;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (!HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
              reinterpret_cast<const unsigned char *>(input.data()), input.size(), digest, &length))
        throw std::runtime_error("origin ticket signing failed");
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < length; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 15]);
    }
    return result;
}
inline bool equal(const std::string &a, const std::string &b) {
    return a.size() == b.size() && !a.empty() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
inline std::string pack(const Json::Value &route) {
    auto value = encodeBase64(Json::FastWriter().write(route));
    std::replace(value.begin(), value.end(), '+', '-');
    std::replace(value.begin(), value.end(), '/', '_');
    value.erase(value.find_last_not_of('=') + 1);
    return value;
}
inline Json::Value unpack(std::string value) {
    std::replace(value.begin(), value.end(), '-', '+');
    std::replace(value.begin(), value.end(), '_', '/');
    while (value.size() % 4) value += '=';
    Json::Value result;
    if (!Json::Reader().parse(decodeBase64(value), result) || !result.isObject())
        throw std::invalid_argument("invalid origin route");
    return result;
}
}
