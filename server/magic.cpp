//
// Created by shenhao on 2025/8/6.
//
#include "magic.h"
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

#endif
auto contains_magic_key(const std::string& url, const std::string& target, const std::string& key) -> bool {
#if defined(ENABLE_OPENSSL)
    std::regex re(R"([?&]magic_key=([^&]+))");
    std::smatch match;

    if (!std::regex_search(url, match, re)) {
        return false;
    }

    if (match.size() <= 1) {
        return false;
    }

    std::string magic_key = match[1].str();
    auto decoded_key = aes_decrypt(magic_key, key);
    auto it = decoded_key.find(target);
    return it != std::string::npos;
#else
    return false;
#endif
}