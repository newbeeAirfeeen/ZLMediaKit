//
// Created by shenhao on 2025/8/18.
//
#include "config_secure.h"
#ifdef ENABLE_OPENSSL
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <string>
#include <Util/base64.h>
static auto aes_128_cbc_encrypt(const std::string &key, const std::string &iv, const std::string &data) -> std::string{

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    // 初始化加密操作
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                                reinterpret_cast<const unsigned char*>(key.c_str()),
                                reinterpret_cast<const unsigned char*>(iv.c_str()))) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    std::string ciphertext;
    ciphertext.resize(data.size() + AES_BLOCK_SIZE);

    int len;
    int ciphertext_len;

    // 执行加密
    if (1 != EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&ciphertext[0]), &len,
                               reinterpret_cast<const unsigned char*>(data.c_str()), data.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len = len;

    // 完成加密
    if (1 != EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&ciphertext[len]), &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ciphertext_len);
    return ciphertext;

}
static auto aes_128_cbc_decrypt(const std::string &key, const std::string &iv, const std::string &data) -> std::string{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return "";

    // 初始化解密操作
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                                reinterpret_cast<const unsigned char*>(key.c_str()),
                                reinterpret_cast<const unsigned char*>(iv.c_str()))) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    std::string plaintext;
    plaintext.resize(data.size() + AES_BLOCK_SIZE);

    int len;
    int plaintext_len;

    // 执行解密
    if (1 != EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]), &len,
                               reinterpret_cast<const unsigned char*>(data.c_str()), data.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintext_len = len;

    // 完成解密
    if (1 != EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plaintext[len]), &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(plaintext_len);
    return plaintext;
}
// 帮我生成一个随机的32个字节的密钥和16个字节的IV
static const std::string KEY = "my_secret_key_12";
static const std::string IV = "unique_iv_123456";
const std::vector<std::string> CONTAINS_KEY = {
    "token",
    "key",
    "secret",
};
static auto is_contains_key(const std::string& key) -> bool {
    auto target_key = key;
    toolkit::strToLower(target_key);
    for (const auto& k : CONTAINS_KEY) {
        auto lower_k = k;
        toolkit::strToLower(lower_k);
        if(target_key.find(k) != std::string::npos) {
            return true;
        }
    }
    return false;
}
bool store_conf(toolkit::mINI_basic<std::string, toolkit::variant>& ini) {
    for(auto& ik : ini) {
        if(ik.second.empty()){
            continue;
        }
        if(is_contains_key(ik.first)){
            ik.second = encodeBase64(aes_128_cbc_encrypt(KEY, IV, ik.second));
        }
    }
    return true;
}
bool load_conf(toolkit::mINI_basic<std::string, toolkit::variant>& ini){
    for(auto& ik : ini) {
        if (ik.second.empty()){
            continue;
        }
        if(is_contains_key(ik.first)){
            auto decoded = decodeBase64(ik.second);
            ik.second = aes_128_cbc_decrypt(KEY, IV, decoded);
        }
    }
    return true;
}
#endif
