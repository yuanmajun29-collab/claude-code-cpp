#include "util/crypto_utils.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <vector>
#include <array>

namespace claude {
namespace util {

std::string sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return std::string(reinterpret_cast<const char*>(hash), SHA256_DIGEST_LENGTH);
}

std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &result_len);
    return std::string(reinterpret_cast<const char*>(result), result_len);
}

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    size_t i = 0;
    size_t len = data.size();
    while (i < len) {
        size_t remaining = len - i;
        unsigned int octet_a = bytes[i++];
        unsigned int octet_b = (remaining > 1) ? bytes[i++] : 0;
        unsigned int octet_c = (remaining > 2) ? bytes[i++] : 0;

        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += (remaining > 1) ? base64_chars[(triple >> 6) & 0x3F] : '=';
        result += (remaining > 2) ? base64_chars[triple & 0x3F] : '=';
    }
    return result;
}

std::string base64_decode(const std::string& encoded) {
    static std::array<int, 256> lookup;
    static bool initialized = false;
    if (!initialized) {
        lookup.fill(-1);
        for (int i = 0; i < 64; i++) lookup[static_cast<unsigned char>(base64_chars[i])] = i;
        initialized = true;
    }

    std::string result;
    result.reserve(encoded.size() * 3 / 4);

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        if (lookup[c] == -1) continue;
        val = (val << 6) + lookup[c];
        valb += 6;
        if (valb >= 0) {
            result += static_cast<char>((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return result;
}

}  // namespace util
}  // namespace claude
