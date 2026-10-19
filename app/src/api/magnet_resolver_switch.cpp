// Switch-only: needs library/pipensx-bt's core/*.h, only vendored/built for
// PLATFORM_SWITCH (see library/CMakeLists.txt, CMakeLists.txt's
// USE_PIPENSX_BT block). Adapted from pipensx's src/app/magnet_resolver.cpp
// -- see the header for what changed and why.
#ifdef USE_PIPENSX_BT

#include "api/magnet_resolver_switch.hpp"

extern "C"
{
#include "bencode.h"
#include "dht.h"
#include "metainfo.h"
#include "mse.h"
#include "net.h"
#include "sha1.h"
#include "peer_handoff.h"
#include "tracker.h"
#include "util.h"
}

#include <borealis.hpp>
#include <borealis/core/assets.hpp>
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace magnet_resolver
{
namespace
{

// The ut_metadata id we advertise in our extension handshake. Peers address
// extended messages back to us using this id (BEP-10), regardless of the id
// the peer advertised for its own ut_metadata implementation.
constexpr uint8_t kLocalUtMetadataId = 1;
constexpr uint8_t kLocalUtPexId      = 2;

constexpr size_t kMetadataPieceSize = 16 * 1024;
constexpr size_t kMetadataLimit     = 8 * 1024 * 1024;
constexpr uint64_t kOverallTimeoutMs = 90 * 1000;
constexpr int kIoTimeoutMs           = 4000;
constexpr uint32_t kMaxPeersPerTracker = 64;
constexpr uint32_t kMaxMergedPeers     = 192;
constexpr uint32_t kMaxConcurrentPeers = 12;
constexpr uint32_t kRequestPipeline    = 8;
// When a sweep of the known peers yields no metadata but the overall deadline
// still has room, re-announce for a rotated peer set and try again -- a thin
// swarm (e.g. everyone already grabbed by another peer behind the same NAT)
// usually just needs another pass rather than a hard failure.
constexpr uint64_t kReannounceBackoffMs = 3000;
constexpr int kMaxEmptyReannounces      = 2;
// 60s, not 25s: the DHT engine only re-bootstraps after 30s with zero good
// nodes, so a shorter search window could never benefit from that. Still
// fits inside the 90s overall deadline.
constexpr uint64_t kDhtSearchTimeoutMs = 60 * 1000;
constexpr uint32_t kDhtTargetPeers     = 32;
constexpr int kDhtPollIntervalMs       = 250;

// PEX amplification: when the tracker/DHT phase yields a thin peer list, ask
// each of the few peers we have for their swarm view.
constexpr uint32_t kPexThinThreshold = 3;
constexpr int kPexPeerTimeoutMs      = 5000;
constexpr int kCancelablePollSliceMs = 100;

struct ResolveStop
{
    const std::atomic<bool>* user  = nullptr;
    const std::atomic<bool>* race  = nullptr;
    const std::atomic<bool>* local = nullptr;

    bool requested() const
    {
        return (user && user->load(std::memory_order_relaxed)) || (race && race->load(std::memory_order_relaxed)) ||
               (local && local->load(std::memory_order_relaxed));
    }
};

int trackerCancelled(void* user)
{
    return static_cast<ResolveStop*>(user)->requested() ? 1 : 0;
}

bool hexNibble(char c, uint8_t& value)
{
    if (c >= '0' && c <= '9')
        value = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f')
        value = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        value = static_cast<uint8_t>(c - 'A' + 10);
    else
        return false;
    return true;
}

bool parseInfoHash(const std::string& hex, uint8_t out[20])
{
    if (hex.size() != 40)
        return false;
    for (size_t i = 0; i < 20; ++i)
    {
        uint8_t hi = 0, lo = 0;
        if (!hexNibble(hex[i * 2], hi) || !hexNibble(hex[i * 2 + 1], lo))
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool containsCaseInsensitive(const char* text, const char* needle)
{
    if (!text || !needle || !*needle)
        return false;
    std::string haystack(text);
    std::string query(needle);
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(query) != std::string::npos;
}

bool containsPeer(const std::vector<uint8_t>& peers, const uint8_t* compact)
{
    for (size_t offset = 0; offset + 6 <= peers.size(); offset += 6)
        if (std::memcmp(peers.data() + offset, compact, 6) == 0)
            return true;
    return false;
}

void appendUniquePeers(std::vector<uint8_t>& peers, const uint8_t* compact, uint32_t count)
{
    for (uint32_t i = 0; i < count && peers.size() / 6 < kMaxMergedPeers; ++i)
    {
        const uint8_t* item = compact + i * 6;
        if (containsPeer(peers, item))
            continue;
        peers.insert(peers.end(), item, item + 6);
    }
}

bool waitFd(socket_t fd, short events, int timeoutMs, const ResolveStop* stop = nullptr)
{
    int remaining = timeoutMs;
    while (remaining > 0 && !(stop && stop->requested()))
    {
        pollfd item { fd, events, 0 };
        int slice = stop ? std::min(remaining, kCancelablePollSliceMs) : remaining;
        int result;
        do
        {
            result = poll(&item, 1, slice);
        } while (result < 0 && errno == EINTR);
        if (result > 0)
            return (item.revents & events) != 0;
        if (result < 0)
            return false;
        remaining -= slice;
    }
    return false;
}

// Compact IPv4 peers discovered by the short-lived DHT search that runs in
// parallel with the tracker announces.
struct DhtSearch
{
    std::mutex mutex;
    std::vector<uint8_t> peers;
};

uint32_t dhtPeerCount(DhtSearch& search)
{
    std::lock_guard<std::mutex> lock(search.mutex);
    return static_cast<uint32_t>(search.peers.size() / 6);
}

bool writeTorrentAtomic(const std::string& path, const std::vector<uint8_t>& torrent, std::string& error)
{
    std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Nao foi possivel criar o arquivo de torrent resolvido.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(torrent.data()), static_cast<std::streamsize>(torrent.size()));
        output.flush();
        if (!output.good())
        {
            unlink(temporary.c_str());
            error = "Nao foi possivel gravar o arquivo de torrent resolvido.";
            return false;
        }
    }
    if (rename(temporary.c_str(), path.c_str()) != 0)
    {
        unlink(temporary.c_str());
        error = "Nao foi possivel substituir o arquivo de torrent resolvido.";
        return false;
    }
    return true;
}

void runDhtSearch(const uint8_t infoHash[20], std::atomic<bool>& cancelled, std::atomic<bool>& stop,
    const std::atomic<bool>* race, DhtSearch& search)
{
    auto raced = [race] { return race && race->load(std::memory_order_relaxed); };
    if (cancelled || stop || raced())
        return;
    // Pure lookup (announce port 0) on the shared engine: when a download is
    // already running the resolver joins its warm routing table instead of
    // losing the old singleton race and falling back to trackers alone.
    dht_session_t* session = dht_attach(infoHash, 0);
    if (!session)
    {
        brls::Logger::warning("magnet_resolver: dht unavailable, resolving without it");
        return;
    }
    uint64_t deadline = now_ms() + kDhtSearchTimeoutMs;
    while (!cancelled && !stop && !raced() && now_ms() < deadline && dhtPeerCount(search) < kDhtTargetPeers)
    {
        uint8_t found[32][6];
        int count = dht_session_poll(session, found, 32);
        if (count > 0)
        {
            std::lock_guard<std::mutex> lock(search.mutex);
            appendUniquePeers(search.peers, &found[0][0], static_cast<uint32_t>(count));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kDhtPollIntervalMs));
        }
    }
    dht_detach(session);
}

