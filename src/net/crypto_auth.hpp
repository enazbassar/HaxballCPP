#pragma once

#include <cstdint>

#include "buffer.hpp"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/sha.h>
#include <openssl/param_build.h>
#include <string>
#include <vector>
#include <stdexcept>

inline std::string base64url_encode(const unsigned char* data, size_t len) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    while (i < len) {
        uint32_t a = data[i++];
        uint32_t b = (i < len) ? data[i++] : 0;
        uint32_t c = (i < len) ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(tbl[(triple >> 18) & 0x3F]);
        out.push_back(tbl[(triple >> 12) & 0x3F]);
        if (i <= len) out.push_back(tbl[(triple >> 6) & 0x3F]);
        if (i < len) out.push_back(tbl[triple & 0x3F]);
    }
    return out;
}

inline std::vector<uint8_t> base64url_decode(const std::string& in) {
    auto decodeChar = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        int v = decodeChar(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((buf >> bits) & 0xFF);
        }
    }
    return out;
}

class AuthSigner {
public:
    EVP_PKEY* pkey = nullptr;
    std::string x_b64;
    std::string y_b64;
    std::string d_b64;

    AuthSigner() {
        generateNew();
    }

    explicit AuthSigner(const std::string& keyStr) {
        if (!loadFromKey(keyStr)) {
            generateNew();
        }
    }

    ~AuthSigner() {
        if (pkey) EVP_PKEY_free(pkey);
    }

    void generateNew() {
        if (pkey) {
            EVP_PKEY_free(pkey);
            pkey = nullptr;
        }
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
        EVP_PKEY_keygen_init(pctx);
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1);
        EVP_PKEY_keygen(pctx, &pkey);
        EVP_PKEY_CTX_free(pctx);

        BIGNUM *pub_x = NULL, *pub_y = NULL, *priv_d = NULL;
        EVP_PKEY_get_bn_param(pkey, "qx", &pub_x);
        EVP_PKEY_get_bn_param(pkey, "qy", &pub_y);
        EVP_PKEY_get_bn_param(pkey, "priv", &priv_d);

        unsigned char bx[32] = {0}, by[32] = {0}, bd[32] = {0};
        BN_bn2binpad(pub_x, bx, 32);
        BN_bn2binpad(pub_y, by, 32);
        BN_bn2binpad(priv_d, bd, 32);

        x_b64 = base64url_encode(bx, 32);
        y_b64 = base64url_encode(by, 32);
        d_b64 = base64url_encode(bd, 32);

        BN_free(pub_x);
        BN_free(pub_y);
        BN_free(priv_d);
    }

    bool loadFromKey(const std::string& keyStr) {

        size_t p1 = keyStr.find('.');
        if (p1 == std::string::npos || keyStr.substr(0, p1) != "idkey") return false;
        size_t p2 = keyStr.find('.', p1 + 1);
        if (p2 == std::string::npos) return false;
        size_t p3 = keyStr.find('.', p2 + 1);
        if (p3 == std::string::npos) return false;

        std::string sx = keyStr.substr(p1 + 1, p2 - p1 - 1);
        std::string sy = keyStr.substr(p2 + 1, p3 - p2 - 1);
        std::string sd = keyStr.substr(p3 + 1);

        auto bx = base64url_decode(sx);
        auto by = base64url_decode(sy);
        auto bd = base64url_decode(sd);
        if (bx.size() != 32 || by.size() != 32 || bd.size() != 32) return false;

        BIGNUM* bn_x = BN_bin2bn(bx.data(), 32, NULL);
        BIGNUM* bn_y = BN_bin2bn(by.data(), 32, NULL);
        BIGNUM* bn_d = BN_bin2bn(bd.data(), 32, NULL);

        OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
        OSSL_PARAM_BLD_push_utf8_string(bld, "group", "prime256v1", 0);
        OSSL_PARAM_BLD_push_BN(bld, "qx", bn_x);
        OSSL_PARAM_BLD_push_BN(bld, "qy", bn_y);
        OSSL_PARAM_BLD_push_BN(bld, "priv", bn_d);
        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);

        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
        EVP_PKEY_fromdata_init(ctx);
        if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_KEYPAIR, params) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            OSSL_PARAM_BLD_free(bld);
            BN_free(bn_x); BN_free(bn_y); BN_free(bn_d);
            return false;
        }
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
        OSSL_PARAM_BLD_free(bld);
        BN_free(bn_x); BN_free(bn_y); BN_free(bn_d);

        x_b64 = sx;
        y_b64 = sy;
        d_b64 = sd;
        return true;
    }

    std::string getAuthKeyString() const {
        return "idkey." + x_b64 + "." + y_b64 + "." + d_b64;
    }

    std::vector<uint8_t> signChallenge(const uint8_t* challenge, size_t chal_len) {
        BufferWriter payload;
        payload.writeStr(x_b64);
        payload.writeStr(y_b64);
        payload.writeBytes(challenge, chal_len);

        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey);
        EVP_DigestSignUpdate(mdctx, payload.buf.data(), payload.buf.size());
        size_t siglen = 0;
        EVP_DigestSignFinal(mdctx, NULL, &siglen);
        std::vector<unsigned char> der_sig(siglen);
        EVP_DigestSignFinal(mdctx, der_sig.data(), &siglen);
        EVP_MD_CTX_free(mdctx);

        const unsigned char* p = der_sig.data();
        ECDSA_SIG* ec_sig = d2i_ECDSA_SIG(NULL, &p, siglen);
        const BIGNUM *r = NULL, *s = NULL;
        ECDSA_SIG_get0(ec_sig, &r, &s);
        unsigned char raw_sig[64] = {0};
        BN_bn2binpad(r, raw_sig, 32);
        BN_bn2binpad(s, raw_sig + 32, 32);
        ECDSA_SIG_free(ec_sig);

        BufferWriter out;
        out.writeU8(1);
        out.writeU16BE((uint16_t)payload.buf.size());
        out.writeBytes(payload.buf.data(), payload.buf.size());
        out.writeBytes(raw_sig, 64);
        return out.buf;
    }
};
