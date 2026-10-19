#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Resolves a torrent's magnet-only info (infoHash + trackers, exactly what
// stremio::StreamEntry already carries -- no addon ever hands out a .torrent
// file) into a full .torrent written to disk, so download_manager_switch.cpp
// can metainfo_load() it and hand it to torrent_create_ex() the normal way.
//
// Adapted from pipensx's own src/app/magnet_resolver.cpp (same engine, same
// mechanism: tracker announce + DHT search + BT peer handshake (MSE/PE
// fallback) + BEP-9 ut_metadata fetch, with PEX amplification when the peer
// list is thin) -- trimmed of everything specific to that app's product
// decisions: the fixed RuTracker tracker allowlist/mirror expansion, and the
// parallel itorrents.net HTTPS metadata-cache race. This instead announces
// to whatever trackers the caller passes in (StreamEntry::trackers, already
// plumbed through the rest of this app) and only ever resolves over the
// swarm (trackers + DHT + PEX).
namespace magnet_resolver
{

struct Progress
{
    enum class Stage
    {
        FindingPeers,
        Connecting,
        FetchingMetadata,
        Validating,
    };

    Stage stage           = Stage::FindingPeers;
    uint32_t completedPieces = 0;
    uint32_t totalPieces     = 0;
    uint32_t peerIndex       = 0;
    uint32_t peerCount       = 0;
};

using ProgressCallback = std::function<void(const Progress&)>;

// Blocking -- run on its own thread (one per download, same as the rest of
// download_manager_switch.cpp already does). `infoHashHex` is the 40-char
// hex info hash (StreamEntry::infoHash, as-is). `cancelled` lets the caller
// abort mid-resolve (checked between I/O waits, not just at the top).
// `verifiedPeers`, when given, receives compact (6-byte) endpoints of every
// peer that completed a BT handshake during the resolve -- pass these to
// torrent_add_initial_peers() after torrent_create_ex() so the download
// doesn't have to reconnect peers this already proved were alive.
bool resolveToFile(const std::string& infoHashHex, const std::vector<std::string>& trackers,
    const std::string& path, std::atomic<bool>& cancelled, const ProgressCallback& progress, std::string& error,
    std::vector<uint8_t>* verifiedPeers = nullptr);

} // namespace magnet_resolver