bool sendAll(socket_t fd, const uint8_t* data, size_t size, const ResolveStop* stop = nullptr)
{
    size_t sent = 0;
    while (sent < size)
    {
        if (!waitFd(fd, POLLOUT, kIoTimeoutMs, stop))
            return false;
        ssize_t count = send(fd, data + sent, size - sent, 0);
        if (count <= 0)
            return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool recvAll(socket_t fd, uint8_t* data, size_t size, int timeoutMs, const ResolveStop* stop = nullptr)
{
    size_t received = 0;
    while (received < size)
    {
        if (!waitFd(fd, POLLIN, timeoutMs, stop))
            return false;
        ssize_t count = recv(fd, data + received, size - received, 0);
        if (count <= 0)
            return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

// A peer socket with an optional MSE/PE (RC4) encryption layer -- some peers
// require encryption and silently drop a plaintext BEP-3 handshake, so all
// post-handshake traffic must be able to ride through RC4. When `encrypted`
// is false this is a thin passthrough over the raw socket.
struct PeerWire
{
    socket_t fd    = INVALID_SOCK;
    bool encrypted = false;
    rc4_t send {}; // our send keystream (keyA)
    rc4_t recv {}; // our receive keystream (keyB)
    std::vector<uint8_t> backlog; // decrypted bytes read during the MSE
    size_t backlogPos = 0;        // handshake that belong to the peer stream
};

bool wireSendAll(PeerWire& wire, const uint8_t* data, size_t size, const ResolveStop* stop = nullptr)
{
    if (!wire.encrypted)
        return sendAll(wire.fd, data, size, stop);
    uint8_t chunk[4096];
    size_t offset = 0;
    while (offset < size)
    {
        size_t count = std::min(sizeof(chunk), size - offset);
        rc4_crypt(&wire.send, data + offset, chunk, count);
        if (!sendAll(wire.fd, chunk, count, stop))
            return false;
        offset += count;
    }
    return true;
}

bool wireRecvAll(PeerWire& wire, uint8_t* data, size_t size, int timeoutMs, const ResolveStop* stop = nullptr)
{
    size_t got = 0;
    if (wire.encrypted && wire.backlogPos < wire.backlog.size())
    {
        size_t take = std::min(wire.backlog.size() - wire.backlogPos, size);
        std::memcpy(data, wire.backlog.data() + wire.backlogPos, take);
        wire.backlogPos += take;
        got += take;
    }
    if (got == size)
        return true;
    if (!recvAll(wire.fd, data + got, size - got, timeoutMs, stop))
        return false;
    if (wire.encrypted)
        rc4_crypt(&wire.recv, data + got, data + got, size - got);
    return true;
}

bool sendFrame(PeerWire& wire, const std::vector<uint8_t>& payload, const ResolveStop* stop = nullptr)
{
    uint32_t size = static_cast<uint32_t>(payload.size());
    uint8_t header[4] = {
        static_cast<uint8_t>(size >> 24),
        static_cast<uint8_t>(size >> 16),
        static_cast<uint8_t>(size >> 8),
        static_cast<uint8_t>(size),
    };
    return wireSendAll(wire, header, sizeof(header), stop) && wireSendAll(wire, payload.data(), payload.size(), stop);
}

void appendCapturedFrame(std::vector<uint8_t>* extra, const std::vector<uint8_t>& payload)
{
    if (!extra)
        return;
    constexpr size_t kMaxCaptured = 64 * 1024;
    if (extra->size() + 4 + payload.size() > kMaxCaptured)
        return;
    uint32_t size = static_cast<uint32_t>(payload.size());
    extra->push_back(static_cast<uint8_t>(size >> 24));
    extra->push_back(static_cast<uint8_t>(size >> 16));
    extra->push_back(static_cast<uint8_t>(size >> 8));
    extra->push_back(static_cast<uint8_t>(size));
    extra->insert(extra->end(), payload.begin(), payload.end());
}

void stashMagnetPeer(PeerWire& wire, const uint8_t infoHash[20], const uint8_t* compact, const std::vector<uint8_t>* extra)
{
    if (wire.fd == INVALID_SOCK || !compact)
        return;
    // Ask for unchoke during the picker gap so adopt may already be fed.
    static const uint8_t interested[] = { 0, 0, 0, 1, 2 };
    wireSendAll(wire, interested, sizeof(interested));

    std::vector<uint8_t> pending;
    if (extra && !extra->empty())
        pending = *extra;
    if (wire.backlogPos < wire.backlog.size())
        pending.insert(pending.end(), wire.backlog.begin() + static_cast<std::ptrdiff_t>(wire.backlogPos), wire.backlog.end());

    torrent_peer_handoff_t handoff {};
    handoff.fd = wire.fd;
    handoff.addr.sin_family = AF_INET;
    std::memcpy(&handoff.addr.sin_addr.s_addr, compact, 4);
    std::memcpy(&handoff.addr.sin_port, compact + 4, 2);
    handoff.mse_active = wire.encrypted ? 1 : 0;
    handoff.send_rc4   = wire.send;
    handoff.recv_rc4   = wire.recv;
    handoff.pending     = pending.empty() ? nullptr : pending.data();
    handoff.pending_len = static_cast<uint32_t>(pending.size());
    torrent_stash_peer(infoHash, &handoff);
    wire.fd = INVALID_SOCK;
}

struct MagnetWireGuard
{
    PeerWire* wire            = nullptr;
    const uint8_t* infoHash   = nullptr;
    const uint8_t* compact    = nullptr;
    std::vector<uint8_t>* extra = nullptr;
    bool handshake             = false;

    ~MagnetWireGuard() { finish(); }

    void finish()
    {
        if (!wire || wire->fd == INVALID_SOCK)
            return;
        if (handshake && infoHash && compact)
            stashMagnetPeer(*wire, infoHash, compact, extra);
        else
            net_close(wire->fd);
        wire->fd = INVALID_SOCK;
    }
};

bool recvFrame(PeerWire& wire, std::vector<uint8_t>& payload, const ResolveStop* stop = nullptr)
{
    uint8_t header[4];
    if (!wireRecvAll(wire, header, sizeof(header), kIoTimeoutMs, stop))
        return false;
    uint32_t size = (static_cast<uint32_t>(header[0]) << 24) | (static_cast<uint32_t>(header[1]) << 16) |
                    (static_cast<uint32_t>(header[2]) << 8) | static_cast<uint32_t>(header[3]);
    if (size > kMetadataPieceSize + 4096)
        return false;
    payload.resize(size);
    return size == 0 || wireRecvAll(wire, payload.data(), size, kIoTimeoutMs, stop);
}

// What one peer attempt achieved, and where it died.
struct PeerAttempt
{
    bool connected         = false; // TCP session established
    bool handshakeVerified = false; // BT handshake for our info-hash completed
    const char* failure    = "";    // stage the attempt died at
    int error              = 0;     // errno there, 0 when there is none to give
};

socket_t tcpConnect(const uint8_t* compact, PeerAttempt& attempt, const ResolveStop* stop = nullptr)
{
    sockaddr_in address {};
    address.sin_family = AF_INET;
    std::memcpy(&address.sin_addr.s_addr, compact, 4);
    std::memcpy(&address.sin_port, compact + 4, 2);
    socket_t fd = net_tcp_connect(&address);
    if (fd == INVALID_SOCK)
    {
        attempt.failure = "connect";
        attempt.error   = errno;
        return INVALID_SOCK;
    }
    if (!waitFd(fd, POLLOUT, kIoTimeoutMs, stop))
    {
        net_close(fd);
        attempt.failure = "connect timed out";
        attempt.error   = 0;
        return INVALID_SOCK;
    }
    int socketError    = 0;
    socklen_t errorSize = sizeof(socketError);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorSize) != 0 || socketError != 0)
    {
        net_close(fd);
        attempt.failure = "connect refused";
        attempt.error   = socketError;
        return INVALID_SOCK;
    }
    return fd;
}

void buildBtHandshake(uint8_t hs[68], const uint8_t infoHash[20], const uint8_t peerId[20])
{
    std::memset(hs, 0, 68);
    hs[0] = 19;
    std::memcpy(hs + 1, "BitTorrent protocol", 19);
    hs[25] = 0x10; // extension protocol (BEP-10)
    std::memcpy(hs + 28, infoHash, 20);
    std::memcpy(hs + 48, peerId, 20);
}

bool readBtHandshakeReply(PeerWire& wire, const uint8_t infoHash[20], const ResolveStop* stop = nullptr)
{
    uint8_t response[68];
    return wireRecvAll(wire, response, sizeof(response), kIoTimeoutMs, stop) && response[0] == 19 &&
           std::memcmp(response + 1, "BitTorrent protocol", 19) == 0 && std::memcmp(response + 28, infoHash, 20) == 0 &&
           (response[25] & 0x10) != 0;
}

// Drive the MSE initiator handshake on a freshly connected socket. The BT
// handshake rides inside the encrypted request as the IA payload, so on
// success the peer's (encrypted) BT handshake reply is left for
// readBtHandshakeReply.
bool mseHandshake(socket_t fd, const uint8_t infoHash[20], const uint8_t peerId[20], PeerWire& wire, const ResolveStop* stop = nullptr)
{
    mse_client_t client;
    uint8_t priv[MSE_DH_LEN];
    mse_dh_private(priv);
    uint8_t ia[68];
    buildBtHandshake(ia, infoHash, peerId);
    uint8_t out[512];
    size_t produced = 0;
    if (mse_client_start(&client, infoHash, priv, nullptr, 0, ia, sizeof(ia), out, sizeof(out), &produced) != MSE_CONTINUE)
        return false;
    if (!sendAll(fd, out, produced, stop))
        return false;

    std::vector<uint8_t> inbuf;
    uint8_t tmp[1024];
    const uint64_t deadline = now_ms() + 2 * kIoTimeoutMs;
    while (now_ms() < deadline && !(stop && stop->requested()))
    {
        if (!waitFd(fd, POLLIN, kIoTimeoutMs, stop))
            return false;
        ssize_t count = recv(fd, tmp, sizeof(tmp), 0);
        if (count <= 0)
            return false;
        inbuf.insert(inbuf.end(), tmp, tmp + count);

        size_t consumed = 0;
        produced         = 0;
        mse_status_t status = mse_client_feed(&client, inbuf.data(), inbuf.size(), &consumed, out, sizeof(out), &produced);
        if (produced && !sendAll(fd, out, produced, stop))
            return false;
        inbuf.erase(inbuf.begin(), inbuf.begin() + static_cast<ptrdiff_t>(consumed));
        if (status == MSE_FAIL)
            return false;
        if (status == MSE_DONE)
        {
            wire.fd        = fd;
            wire.encrypted = true;
            wire.send      = client.send_rc4;
            wire.recv      = client.recv_rc4;
            // Bytes past the handshake are the peer's encrypted stream;
            // decrypt them now so wireRecvAll can serve them as plaintext.
            if (!inbuf.empty())
            {
                wire.backlog.resize(inbuf.size());
                rc4_crypt(&wire.recv, inbuf.data(), wire.backlog.data(), inbuf.size());
            }
            return true;
        }
    }
    return false;
}

// Connect, then complete the BT handshake. Plaintext is tried first; if the
// peer drops it (encryption-only), reconnect and retry over MSE/PE.
bool connectPeer(const uint8_t* compact, const uint8_t infoHash[20], const uint8_t peerId[20], PeerWire& wire, PeerAttempt& attempt,
    const ResolveStop* stop = nullptr)
{
    socket_t fd = tcpConnect(compact, attempt, stop);
    if (fd == INVALID_SOCK)
        return false;
    attempt.connected = true;

    uint8_t handshake[68];
    buildBtHandshake(handshake, infoHash, peerId);
    if (sendAll(fd, handshake, sizeof(handshake), stop))
    {
        wire.fd        = fd;
        wire.encrypted = false;
        if (readBtHandshakeReply(wire, infoHash, stop))
        {
            net_set_tcp_receive_buffer(fd);
            return true;
        }
    }
    // Plaintext refused or dropped -- retry the peer with MSE/PE encryption.
    net_close(fd);
    wire = PeerWire {};

    fd = tcpConnect(compact, attempt, stop);
    if (fd == INVALID_SOCK)
        return false;
    if (!mseHandshake(fd, infoHash, peerId, wire, stop) || !readBtHandshakeReply(wire, infoHash, stop))
    {
        net_close(fd);
        wire            = PeerWire {};
        attempt.failure = "plaintext and MSE handshake";
        attempt.error   = 0;
        return false;
    }
    net_set_tcp_receive_buffer(fd);
    return true;
}

bool negotiateMetadata(PeerWire& wire, uint8_t& peerExtension, size_t& metadataSize, std::vector<uint8_t>* extra = nullptr,
    const ResolveStop* stop = nullptr)
{
    static const char handshake[] = "d1:md11:ut_metadatai1eee";
    std::vector<uint8_t> request { 20, 0 };
    request.insert(request.end(), handshake, handshake + sizeof(handshake) - 1);
    if (!sendFrame(wire, request, stop))
        return false;

    for (int message = 0; message < 32; ++message)
    {
        std::vector<uint8_t> frame;
        if (!recvFrame(wire, frame, stop))
            return false;
        if (frame.size() < 3 || frame[0] != 20 || frame[1] != 0)
        {
            appendCapturedFrame(extra, frame);
            continue;
        }
        const char* begin = reinterpret_cast<const char*>(frame.data() + 2);
        const char* end   = reinterpret_cast<const char*>(frame.data() + frame.size());
        be_node_t root;
        const char* cursor = begin;
        if (!be_decode(&cursor, end, &root) || root.type != BE_DICT)
            return false;
        be_node_t map;
        be_node_t extension;
        be_node_t size;
        if (!be_dict_get(root.buf, root.buf + root.raw_len, "m", 1, &map) || map.type != BE_DICT ||
            !be_dict_get(map.buf, map.buf + map.raw_len, "ut_metadata", 11, &extension) || extension.type != BE_INT ||
            extension.ival <= 0 || extension.ival > 255 ||
            !be_dict_get(root.buf, root.buf + root.raw_len, "metadata_size", 13, &size) || size.type != BE_INT ||
            size.ival <= 0 || size.ival > static_cast<int64_t>(kMetadataLimit))
            return false;
        peerExtension = static_cast<uint8_t>(extension.ival);
        metadataSize  = static_cast<size_t>(size.ival);
        return true;
    }
    return false;
}

bool sendMetadataRequest(PeerWire& wire, uint8_t extension, uint32_t piece, const ResolveStop* stop = nullptr)
{
    std::string body = "d8:msg_typei0e5:piecei" + std::to_string(piece) + "ee";
    std::vector<uint8_t> frame { 20, extension };
    frame.insert(frame.end(), body.begin(), body.end());
    return sendFrame(wire, frame, stop);
}

bool receiveMetadataPiece(PeerWire& wire, uint32_t& piece, const uint8_t*& data, size_t& dataSize, std::vector<uint8_t>& frame,
    std::vector<uint8_t>* extra = nullptr, const ResolveStop* stop = nullptr)
{
    for (int message = 0; message < 32; ++message)
    {
        if (!recvFrame(wire, frame, stop))
            return false;
        // Extended messages the peer sends back to us carry our advertised
        // ut_metadata id, not the peer's (BEP-10).
        if (frame.size() < 3 || frame[0] != 20 || frame[1] != kLocalUtMetadataId)
        {
            appendCapturedFrame(extra, frame);
            continue;
        }
        const char* begin = reinterpret_cast<const char*>(frame.data() + 2);
        const char* end   = reinterpret_cast<const char*>(frame.data() + frame.size());
        const char* cursor = begin;
        be_node_t header;
        if (!be_decode(&cursor, end, &header) || header.type != BE_DICT)
            return false;
        be_node_t type;
        be_node_t index;
        if (!be_dict_get(header.buf, header.buf + header.raw_len, "msg_type", 8, &type) ||
            !be_dict_get(header.buf, header.buf + header.raw_len, "piece", 5, &index) || type.type != BE_INT ||
            index.type != BE_INT || index.ival < 0)
            return false;
        if (type.ival == 2) // reject
            return false;
        if (type.ival != 1) // not a data message
            continue;
        piece    = static_cast<uint32_t>(index.ival);
        data     = reinterpret_cast<const uint8_t*>(cursor);
        dataSize = static_cast<size_t>(end - cursor);
        return true;
    }
    return false;
}

struct FetchContext
{
    const std::vector<uint8_t>* peers;
    const uint8_t* infoHash;
    const uint8_t* peerId;
    uint64_t deadline;
    std::atomic<bool>* cancelled;
    std::atomic<bool>* stopWorkers;
    const ProgressCallback* progress;
};

bool fetchMetadataFromPeer(const FetchContext& ctx, uint32_t peerIndex, uint32_t peerCount, std::atomic<bool>& peerAbort,
    std::vector<uint8_t>& metadata, PeerAttempt& attempt)
{
    attempt = PeerAttempt {};
    ResolveStop stop { ctx.cancelled, ctx.stopWorkers, &peerAbort };
    if (stop.requested() || now_ms() >= ctx.deadline)
        return false;
    if (*ctx.progress)
        (*ctx.progress)({ Progress::Stage::Connecting, 0, 0, peerIndex + 1, peerCount });

    const uint8_t* compact = ctx.peers->data() + peerIndex * 6;
    PeerWire wire;
    std::vector<uint8_t> extraFrames;
    MagnetWireGuard guard;
    guard.wire     = &wire;
    guard.infoHash = ctx.infoHash;
    guard.compact  = compact;
    guard.extra    = &extraFrames;
    if (!connectPeer(compact, ctx.infoHash, ctx.peerId, wire, attempt, &stop))
        return false;
    attempt.handshakeVerified = true;
    guard.handshake            = true;

    uint8_t extension  = 0;
    size_t metadataSize = 0;
    if (!negotiateMetadata(wire, extension, metadataSize, &extraFrames, &stop))
        return false;

    std::vector<uint8_t> local(metadataSize);
    std::vector<uint8_t> received((metadataSize + kMetadataPieceSize - 1) / kMetadataPieceSize, 0);
    std::vector<uint8_t> requested(received.size(), 0);
    uint32_t completed = 0;
    uint32_t inFlight   = 0;

    while (!stop.requested() && now_ms() < ctx.deadline && completed < received.size())
    {
        for (uint32_t piece = 0; piece < received.size() && inFlight < kRequestPipeline; ++piece)
        {
            if (received[piece] || requested[piece])
                continue;
            if (!sendMetadataRequest(wire, extension, piece, &stop))
                return false;
            requested[piece] = 1;
            ++inFlight;
        }

        uint32_t piece      = 0;
        const uint8_t* bytes = nullptr;
        size_t byteCount     = 0;
        std::vector<uint8_t> frame;
        if (!receiveMetadataPiece(wire, piece, bytes, byteCount, frame, &extraFrames, &stop))
            return false;
        if (piece >= received.size())
            continue;
        if (requested[piece] && inFlight)
            --inFlight;
        requested[piece] = 0;
        size_t offset    = static_cast<size_t>(piece) * kMetadataPieceSize;
        size_t expected  = std::min(kMetadataPieceSize, metadataSize - offset);
        if (byteCount != expected)
            return false;
        if (!received[piece])
        {
            std::memcpy(local.data() + offset, bytes, byteCount);
            received[piece] = 1;
            ++completed;
            if (*ctx.progress)
                (*ctx.progress)({ Progress::Stage::FetchingMetadata, completed, static_cast<uint32_t>(received.size()),
                    peerIndex + 1, peerCount });
        }
    }

    if (completed != received.size())
        return false;
    metadata = std::move(local);
    return true;
}

bool sendPexHandshake(PeerWire& wire, const ResolveStop* stop = nullptr)
{
    static const char handshake[] = "d1:md11:ut_metadatai1e6:ut_pexi2eee";
    std::vector<uint8_t> request { 20, 0 };
    request.insert(request.end(), handshake, handshake + sizeof(handshake) - 1);
    return sendFrame(wire, request, stop);
}

// Connect to one thin-list peer, advertise ut_pex, and harvest the compact
// peers it pushes in any ut_pex "added" field. Best effort: peers emit PEX
// on their own cadence, so an empty result is normal. Merges into `out`
// (never the live `peers` vector -- appending there while its data() is
// being iterated would dangle).
void harvestPexFromPeer(const uint8_t* compact, const uint8_t infoHash[20], const uint8_t peerId[20], std::atomic<bool>& cancelled,
    std::atomic<bool>& stopWorkers, std::vector<uint8_t>& out)
{
    ResolveStop stop { &cancelled, &stopWorkers };
    PeerWire wire;
    std::vector<uint8_t> extraFrames;
    MagnetWireGuard guard;
    guard.wire     = &wire;
    guard.infoHash = infoHash;
    guard.compact  = compact;
    guard.extra    = &extraFrames;
    PeerAttempt attempt;
    if (!connectPeer(compact, infoHash, peerId, wire, attempt, &stop))
        return;
    guard.handshake = true;
    if (!sendPexHandshake(wire, &stop))
        return;
    uint64_t deadline = now_ms() + kPexPeerTimeoutMs;
    while (!stop.requested() && now_ms() < deadline)
    {
        std::vector<uint8_t> frame;
        if (!recvFrame(wire, frame, &stop))
            break;
        if (frame.size() < 3 || frame[0] != 20 || frame[1] != kLocalUtPexId)
        {
            appendCapturedFrame(&extraFrames, frame);
            continue;
        }
        const char* begin = reinterpret_cast<const char*>(frame.data() + 2);
        const char* end   = reinterpret_cast<const char*>(frame.data() + frame.size());
        be_node_t root;
        const char* cursor = begin;
        if (!be_decode(&cursor, end, &root) || root.type != BE_DICT)
            break;
        be_node_t added;
        if (be_dict_get(root.buf, root.buf + root.raw_len, "added", 5, &added) && added.type == BE_STR && added.slen >= 6)
            appendUniquePeers(out, reinterpret_cast<const uint8_t*>(added.sval), static_cast<uint32_t>(added.slen / 6));
    }
}

std::string bencodeString(const std::string& value)
{
    return std::to_string(value.size()) + ":" + value;
}

// Validates `info` against `infoHash` and wraps it into a full, parseable
// .torrent (announce + announce-list of every given tracker + the info
// dict). trackers is whatever the caller passed to resolveToFile (already
// deduplicated by the caller, but deduped again here for safety).
bool buildTorrent(const uint8_t infoHash[20], const std::vector<std::string>& trackers, const std::vector<uint8_t>& info,
    std::vector<uint8_t>& torrent, std::string& error)
{
    if (info.empty() || info.size() > kMetadataLimit)
    {
        error = "Metadados do peer vazios ou grandes demais.";
        return false;
    }
    const char* cursor = reinterpret_cast<const char*>(info.data());
    const char* end     = cursor + info.size();
    be_node_t root;
    if (!be_decode(&cursor, end, &root) || root.type != BE_DICT || cursor != end)
    {
        error = "Metadados do peer nao sao um dicionario 'info' valido.";
        return false;
    }
    uint8_t digest[20];
    sha1(info.data(), info.size(), digest);
    if (std::memcmp(digest, infoHash, 20) != 0)
    {
        error = "Metadados do peer nao correspondem ao info hash do magnet.";
        return false;
    }

    std::vector<std::string> uniqueTrackers;
    for (const auto& t : trackers)
        if (!t.empty() && std::find(uniqueTrackers.begin(), uniqueTrackers.end(), t) == uniqueTrackers.end())
            uniqueTrackers.push_back(t);

    std::string announceList = "13:announce-listl";
    for (const std::string& tracker : uniqueTrackers)
        announceList += "l" + bencodeString(tracker) + "e";
    announceList += "e";
    std::string primaryTracker = uniqueTrackers.empty() ? "" : uniqueTrackers.front();
    std::string prefix = "d8:announce" + bencodeString(primaryTracker) + announceList + "4:info";
    torrent.assign(prefix.begin(), prefix.end());
    torrent.insert(torrent.end(), info.begin(), info.end());
    torrent.push_back('e');

    metainfo_t parsed;
    if (!metainfo_parse(torrent.data(), torrent.size(), &parsed))
    {
        error = "Metadados resolvidos nao formam um torrent seguro suportado.";
        return false;
    }
    bool hashMatches = std::memcmp(parsed.info_hash, infoHash, 20) == 0;
    metainfo_free(&parsed);
    if (!hashMatches)
    {
        error = "Torrent gerado falhou na validacao de info hash.";
        return false;
    }
    return true;
}

} // namespace

// The tracker/DHT/peer (ut_metadata) half of resolveToFile()'s race. `race`
// is raised when the HTTPS cache wins, so this stops promptly instead of
// running out its own 90s deadline.
static bool resolveFromSwarm(const std::string& infoHashHex, const std::vector<std::string>& trackersIn,
    const std::string& path, std::atomic<bool>& cancelled, const std::atomic<bool>* race, const ProgressCallback& progress,
    std::string& error, std::vector<uint8_t>* verifiedPeers)
{
    if (verifiedPeers)
        verifiedPeers->clear();

    uint8_t infoHash[20];
    if (!parseInfoHash(infoHashHex, infoHash))
    {
        error = "Info hash invalido.";
        return false;
    }

    std::vector<std::string> trackers;
    for (const auto& t : trackersIn)
        if (!t.empty() && std::find(trackers.begin(), trackers.end(), t) == trackers.end())
            trackers.push_back(t);

    ResolveStop raceStop { &cancelled, race };

    uint8_t peerId[20];
    std::memcpy(peerId, "-SX0001-", 8);
    rand_bytes(peerId + 8, 12);

    if (progress)
        progress({ Progress::Stage::FindingPeers });

    DhtSearch dhtSearch;
    std::atomic<bool> dhtStop { false };
    // A bare std::thread's root function must never let an exception escape
    // -- on the Switch that's an instant, silent std::terminate/abort of the
    // whole app (no dialog, no log), not something an enclosing try/catch on
    // a different thread could ever catch. Treated as "no DHT peers this
    // round" rather than fatal: trackers alone can still resolve the magnet.
    std::thread dhtThread([&infoHash, &cancelled, &dhtStop, race, &dhtSearch] {
        try
        {
            runDhtSearch(infoHash, cancelled, dhtStop, race, dhtSearch);
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("magnet_resolver: dht search threw: {}", e.what());
        }
        catch (...)
        {
            brls::Logger::error("magnet_resolver: dht search threw a non-std exception");
        }
    });
    // The search outlives the tracker phase, so every exit path has to stop
    // and join it.
    struct DhtJoin
    {
        std::atomic<bool>& stop;
        std::thread& thread;
        ~DhtJoin()
        {
            stop.store(true);
            if (thread.joinable())
                thread.join();
        }
    } dhtJoin { dhtStop, dhtThread };

    std::vector<uint8_t> peers;
    std::string firstFailure;
    bool sawTrackerFailure = false;
    bool sawNotRegistered  = false;
    for (const std::string& tracker : trackers)
    {
        if (raceStop.requested())
            break;
        uint8_t batch[kMaxPeersPerTracker * 6];
        tracker_announce_result_t result;
        uint32_t count = tracker_announce_url_ex_cancel(
            tracker.c_str(), infoHash, peerId, 6881, 0, 0, batch, kMaxPeersPerTracker, &result, trackerCancelled, &raceStop);
        if (raceStop.requested())
            break;
        if (result.tracker_failure)
        {
            sawTrackerFailure = true;
            if (firstFailure.empty())
                firstFailure = result.failure_reason;
            if (containsCaseInsensitive(result.failure_reason, "not registered"))
                sawNotRegistered = true;
            brls::Logger::warning("magnet_resolver: tracker {} rejected hash: {}", tracker, result.failure_reason);
            if (sawNotRegistered)
                break;
        }
        appendUniquePeers(peers, batch, count);
        if (peers.size() / 6 >= kMaxMergedPeers)
            break;
    }

    // Merge whatever the DHT search has turned up so far. Only ever called
    // with every metadata worker joined: it grows `peers`, and a live worker
    // holds peers.data().
    auto mergeDht = [&]() -> uint32_t {
        std::lock_guard<std::mutex> lock(dhtSearch.mutex);
        uint32_t before = static_cast<uint32_t>(peers.size() / 6);
        if (!dhtSearch.peers.empty())
            appendUniquePeers(peers, dhtSearch.peers.data(), static_cast<uint32_t>(dhtSearch.peers.size() / 6));
        return static_cast<uint32_t>(peers.size() / 6) - before;
    };

    // "Not registered" is authoritative -- nothing left to look for. Peers,
    // on the other hand, are not an answer: a tracker's whole peer list can
    // be unreachable, and then the DHT is the only other source. So the
    // search keeps running alongside the metadata workers and every retry
    // round merges what it found.
    if (sawNotRegistered || raceStop.requested())
        dhtStop.store(true);
    if (peers.empty() || sawNotRegistered)
        dhtThread.join();
    mergeDht();
    uint32_t peerCount = static_cast<uint32_t>(peers.size() / 6);
    if (!peerCount)
    {
        if (sawNotRegistered)
            error = "O tracker diz que este torrent nao esta mais registrado.";
        else if (sawTrackerFailure && !firstFailure.empty())
            error = "Tracker rejeitou este torrent: " + firstFailure;
        else
            error = "Trackers e DHT nao retornaram nenhum peer utilizavel.";
        return false;
    }

    // PEX amplification: a blocked-tracker/DHT-only resolve can surface just
    // 1-3 peers. Ask those few for their swarm view before committing the
    // metadata-fetch workers.
    std::atomic<bool> stopWorkers { false };
    if (peerCount <= kPexThinThreshold && !raceStop.requested())
    {
        std::vector<uint8_t> pexPeers;
        for (uint32_t i = 0; i < peerCount && !raceStop.requested(); ++i)
            harvestPexFromPeer(peers.data() + i * 6, infoHash, peerId, cancelled, stopWorkers, pexPeers);
        if (!pexPeers.empty())
            appendUniquePeers(peers, pexPeers.data(), static_cast<uint32_t>(pexPeers.size() / 6));
        peerCount = static_cast<uint32_t>(peers.size() / 6);
    }

    uint64_t deadline = now_ms() + kOverallTimeoutMs;
    std::mutex mutex;
    uint32_t nextPeer = 0;
    bool resolved      = false;
    std::atomic<bool> peerAbort { false };
    std::atomic<uint32_t> reachedPeers { 0 }; // peers we got a TCP session to
    // Peers that actively refused the SYN (RST) -- proves our packets
    // reached a live host, so the peer list is stale, not the network
    // blocking us.
    std::atomic<uint32_t> refusedPeers { 0 };
    std::vector<uint8_t> metadata;
    std::vector<uint8_t> verifiedEndpoints;
    std::string unsafeMetadata;

    // Re-announce every tracker for a fresh (rotated) peer set and merge any
    // newcomers into `peers`. Only ever called between worker rounds.
    auto reannounce = [&]() -> uint32_t {
        uint32_t before = static_cast<uint32_t>(peers.size() / 6);
        for (const std::string& tracker : trackers)
        {
            if (raceStop.requested() || now_ms() >= deadline || peers.size() / 6 >= kMaxMergedPeers)
                break;
            uint8_t batch[kMaxPeersPerTracker * 6];
            tracker_announce_result_t result;
            uint32_t count = tracker_announce_url_ex_cancel(
                tracker.c_str(), infoHash, peerId, 6881, 0, 0, batch, kMaxPeersPerTracker, &result, trackerCancelled, &raceStop);
            if (raceStop.requested())
                break;
            appendUniquePeers(peers, batch, count);
        }
        return static_cast<uint32_t>(peers.size() / 6) - before;
    };

    int emptyReannounces = 0;
    while (!raceStop.requested() && !resolved && unsafeMetadata.empty() && now_ms() < deadline)
    {
        uint32_t roundEnd = peerCount;
        if (nextPeer >= roundEnd)
        {
            // Every known peer was tried once without metadata -- back off
            // briefly, pull a rotated peer set plus whatever the DHT has
            // found meanwhile, and keep trying until the deadline.
            uint64_t backoffUntil = now_ms() + kReannounceBackoffMs;
            while (!raceStop.requested() && now_ms() < backoffUntil && now_ms() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (raceStop.requested() || now_ms() >= deadline)
                break;
            uint32_t added = mergeDht();
            if (emptyReannounces < kMaxEmptyReannounces)
                added += reannounce();
            peerCount = static_cast<uint32_t>(peers.size() / 6);
            if (added)
            {
                emptyReannounces = 0;
            }
            else
            {
                // Nothing new anywhere -- sweep the ones we know again
                // instead of giving up: a peer that dropped our SYN or was
                // mid-disconnect can answer the next time around.
                ++emptyReannounces;
                nextPeer = 0;
            }
            continue;
        }

        std::vector<std::thread> workers;
        uint32_t pending    = roundEnd - nextPeer;
        uint32_t workerCount = std::min(pending, kMaxConcurrentPeers);
        FetchContext fetchCtx { &peers, infoHash, peerId, deadline, &cancelled, &stopWorkers, &progress };
        // Up front, so emplace_back below can't throw std::bad_alloc from a
        // reallocation while some workers are already running.
        workers.reserve(workerCount);
        // Every worker already started must be joined no matter how this
        // scope is left: a std::vector<std::thread> destroyed with a
        // joinable thread in it calls std::terminate -- which is exactly
        // what used to happen (a silent kill of the whole app, "terminate
        // called with no live exception") when the Switch's per-process
        // thread limit refused one of these mid-loop.
        struct JoinAll
        {
            std::vector<std::thread>& threads;
            ~JoinAll()
            {
                for (std::thread& t : threads)
                    if (t.joinable())
                        t.join();
            }
        } joinAll { workers };
        for (uint32_t worker = 0; worker < workerCount; ++worker)
        {
            // Starting a thread can fail (std::system_error) once the
            // process is near its thread limit -- mpv, curl and borealis
            // already hold a good share of it. Fewer concurrent peers is a
            // slower resolve, not a failure: carry on with however many
            // started.
            try
            {
            // A bare std::thread's root function must never let an exception
            // escape (instant, silent std::terminate/abort of the whole app
            // on the Switch, unrecoverable by any try/catch on another
            // thread) -- the outer try wraps every iteration so one peer's
            // failure (e.g. std::bad_alloc sizing `candidate` off a
            // malicious/corrupt metadata_size a peer advertised) costs just
            // that attempt, same as a plain `false` return already would.
            workers.emplace_back([&, roundEnd] {
                while (!raceStop.requested() && !peerAbort.load() && now_ms() < deadline)
                {
                    try
                    {
                        uint32_t peerIndex = 0;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (resolved || nextPeer >= roundEnd)
                                return;
                            peerIndex = nextPeer++;
                        }

                        std::vector<uint8_t> candidate;
                        PeerAttempt attempt;
                        bool fetched = fetchMetadataFromPeer(fetchCtx, peerIndex, peerCount, peerAbort, candidate, attempt);
                        if (attempt.connected)
                            reachedPeers.fetch_add(1);
                        else if (attempt.failure && std::strcmp(attempt.failure, "connect refused") == 0)
                            refusedPeers.fetch_add(1);
                        if (attempt.handshakeVerified)
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            appendUniquePeers(verifiedEndpoints, peers.data() + peerIndex * 6, 1);
                        }
                        if (!fetched)
                            continue;

                        std::vector<uint8_t> torrentProbe;
                        std::string probeError;
                        if (!buildTorrent(infoHash, trackers, candidate, torrentProbe, probeError))
                        {
                            // Same info-hash -> same dictionary. A parse/safety
                            // reject will never succeed from another peer.
                            if (probeError == "Metadados resolvidos nao formam um torrent seguro suportado.")
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                if (unsafeMetadata.empty())
                                    unsafeMetadata = probeError;
                                peerAbort.store(true);
                            }
                            continue;
                        }

                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (!resolved)
                            {
                                metadata = std::move(candidate);
                                resolved = true;
                                stopWorkers.store(true);
                            }
                        }
                        return;
                    }
                    catch (const std::exception& e)
                    {
                        brls::Logger::error("magnet_resolver: peer worker threw: {}", e.what());
                    }
                    catch (...)
                    {
                        brls::Logger::error("magnet_resolver: peer worker threw a non-std exception");
                    }
                }
            });
            }
            catch (const std::exception& e)
            {
                brls::Logger::warning("magnet_resolver: started only {} of {} peer workers: {}", workers.size(), workerCount, e.what());
                break;
            }
        }
        if (workers.empty())
        {
            // Not even one thread -- wait a moment for others to exit
            // instead of spinning through rounds; the deadline still bounds
            // the whole resolve.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        for (std::thread& worker : workers)
            worker.join();
    }

    if (cancelled && !resolved)
    {
        error = "Resolucao de metadados cancelada.";
        return false;
    }
    if (metadata.empty())
    {
        if (!unsafeMetadata.empty())
        {
            error = unsafeMetadata;
            return false;
        }
        if (!reachedPeers.load())
        {
            if (refusedPeers.load() > 0)
                error = "Encontrados " + std::to_string(peerCount) + " peers, mas nenhum aceitou conexao (" +
                        std::to_string(refusedPeers.load()) + " recusaram). O swarm parece morto agora.";
            else
                error = "Encontrados " + std::to_string(peerCount) +
                        " peers, mas nao foi possivel conectar a nenhum. A rede pode estar bloqueando BitTorrent.";
        }
        else
        {
            error = "Peers encontrados, mas nenhum retornou metadados do torrent.";
        }
        return false;
    }

    if (progress)
        progress({ Progress::Stage::Validating, 1, 1 });
    std::vector<uint8_t> torrent;
    if (!buildTorrent(infoHash, trackers, metadata, torrent, error))
        return false;
    if (!writeTorrentAtomic(path, torrent, error))
        return false;
    if (verifiedPeers)
        *verifiedPeers = std::move(verifiedEndpoints);
    return true;
}

