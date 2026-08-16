#include "sentinel/ja3.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t read_u24_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

// 注意：RFC 1321 MD5。JA3 被定义为字段串的 MD5 十六进制摘要，
// 自实现可避免引入 OpenSSL 链接依赖。
class Md5 {
public:
    Md5() = default;

    void update(const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        total_len_ += len;
        if (buffered_ > 0) {
            size_t take = 64 - buffered_;
            if (len < take) {
                take = len;
            }
            std::memcpy(buf_ + buffered_, p, take);
            buffered_ += take;
            p += take;
            len -= take;
            if (buffered_ == 64) {
                process(buf_);
                buffered_ = 0;
            }
        }
        while (len >= 64) {
            process(p);
            p += 64;
            len -= 64;
        }
        if (len > 0) {
            std::memcpy(buf_, p, len);
            buffered_ = len;
        }
    }

    void finalize(uint8_t out[16]) {
        const uint64_t bit_len = total_len_ * 8;
        const uint8_t pad = 0x80;
        const uint8_t zero = 0;
        update(&pad, 1);
        if (buffered_ <= 56) {
            while (buffered_ < 56) {
                update(&zero, 1);
            }
        } else {
            while (buffered_ < 64) {
                update(&zero, 1);
            }
            while (buffered_ < 56) {
                update(&zero, 1);
            }
        }
        uint8_t len_bytes[8];
        for (int i = 0; i < 8; ++i) {
            len_bytes[i] = static_cast<uint8_t>(bit_len >> (8 * i));
        }
        update(len_bytes, sizeof(len_bytes));

        put_le32(out, a0_);
        put_le32(out + 4, b0_);
        put_le32(out + 8, c0_);
        put_le32(out + 12, d0_);
    }

private:
    static uint32_t le32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    static void put_le32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }

    static uint32_t rotl(uint32_t x, int s) {
        return (x << s) | (x >> (32 - s));
    }

    void process(const uint8_t* block) {
        uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = le32(block + 4 * i);
        }
        uint32_t a = a0_;
        uint32_t b = b0_;
        uint32_t c = c0_;
        uint32_t d = d0_;
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) & 15;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) & 15;
            }
            const uint32_t tmp = d;
            d = c;
            c = b;
            b = b + rotl(a + f + k_[i] + m[g], s_[i]);
            a = tmp;
        }
        a0_ += a;
        b0_ += b;
        c0_ += c;
        d0_ += d;
    }

    uint32_t a0_ = 0x67452301;
    uint32_t b0_ = 0xefcdab89;
    uint32_t c0_ = 0x98badcfe;
    uint32_t d0_ = 0x10325476;
    uint8_t buf_[64]{};
    size_t buffered_ = 0;
    uint64_t total_len_ = 0;

    static constexpr uint32_t k_[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };

    static constexpr uint8_t s_[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
};

// 从包含 ClientHello 的 TLS 记录中提取 JA3 字段串。
// 输入从 TLS record 头开始；无扩展的最小 ClientHello 产出 "771,<密码套件>,,,"。
bool parse_client_hello(const uint8_t* data, size_t len, std::string& out) {
    if (len < 5 || data[0] != 0x16 || data[1] != 0x03) {
        return false;
    }
    const size_t rec_len = read_u16_be(data + 3);
    if (5 + rec_len > len) {
        return false;
    }
    const uint8_t* p = data + 5;
    size_t rem = rec_len;

    if (rem < 4 || p[0] != 0x01) {
        return false;
    }
    const size_t hs_len = read_u24_be(p + 1);
    p += 4;
    rem -= 4;
    if (hs_len > rem) {
        return false;
    }

    const uint8_t* body = p;
    const size_t body_len = hs_len;
    size_t o = 0;

    if (body_len < 2) {
        return false;
    }
    const uint16_t version = read_u16_be(body);
    if (version < 0x0301 || version > 0x0304) {
        return false;
    }
    o += 2;

    if (body_len < o + 32) {
        return false;
    }
    o += 32;

    if (o + 1 > body_len) {
        return false;
    }
    const size_t session_len = body[o++];
    if (o + session_len > body_len) {
        return false;
    }
    o += session_len;

    if (o + 2 > body_len) {
        return false;
    }
    const size_t cipher_len = read_u16_be(body + o);
    o += 2;
    if (cipher_len % 2 != 0 || o + cipher_len > body_len) {
        return false;
    }
    std::string ciphers;
    for (size_t i = 0; i < cipher_len; i += 2) {
        if (!ciphers.empty()) {
            ciphers += '-';
        }
        ciphers += std::to_string(read_u16_be(body + o + i));
    }
    o += cipher_len;

    if (o + 1 > body_len) {
        return false;
    }
    const size_t compression_len = body[o++];
    if (o + compression_len > body_len) {
        return false;
    }
    o += compression_len;

    std::string extensions;
    std::string curves;
    std::string formats;
    if (o + 2 <= body_len) {
        const size_t ext_len = read_u16_be(body + o);
        o += 2;
        if (o + ext_len > body_len) {
            return false;
        }
        const size_t ext_end = o + ext_len;
        while (o + 4 <= ext_end) {
            const uint16_t type = read_u16_be(body + o);
            const size_t len = read_u16_be(body + o + 2);
            o += 4;
            if (o + len > ext_end) {
                return false;
            }
            // 注意：GREASE 值不进入 JA3 字段串。
            const bool grease = (type & 0x0f0f) == 0x0a0a;
            if (!grease) {
                if (!extensions.empty()) {
                    extensions += '-';
                }
                extensions += std::to_string(type);
                if (type == 10 && len >= 2) {
                    const size_t group_len = read_u16_be(body + o);
                    if (2 + group_len > len || group_len % 2 != 0) {
                        return false;
                    }
                    for (size_t i = 0; i < group_len; i += 2) {
                        if (!curves.empty()) {
                            curves += '-';
                        }
                        curves += std::to_string(read_u16_be(body + o + 2 + i));
                    }
                } else if (type == 11 && len >= 1) {
                    const size_t fmt_len = body[o];
                    if (1 + fmt_len > len) {
                        return false;
                    }
                    for (size_t i = 0; i < fmt_len; ++i) {
                        if (!formats.empty()) {
                            formats += '-';
                        }
                        formats += std::to_string(body[o + 1 + i]);
                    }
                }
            }
            o += len;
        }
    }

    out = std::to_string(version) + "," + ciphers + "," + extensions + "," +
          curves + "," + formats;
    return true;
}

} // namespace

extern "C" int sentinel_ja3_from_hello(const uint8_t* data, size_t len,
                                       char* out, size_t cap) {
    if (data == nullptr || out == nullptr || cap < 33 || len < 5) {
        return -1;
    }

    std::string ja3;
    if (!parse_client_hello(data, len, ja3)) {
        return -1;
    }

    Md5 md5;
    md5.update(ja3.data(), ja3.size());
    uint8_t digest[16];
    md5.finalize(digest);

    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    out[32] = '\0';
    return 32;
}
