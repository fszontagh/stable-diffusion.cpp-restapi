#include "utils.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <openssl/evp.h>

namespace fs = std::filesystem;

namespace sdcpp {
namespace utils {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);
    
    // Set version to 4
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant to 1
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32);
    ss << '-';
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF);
    ss << '-';
    ss << std::setw(4) << (ab & 0xFFFF);
    ss << '-';
    ss << std::setw(4) << (cd >> 48);
    ss << '-';
    ss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);
    
    return ss.str();
}

std::string compute_sha256(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for hashing: " + filepath);
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize SHA256 digest");
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

static const char* base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (data[i] << 16);
        if (i + 1 < len) n |= (data[i + 1] << 8);
        if (i + 2 < len) n |= data[i + 2];
        
        result += base64_chars[(n >> 18) & 0x3F];
        result += base64_chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? base64_chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? base64_chars[n & 0x3F] : '=';
    }
    return result;
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(data.data(), data.size());
}

static const uint8_t base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

std::vector<uint8_t> base64_decode(const std::string& encoded) {
    if (encoded.empty()) {
        return {};
    }
    
    size_t len = encoded.size();
    size_t padding = 0;
    if (len >= 1 && encoded[len - 1] == '=') padding++;
    if (len >= 2 && encoded[len - 2] == '=') padding++;
    
    size_t out_len = (len / 4) * 3 - padding;
    std::vector<uint8_t> result(out_len);
    
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t n = 0;
        for (int k = 0; k < 4 && i + k < len; k++) {
            uint8_t c = base64_decode_table[(uint8_t)encoded[i + k]];
            if (c == 64 && encoded[i + k] != '=') {
                throw std::runtime_error("Invalid base64 character");
            }
            n = (n << 6) | (c & 0x3F);
        }
        
        if (j < out_len) result[j++] = (n >> 16) & 0xFF;
        if (j < out_len) result[j++] = (n >> 8) & 0xFF;
        if (j < out_len) result[j++] = n & 0xFF;
    }
    
    return result;
}

std::string get_file_extension(const std::string& filepath) {
    fs::path p(filepath);
    std::string ext = p.extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string get_filename(const std::string& filepath) {
    return fs::path(filepath).filename().string();
}

size_t get_file_size(const std::string& filepath) {
    std::error_code ec;
    auto size = fs::file_size(filepath, ec);
    return ec ? 0 : size;
}

bool file_exists(const std::string& filepath) {
    return fs::exists(filepath) && fs::is_regular_file(filepath);
}

bool directory_exists(const std::string& dirpath) {
    return fs::exists(dirpath) && fs::is_directory(dirpath);
}

bool create_directory(const std::string& dirpath) {
    std::error_code ec;
    if (fs::exists(dirpath)) {
        return fs::is_directory(dirpath);
    }
    return fs::create_directories(dirpath, ec) || !ec;
}

std::string get_timestamp() {
    return time_to_string(std::chrono::system_clock::now());
}

std::chrono::system_clock::time_point get_time_now() {
    return std::chrono::system_clock::now();
}

std::string time_to_string(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    gmtime_r(&time, &tm_buf);
    
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point string_to_time(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) {
        return std::chrono::system_clock::time_point{};
    }
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

std::vector<std::string> list_files(
    const std::string& dirpath,
    const std::vector<std::string>& extensions,
    bool recursive
) {
    std::vector<std::string> result;
    
    if (!fs::exists(dirpath) || !fs::is_directory(dirpath)) {
        return result;
    }
    
    auto matches_extension = [&extensions](const std::string& filename) {
        if (extensions.empty()) return true;
        std::string ext = get_file_extension(filename);
        for (const auto& e : extensions) {
            std::string check = e;
            if (!check.empty() && check[0] == '.') {
                check = check.substr(1);
            }
            std::transform(check.begin(), check.end(), check.begin(), ::tolower);
            if (ext == check) return true;
        }
        return false;
    };
    
    fs::path base(dirpath);
    
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
            if (entry.is_regular_file() && matches_extension(entry.path().string())) {
                // Get relative path from base
                auto rel = fs::relative(entry.path(), base);
                result.push_back(rel.string());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dirpath)) {
            if (entry.is_regular_file() && matches_extension(entry.path().string())) {
                result.push_back(entry.path().filename().string());
            }
        }
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

std::string sanitize_filename(const std::string& filename) {
    std::string result;
    result.reserve(filename.size());
    
    for (char c : filename) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            result += c;
        } else if (c == ' ' || c == '/') {
            result += '_';
        }
    }
    
    return result;
}

} // namespace utils
} // namespace sdcpp