namespace
{

size_t curlAppend(char* data, size_t size, size_t count, void* user)
{
    auto* body = static_cast<std::vector<uint8_t>*>(user);
    body->insert(body->end(), data, data + size * count);
    return size * count;
}

int curlCancelled(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return (*static_cast<std::function<bool()>*>(user))() ? 1 : 0;
}

// The other half of resolveToFile()'s race, ported from pipensx's
// torrent_metainfo_fetch.cpp: fetch the .torrent itself over HTTPS from a
// public torrent cache keyed by info hash -- no BitTorrent traffic at all.
// On a network that blocks outgoing BitTorrent connections (the "found N
// peers, couldn't connect to any" failure) this is the only way the
// metadata can arrive; the download engine afterwards also speaks uTP, which
// such networks often still let through. The body is only accepted if its
// info dictionary hashes to the requested info hash.
bool fetchFromHttpsCache(const std::string& infoHashHex, const std::string& path, std::function<bool()> stop,
    std::string& error)
{
    std::string upper = infoHashHex;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    // itorrents.org 301-redirects to plain-http itorrents.net -- ask
    // itorrents.net over HTTPS directly (same torrents, no downgrade hop).
    const std::string url = "https://itorrents.net/torrent/" + upper + ".torrent";

    std::vector<uint8_t> body;
    long status = 0;
    CURL* curl  = curl_easy_init();
    if (!curl)
    {
        error = "Nao foi possivel iniciar HTTP.";
        return false;
    }
    // Same CA bundle http_client.cpp uses: this curl build trusts nothing
    // it isn't pointed at, and on the Switch the file lives in romfs.
    static const std::string caBundlePath = std::string(BRLS_RESOURCES) + "ca-bundle.crt";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "StreamNX/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlAppend);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlCancelled);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop);
    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (stop())
    {
        error = "Cancelado.";
        return false;
    }
    if (result != CURLE_OK)
    {
        error = std::string("cache HTTPS falhou: ") + curl_easy_strerror(result);
        return false;
    }
    if (status != 200)
    {
        error = "cache HTTPS respondeu HTTP " + std::to_string(status);
        return false;
    }

    uint8_t expected[20];
    metainfo_t parsed {};
    if (!parseInfoHash(infoHashHex, expected) || body.empty() || body.front() != 'd' ||
        !metainfo_parse(body.data(), body.size(), &parsed))
    {
        error = "cache HTTPS devolveu um arquivo que nao e torrent.";
        return false;
    }
    bool matches = std::memcmp(parsed.info_hash, expected, 20) == 0;
    metainfo_free(&parsed);
    if (!matches)
    {
        error = "torrent do cache HTTPS nao bate com o info hash.";
        return false;
    }
    return writeTorrentAtomic(path, body, error);
}

} // namespace

