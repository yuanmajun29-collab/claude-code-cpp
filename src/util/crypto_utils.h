#pragma once

#include <string>

namespace claude {
namespace util {

// Simple SHA-256 hash (using OpenSSL)
std::string sha256(const std::string& data);

// HMAC-SHA256 (using OpenSSL)
std::string hmac_sha256(const std::string& key, const std::string& data);

// Base64 encode
std::string base64_encode(const std::string& data);

// Base64 decode
std::string base64_decode(const std::string& encoded);

}  // namespace util
}  // namespace claude
