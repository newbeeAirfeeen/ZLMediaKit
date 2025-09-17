//
// Created by shenhao on 2025/8/6.
//
#include "magic.h"
#include "json/json.h"
#include "Util/util.h"
#include "Util/logger.h"
#if defined(ENABLE_OPENSSL)
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <regex>
#include <cstring>
// Base64 解码
static std::string base64_decode(const std::string& encoded) {
    BIO* bio, *b64;
    int decodeLen = encoded.length();
    char* buffer = (char*)malloc(decodeLen);
    memset(buffer, 0, decodeLen);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(encoded.data(), encoded.length());
    b64 = BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    int length = BIO_read(b64, buffer, decodeLen);
    std::string result(buffer, length);
    BIO_free_all(b64);
    free(buffer);
    return result;
}
static std::string base64url_to_base64(const std::string& input) {
    std::string out = input;
    std::replace(out.begin(), out.end(), '-', '+');
    std::replace(out.begin(), out.end(), '_', '/');
    while (out.size() % 4) out += '=';
    return out;
}
static std::string aes_decrypt(const std::string& b64cipher, const std::string& key) {
    std::string b64 = base64url_to_base64(b64cipher);
    std::string ciphertext = base64_decode(b64);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    unsigned char outbuf[1024];
    int outlen, tmplen;
    std::string plaintext;

    EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, reinterpret_cast<const unsigned char *>(key.data()), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 1);

    EVP_DecryptUpdate(ctx, outbuf, &outlen, reinterpret_cast<const unsigned char *>(ciphertext.data()), ciphertext.length());
    plaintext.append(reinterpret_cast<char *>(outbuf), outlen);

    EVP_DecryptFinal_ex(ctx, outbuf, &tmplen);
    plaintext.append(reinterpret_cast<char *>(outbuf), tmplen);

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}
static std::string get_magic_key(const std::string& url) {
    std::string key = "magic_key=";
    auto pos = url.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    auto end = url.find('&', pos);
    if (end == std::string::npos) end = url.size();
    return url.substr(pos, end - pos);
}
#endif
auto check_magic_key(const std::string& url, const std::string& key) -> bool {
#if defined(ENABLE_OPENSSL)
    std::string magic_key = get_magic_key(url);
    if(magic_key.empty()) {
        return true;
    }
    auto decoded_key = aes_decrypt(magic_key, key);
    DebugL << "magic key: " << decoded_key;
    const char* PATTERN = "[Closeli]|";
    auto it = decoded_key.find(PATTERN);
    if (it == std::string::npos) {
        return false;
    }
    auto content = decoded_key.substr(it + strlen(PATTERN));
    DebugL << "magic key content: " << content;
    std::istringstream iss(content);
    // 解析 JSON
    Json::CharReaderBuilder builder;
    Json::Value root;
    // 从content解析json
    std::string errs;
    TraceL << "json parse: " << content;
    if (!Json::parseFromStream(builder, iss, &root, &errs)) {
        WarnL << "json parse error: " << errs;
        return false;
    }
    TraceL << "json parse success";
    auto now = toolkit::getCurrentMillisecond();
    auto expire_at = root.isMember("publish_expired_at") && root["publish_expired_at"].isUInt64() ? root["publish_expired_at"].asUInt64() : 0;
    if (expire_at < now) {
        WarnL << "magic key expired, expire_at: " << expire_at << ", now: " << now;
        return false;
    }
    return true;
#else
    return true;
#endif
}