bool resolveToFile(const std::string& infoHashHex, const std::vector<std::string>& trackers, const std::string& path,
    std::atomic<bool>& cancelled, const ProgressCallback& progress, std::string& error, std::vector<uint8_t>* verifiedPeers)
{
    if (verifiedPeers)
        verifiedPeers->clear();

    // Same race pipensx runs: HTTPS torrent cache vs. tracker/DHT/peer
    // swarm, first fully validated result wins. Each writes its own
    // candidate file and only the winner is renamed into place, so `path`
    // never holds a partial or losing result.
    const std::string cachePath = path + ".cache-candidate";
    const std::string swarmPath = path + ".swarm-candidate";
    unlink(cachePath.c_str());
    unlink(swarmPath.c_str());

    std::atomic<int> winner { 0 }; // 0 = none yet, 1 = HTTPS cache, 2 = swarm
    std::atomic<bool> cacheWon { false };
    std::atomic<bool> swarmWon { false };
    std::string cacheError;
    const uint64_t startedMs = now_ms();

    std::thread cacheThread([&] {
        try
        {
            auto stop = [&] { return cancelled.load() || swarmWon.load(); };
            // A single transient timeout must not kill the only path that
            // works on a BitTorrent-blocking network: retry once.
            for (int attempt = 0; attempt < 2 && !stop(); ++attempt)
            {
                cacheError.clear();
                if (fetchFromHttpsCache(infoHashHex, cachePath, stop, cacheError))
                {
                    int expected = 0;
                    if (winner.compare_exchange_strong(expected, 1) && std::rename(cachePath.c_str(), path.c_str()) == 0)
                    {
                        cacheWon.store(true);
                        brls::Logger::info("magnet_resolver: resolved via HTTPS cache in {} ms", now_ms() - startedMs);
                    }
                    return;
                }
                brls::Logger::warning("magnet_resolver: {}", cacheError);
                if (cacheError.find("HTTP 404") != std::string::npos || cacheError.find("nao bate") != std::string::npos)
                    return; // definitive -- the cache doesn't have this torrent
            }
        }
        catch (const std::exception& e)
        {
            cacheError = e.what();
        }
        catch (...)
        {
            cacheError = "erro inesperado no cache HTTPS";
        }
    });
    struct JoinCache
    {
        std::thread& thread;
        ~JoinCache()
        {
            if (thread.joinable())
                thread.join();
        }
    } joinCache { cacheThread };

    std::string swarmError;
    bool swarmOk = resolveFromSwarm(infoHashHex, trackers, swarmPath, cancelled, &cacheWon, progress, swarmError, verifiedPeers);
    if (swarmOk)
    {
        int expected = 0;
        if (winner.compare_exchange_strong(expected, 2) && std::rename(swarmPath.c_str(), path.c_str()) == 0)
        {
            swarmWon.store(true);
            brls::Logger::info("magnet_resolver: resolved via peers in {} ms", now_ms() - startedMs);
        }
    }

    // The swarm gave up (or lost) -- the cache may still be mid-request.
    cacheThread.join();
    unlink(cachePath.c_str());
    unlink(swarmPath.c_str());

    if (cacheWon.load() || swarmWon.load())
    {
        if (cacheWon.load() && verifiedPeers)
            verifiedPeers->clear();
        return true;
    }

    error = swarmError;
    if (!cacheError.empty() && !cancelled.load())
        error += " (" + cacheError + ")";
    return false;
}

} // namespace magnet_resolver

#endif // USE_PIPENSX_BT
