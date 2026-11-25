#include <fstream>
#include <unistd.h>
#include "haxball_client.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

namespace haxball {

static std::string limitUTF8Codepoints(const std::string& text, size_t maxCodepoints) {
    size_t i = 0;
    size_t count = 0;
    while (i < text.size() && count < maxCodepoints) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t step = 1;
        if ((c & 0x80) == 0) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        if (i + step > text.size()) break;
        i += step;
        count++;
    }
    return text.substr(0, i);
}

void GameAction::deserializePayload(::BufferReader& r, bool isSinglePacket) {
    eventType = r.readU8();
    isPredicted = (eventType == 3 || eventType == 7 || eventType == 8 ||
                   eventType == 9 || eventType == 12 || eventType == 21 || eventType == 23);
    isTimed = (eventType == 3 || eventType == 9);

    switch (eventType) {
        case 0: {
            strVal = r.readStr();
            i32Val1 = r.readI32BE();
            u8Val1 = r.readU8();
            u8Val2 = r.readU8();
            break;
        }
        case 1: {
            u8Val1 = r.readU8();
            if (isSinglePacket && r.remaining() >= 5) {
                i32Val1 = r.readI32BE();
                u8Val2 = r.readU8();
                hasExtraChatInd = true;
            }
            break;
        }
        case 2: {
            uint32_t len = r.readVarInt();
            rawBytes = r.readBytes(len);
            if (isSinglePacket && r.hasRemaining()) {
                auto extra = r.readBytes(r.remaining());
                rawBytes.insert(rawBytes.end(), extra.begin(), extra.end());
            }
            break;
        }
        case 3: {
            u32Val = r.readU32BE();
            break;
        }
        case 4: {
            strVal = r.readStr();
            break;
        }
        case 5: {
            i32Val1 = r.readI32BE();
            optStr1 = r.readOptStr();
            optStr2 = r.readOptStr();
            optStr3 = r.readOptStr();
            break;
        }
        case 6: {
            i32Val1 = r.readI32BE();
            optStr1 = r.readOptStr();
            boolVal = r.readU8() != 0;
            break;
        }
        case 7:
        case 8:
            break;
        case 9: {
            boolVal = r.readU8() != 0;
            break;
        }
        case 10: {
            i32Val1 = r.readI32BE();
            i32Val2 = r.readI32BE();
            break;
        }
        case 11: {
            uint16_t compLen = r.readU16BE();
            auto compBytes = r.readBytes(compLen);
            auto inflated = raw_inflate(compBytes.data(), compBytes.size());
            BufferReader sr(inflated.data(), inflated.size());
            stadium = Stadium::deserialize(sr);
            break;
        }
        case 12: {
            i32Val1 = r.readI32BE();
            i32Val2 = r.readI8();
            break;
        }
        case 13: {
            boolVal = r.readU8() != 0;
            break;
        }
        case 14: {
            i32Val1 = r.readI32BE();
            boolVal = r.readU8() != 0;
            break;
        }
        case 15:
            break;
        case 16: {
            boolVal = r.readU8() != 0;
            break;
        }
        case 17: {
            uint32_t count = r.readVarInt();
            if (count > r.remaining() || count > 1024u) {
                throw std::runtime_error("Invalid Ping list count: " + std::to_string(count));
            }
            u32List.clear();
            u32List.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                u32List.push_back(r.readVarInt());
            }
            break;
        }
        case 18: {
            optStr1 = r.readOptStr();
            if (optStr1.has_value()) {
                *optStr1 = limitUTF8Codepoints(*optStr1, 2);
            }
            break;
        }
        case 19: {
            i32Val1 = r.readI8();
            teamColors.deserialize(r);
            break;
        }
        case 20: {
            boolVal = r.readU8() != 0;
            uint8_t count = r.readU8();
            i32List.clear();
            i32List.reserve(count);
            for (uint8_t i = 0; i < count; ++i) {
                i32List.push_back(r.readI32BE());
            }
            break;
        }
        case 21: {
            i32Val1 = r.readI32BE();
            i32Val2 = r.readI32BE();
            i32Val3 = r.readI32BE();
            break;
        }
        case 22: {
            optStr1 = r.readOptStr();
            if (optStr1.has_value()) {
                *optStr1 = limitUTF8Codepoints(*optStr1, 2);
            }
            i32Val1 = r.readI32BE();
            break;
        }
        case 23: {
            i32Val1 = r.readI32BE();
            boolVal = r.readU8() != 0;
            uint16_t flags = r.readU16BE();
            for (int i = 0; i < 10; ++i) {
                if (flags & (1 << i)) {
                    fProps[i] = r.readF32BE();
                } else {
                    fProps[i] = std::nullopt;
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (flags & (1 << (10 + i))) {
                    iProps[i] = r.readI32BE();
                } else {
                    iProps[i] = std::nullopt;
                }
            }
            break;
        }
        case 24: {
            u32Val = r.readU32BE();
            strVal = r.readStr();
            break;
        }
        case 25: {
            uint32_t compLen = r.readU32BE();
            rawBytes = r.readBytes(compLen);
            break;
        }
        case 26: {
            i32Val1 = r.readI32BE();
            strVal = r.readStr();
            break;
        }
        default:
            break;
    }
}

HaxballClient::HaxballClient() {
    clientStartTime = std::chrono::steady_clock::now();
    baseWorld = std::make_shared<World>();
    realExtrapWorld = std::make_shared<World>();
    renderWorld = std::make_shared<World>();
}

HaxballClient::~HaxballClient() {
    disconnect();
}

double HaxballClient::getNowMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - clientStartTime).count();
}

void HaxballClient::connect(const std::string& rId, const std::string& name, const std::string& flag, const std::string& avatar, const std::string& password, const std::string& authKey) {
    disconnect();

    roomId = rId;
    roomPassword = password;
    playerName = name;
    playerFlag = flag.empty() ? "nl" : flag;
    playerAvatar = avatar;
    if (!authKey.empty()) {
        activeAuthKey = authKey;
    }

    status = ConnectionStatus::ConnectingSignaling;
    statusMessage = "Connecting to Haxball room...";
    channelsOpen = 0;
    localCandidates.clear();
    actionSequence = 1;
    lastSentKeyMask = 0;
    lastSentChatIndicator = false;
    initialPingsSent = 0;
    clockOffsetFilter.clear();
    pingFilter.clear();
    syncQueue.clear();
    actionQueue.clear();
    localPredictions.clear();

    if (!activeAuthKey.empty()) {
        signer = std::make_unique<AuthSigner>(activeAuthKey);
    } else {
        signer = std::make_unique<AuthSigner>();
        activeAuthKey = signer->getAuthKeyString();
    }

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    pc = std::make_shared<rtc::PeerConnection>(config);

    rtc::WebSocket::Configuration wsConfig;

    if (std::ifstream("/etc/ssl/cert.pem").good()) {
        wsConfig.caCertificatePemFile = "/etc/ssl/cert.pem";
    }
    ws = std::make_shared<rtc::WebSocket>(wsConfig);

    std::string wsUrl = "wss://p2p.haxball.com/client?id=" + roomId;
    if (verbose) {
        std::cout << "\n================ [WebRTC Telemetry] ================" << std::endl;
        std::cout << "[Connect] Target Room ID: " << roomId << std::endl;
        std::cout << "[Signaling] Opening WebSocket -> " << wsUrl << std::endl;
    }

    pc->onLocalDescription([this, wsUrl](rtc::Description desc) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        offerSdp = std::string(desc);
        status = ConnectionStatus::NegotiatingWebRTC;
        statusMessage = "Connecting to signaling server...";

        if (verbose) {
            std::cout << "[SDP] Local Description Generated (" << desc.type() << "):" << std::endl;
            std::cout << "----------------------------------------------------" << std::endl;
            std::cout << offerSdp << std::endl;
            std::cout << "----------------------------------------------------" << std::endl;
        }

        rtc::WebSocket::Headers headers;
        headers["Origin"] = "https://www.haxball.com";
        headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
        if (ws) ws->open(wsUrl, headers);
    });

    pc->onLocalCandidate([this](rtc::Candidate candidate) {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        json c;
        c["candidate"] = candidate.candidate();
        c["sdpMid"] = candidate.mid();
        c["sdpMLineIndex"] = 0;
        localCandidates.push_back(c);

        if (verbose) {
            std::cout << "[ICE] Local Candidate Gathered: " << candidate.candidate() << std::endl;
        }

        if (ws && ws->isOpen()) {
            BufferWriter f;
            f.writeStr(c.dump());
            auto compressed = raw_deflate(f.buf.data(), f.buf.size());
            BufferWriter packet;
            packet.writeU8(4);
            packet.writeBytes(compressed.data(), compressed.size());
            ws->send((const std::byte*)packet.buf.data(), packet.buf.size());
        }
    });

    ws->onOpen([this]() {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        statusMessage = "Sending WebRTC offer...";
        if (verbose) {
            std::cout << "[Signaling] WebSocket open! Forwarding deflated SDP offer + "
                      << localCandidates.size() << " ICE candidates..." << std::endl;
        }

        BufferWriter f;
        f.writeU8(0);
        f.writeStr(offerSdp);
        f.writeStr(json(localCandidates).dump());
        f.writeU16BE(9);
        f.writeOptStr(roomPassword.empty() ? nullptr : &roomPassword);

        auto compressed = raw_deflate(f.buf.data(), f.buf.size());
        BufferWriter packet;
        packet.writeU8(1);
        packet.writeBytes(compressed.data(), compressed.size());
        ws->send((const std::byte*)packet.buf.data(), packet.buf.size());
    });

    ws->onMessage([this](rtc::message_variant data) {
        if (!std::holds_alternative<rtc::binary>(data)) return;
        const auto& raw = std::get<rtc::binary>(data);
        if (raw.empty()) return;

        try {
            BufferReader r((const uint8_t*)raw.data(), raw.size());
            uint8_t type = r.readU8();

            if (r.hasRemaining()) {
                auto decompressed = raw_inflate(r.data + r.pos, r.remaining());
                BufferReader dr(decompressed.data(), decompressed.size());
                if (type == 1) {
                    std::string sdp = dr.readStr();
                    std::string candStr = dr.readStr();

                    if (verbose) {
                        std::cout << "[SDP] Received Remote SDP Answer from Host:" << std::endl;
                        std::cout << "----------------------------------------------------" << std::endl;
                        std::cout << sdp << std::endl;
                        std::cout << "----------------------------------------------------" << std::endl;
                    }

                    std::lock_guard<std::recursive_mutex> lock(clientMutex);
                    remoteIpv4Candidates.clear();
                    if (pc) {
                        pc->setRemoteDescription(rtc::Description(sdp, "answer"));
                        auto cands = json::parse(candStr);
                        for (const auto& item : cands) {
                            std::string c = item["candidate"];
                            std::string mid = item.value("sdpMid", "0");
                            if (verbose) {
                                std::cout << "[ICE] Remote Host Candidate: " << c << std::endl;
                            }
                            rtc::Candidate rc(c, mid);
                            if (rc.family() == rtc::Candidate::Family::Ipv4 && rc.address() && rc.port()) {
                                std::string ep = *rc.address() + ":" + std::to_string(*rc.port());
                                if (std::find(remoteIpv4Candidates.begin(), remoteIpv4Candidates.end(), ep) == remoteIpv4Candidates.end()) {
                                    remoteIpv4Candidates.push_back(ep);
                                }
                            }
                            pc->addRemoteCandidate(rc);
                        }
                    }
                } else if (type == 4) {
                    std::string candStr = dr.readStr();
                    auto item = json::parse(candStr);
                    std::string c = item["candidate"];
                    std::string mid = item.value("sdpMid", "0");
                    if (verbose) {
                        std::cout << "[ICE] Trickle Remote Host Candidate: " << c << std::endl;
                    }
                    std::lock_guard<std::recursive_mutex> lock(clientMutex);
                    rtc::Candidate rc(c, mid);
                    if (rc.family() == rtc::Candidate::Family::Ipv4 && rc.address() && rc.port()) {
                        std::string ep = *rc.address() + ":" + std::to_string(*rc.port());
                        if (std::find(remoteIpv4Candidates.begin(), remoteIpv4Candidates.end(), ep) == remoteIpv4Candidates.end()) {
                            remoteIpv4Candidates.push_back(ep);
                        }
                    }
                    if (pc) pc->addRemoteCandidate(rc);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[WebSocket] Message parse error: " << e.what() << std::endl;
        }
    });

    ws->onError([this](std::string err) {
        std::cerr << "[WebSocket] Error: " << err << std::endl;
    });

    auto setupChannel = [this](int idx, std::shared_ptr<rtc::DataChannel> dc) {
        dcs[idx] = dc;
        dc->onOpen([this, idx]() {
            int openCount = ++channelsOpen;
            if (verbose) {
                std::cout << "[SCTP] DataChannel #" << idx << " opened (" << openCount << "/3 ready)" << std::endl;
            }
            if (openCount == 3) {
                {
                    std::lock_guard<std::recursive_mutex> lock(clientMutex);
                    status = ConnectionStatus::Authenticating;
                    statusMessage = "WebRTC connected. Authenticating...";
                }
                if (pc) {
                    auto fmtAddr = [](const std::string& ip, uint16_t port) {
                        if (ip.find(':') != std::string::npos) {
                            return "[" + ip + "]:" + std::to_string(port);
                        }
                        return ip + ":" + std::to_string(port);
                    };
                    rtc::Candidate localCand, remoteCand;
                    if (pc->getSelectedCandidatePair(&localCand, &remoteCand)) {
                        std::string remIp = remoteCand.address().value_or("unknown");
                        uint16_t remPort = remoteCand.port().value_or(0);
                        std::string locIp = localCand.address().value_or("unknown");
                        uint16_t locPort = localCand.port().value_or(0);
                        std::cout << "[WebRTC] Connected to room '" << roomId << "' -> Remote: "
                                  << fmtAddr(remIp, remPort)
                                  << " (Local: " << fmtAddr(locIp, locPort) << ")" << std::endl;
                    } else if (auto remAddr = pc->remoteAddress()) {
                        std::cout << "[WebRTC] Connected to room '" << roomId << "' -> Remote: "
                                  << *remAddr << std::endl;
                    }

                    std::lock_guard<std::recursive_mutex> lock(clientMutex);
                    if (!remoteIpv4Candidates.empty()) {
                        std::cout << "[WebRTC] Host IPv4: ";
                        for (size_t i = 0; i < remoteIpv4Candidates.size(); ++i) {
                            if (i > 0) std::cout << ", ";
                            std::cout << remoteIpv4Candidates[i];
                        }
                        std::cout << std::endl;
                    } else {
                        std::cout << "[WebRTC] Host IPv4: (none advertised)" << std::endl;
                    }
                }
                if (verbose) {
                    std::cout << "[WebRTC] All 3 SCTP DataChannels established successfully!" << std::endl;
                }
            }
        });
        dc->onMessage([this, idx](rtc::message_variant data) {
            if (!std::holds_alternative<rtc::binary>(data)) return;
            const auto& raw = std::get<rtc::binary>(data);
            handleChannelMessage(idx, (const uint8_t*)raw.data(), raw.size());
        });
    };

    rtc::DataChannelInit init0;
    init0.id = 0;
    init0.negotiated = true;
    init0.reliability.unordered = false;
    setupChannel(0, pc->createDataChannel("c1", init0));

    rtc::DataChannelInit init1;
    init1.id = 1;
    init1.negotiated = true;
    init1.reliability.unordered = true;
    setupChannel(1, pc->createDataChannel("c2", init1));

    rtc::DataChannelInit init2;
    init2.id = 2;
    init2.negotiated = true;
    init2.reliability.unordered = true;
    init2.reliability.maxRetransmits = 0;
    setupChannel(2, pc->createDataChannel("c3", init2));
}

void HaxballClient::disconnect() {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    status = ConnectionStatus::Disconnected;
    statusMessage = "Disconnected";
    for (int i = 0; i < 3; ++i) {
        if (dcs[i]) {
            try { dcs[i]->close(); } catch (...) {}
            dcs[i].reset();
        }
    }
    if (ws) {
        try { ws->close(); } catch (...) {}
        ws.reset();
    }
    if (pc) {
        try { pc->close(); } catch (...) {}
        pc.reset();
    }
}

void HaxballClient::handleChannelMessage(int channelIdx, const uint8_t* data, size_t size) {
    if (size == 0) return;
    try {
        BufferReader r(data, size);
        uint8_t type = r.readU8();

        switch (type) {
            case 0: handleChallenge(r); break;
            case 1: handleGameState(r); break;
            case 2: handleBroadcastAction(r); break;
            case 3: handleFrameSync(r); break;
            case 4: handlePingReply(r); break;
            case 5: handleDisconnect(r); break;
            case 6: handleImmediateAction(r); break;
            default: break;
        }
    } catch (const std::exception& e) {
        std::cerr << "[HaxballClient] Channel #" << channelIdx << " message error: " << e.what() << std::endl;
    }
}

void HaxballClient::handleChallenge(BufferReader& r) {
    uint32_t chal_len = r.readVarInt();
    auto chal = r.readBytes(chal_len);

    if (!signer) return;
    auto signedResp = signer->signChallenge(chal.data(), chal.size());

    BufferWriter playerMeta;
    playerMeta.writeStr(playerName);
    playerMeta.writeStr(playerFlag);
    playerMeta.writeOptStr(playerAvatar.empty() ? nullptr : &playerAvatar);

    BufferWriter resp;
    resp.writeU8(0);
    resp.writeVarInt((uint32_t)signedResp.size());
    resp.writeBytes(signedResp.data(), signedResp.size());
    resp.writeVarInt((uint32_t)playerMeta.buf.size());
    resp.writeBytes(playerMeta.buf.data(), playerMeta.buf.size());

    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (dcs[0] && dcs[0]->isOpen()) {
        dcs[0]->send((const std::byte*)resp.buf.data(), resp.buf.size());
        status = ConnectionStatus::Synchronizing;
        statusMessage = "Receiving room state...";
    }
}

GameAction HaxballClient::readBroadcastAction(BufferReader& r, bool isSinglePacket) {
    GameAction act;
    act.frame = r.readU32BE();
    act.order = r.readVarInt();
    act.senderId = r.readU16BE();
    act.clientSeq = r.readU32BE();
    act.deserializePayload(r, isSinglePacket);
    return act;
}

void HaxballClient::handleGameState(BufferReader& r) {
    if (!r.hasRemaining()) return;
    auto inflated = raw_inflate(r.data + r.pos, r.remaining());
    BufferReader sr(inflated.data(), inflated.size());

    bool connectedTrigger = false;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        try {
            myPlayerId = sr.readU16BE();
            currentFrame = sr.readU32BE();
            totalActionsProcessed = sr.readU32BE();
            actionsAtCurrentFrame = sr.readVarInt();
            dD = 10.0;

            actionQueue.clear();
            localPredictions.clear();
            syncQueue.clear();
            opponentFakeMemory.clear();
            predictedPulseReleaseFrame = 0;

            baseWorld = std::make_shared<World>();
            baseWorld->onKick = [this]() { if (onKickSound) onKickSound(); };
            baseWorld->onGoal = [this](int t) { if (onGoalSound) onGoalSound(t); };
            baseWorld->onPlayerBallKick = [this](const BallKickEvent& ev) { if (onPlayerBallKick) onPlayerBallKick(ev); };
            baseWorld->deserialize(sr);
            baseWorld->currentTick = currentFrame;

            while (sr.hasRemaining()) {
                GameAction act = readBroadcastAction(sr, false);
                enqueueAction(std::move(act));
            }

            clockOffsetFilter.clear();

            *realExtrapWorld = baseWorld->cloneState();
            *renderWorld = baseWorld->cloneState();

            status = ConnectionStatus::Connected;
            statusMessage = "Connected to room!";
            connectedTrigger = true;
        } catch (const std::exception& e) {
            std::cerr << "[HaxballClient] GameState parse error: " << e.what() << std::endl;
        }
    }

    if (connectedTrigger) {
        sendPing();
        if (onConnected) onConnected();
    }
}

void HaxballClient::handleBroadcastAction(BufferReader& r) {
    if (!r.hasRemaining()) return;
    try {
        GameAction act = readBroadcastAction(r, true);
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        uint16_t sender = act.senderId;
        uint32_t seq = act.clientSeq;
        enqueueAction(std::move(act));

        if (sender == myPlayerId) {
            localPredictions.erase(
                std::remove_if(localPredictions.begin(), localPredictions.end(),
                    [seq](const GameAction& a) { return a.clientSeq == seq; }),
                localPredictions.end()
            );
        }
        processSyncQueue();
    } catch (const std::exception& e) {
        std::cerr << "[HaxballClient] BroadcastAction parse error: " << e.what() << std::endl;
    }
}

void HaxballClient::handleFrameSync(BufferReader& r) {
    if (r.remaining() < 8) return;
    uint32_t targetFrame = r.readU32BE();
    uint32_t expectedActions = r.readU32BE();

    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (syncQueue.size() >= 600) {
        syncQueue.erase(syncQueue.begin(), syncQueue.begin() + 300);
    }
    syncQueue.push_back({targetFrame, expectedActions});
    processSyncQueue();
}

void HaxballClient::handlePingReply(BufferReader& r) {
    if (r.remaining() < 16) return;
    double hostFrame = r.readF64BE();
    double clientSentMs = r.readF64BE();
    double nowMs = getNowMs();
    double rtt = std::max(1.0, nowMs - clientSentMs);

    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    clockOffsetFilter.add(hostFrame - clientSentMs * 0.06);
    pingFilter.add(rtt);
    pingMs = static_cast<int>(std::round(pingFilter.quantile(0.5)));
    maxPingMs = static_cast<int>(std::round(pingFilter.max()));
}

void HaxballClient::handleDisconnect(BufferReader& r) {
    bool isBanned = r.readU8() != 0;
    std::string reason = r.readStr();
    std::string msg = (isBanned ? "Banned: " : "Kicked: ") + reason;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        status = ConnectionStatus::Disconnected;
        statusMessage = msg;
    }
    if (onDisconnected) onDisconnected(msg);
}

void HaxballClient::handleImmediateAction(BufferReader& r) {
    if (!r.hasRemaining()) return;
    try {
        GameAction act;
        act.senderId = 0;
        act.clientSeq = 0;
        act.deserializePayload(r, true);

        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        applyAction(*baseWorld, act, true);
    } catch (const std::exception& e) {
        std::cerr << "[HaxballClient] ImmediateAction parse error: " << e.what() << std::endl;
    }
}

void HaxballClient::enqueueAction(GameAction act) {
    if (act.frame < currentFrame || (act.frame == currentFrame && act.order <= actionsAtCurrentFrame)) {
        act.frame = currentFrame;
        act.order = actionsAtCurrentFrame++;
        applyAction(*baseWorld, act, true);
        return;
    }

    size_t pos = 0;
    while (pos < actionQueue.size()) {
        if (actionQueue[pos].frame > act.frame) break;
        if (actionQueue[pos].frame == act.frame) {
            if (actionQueue[pos].order > act.order) break;
            if (actionQueue[pos].order == act.order) {
                act.order++;
            }
        }
        pos++;
    }
    actionQueue.insert(actionQueue.begin() + pos, std::move(act));
}

void HaxballClient::processSyncQueue() {
    if (status != ConnectionStatus::Connected) return;

    for (const auto& f : syncQueue) {
        if (f.targetFrame <= currentFrame) continue;

        uint32_t actionsBeforeGr = 0;
        for (const auto& act : actionQueue) {
            if (act.frame >= f.targetFrame) break;
            actionsBeforeGr++;
        }

        if (f.expectedActions == totalActionsProcessed + actionsAtCurrentFrame + actionsBeforeGr) {
            uint32_t steps = f.targetFrame - currentFrame;
            stepAuthoritative(steps);
        }
    }

    syncQueue.erase(
        std::remove_if(syncQueue.begin(), syncQueue.end(),
            [this](const FrameSync& f) { return f.targetFrame <= currentFrame; }),
        syncQueue.end()
    );

    localPredictions.erase(
        std::remove_if(localPredictions.begin(), localPredictions.end(),
            [this](const GameAction& a) { return a.frame < currentFrame; }),
        localPredictions.end()
    );
}

void HaxballClient::stepAuthoritative(uint32_t steps) {
    if (steps > 600) steps = 600;
    size_t idx = 0;
    size_t n = actionQueue.size();

    auto computeMinBallGap = [](const World& w, const Player& p) -> double {
        if (!w.hasMatch || p.discIndex < 0 || p.discIndex >= (int)w.discs.size()) return 999.0;
        const auto& pDisc = w.discs[p.discIndex];
        double minG = 999.0;
        for (size_t di = 0; di < w.discs.size(); ++di) {
            if ((int)di == p.discIndex) continue;
            const auto& bDisc = w.discs[di];
            if ((bDisc.cGroup & 64) == 0) continue;
            double dx = bDisc.pos.x - pDisc.pos.x;
            double dy = bDisc.pos.y - pDisc.pos.y;
            double g = std::sqrt(dx * dx + dy * dy) - bDisc.radius - pDisc.radius;
            if (g < minG) minG = g;
        }
        return minG;
    };

    for (uint32_t f = 0; f < steps; ++f) {
        while (idx < n && actionQueue[idx].frame <= currentFrame) {
            applyAction(*baseWorld, actionQueue[idx], true);
            actionsAtCurrentFrame++;
            idx++;
        }

        if (baseWorld->hasMatch && !baseWorld->discs.empty()) {
            for (const auto& p : baseWorld->players) {
                if (p.id == myPlayerId || p.discIndex < 0) continue;
                auto& mem = opponentFakeMemory[p.id];
                double gPre = computeMinBallGap(*baseWorld, p);
                if (!mem.prevIsKicking && p.isKicking) {
                    mem.kickStartGap = gPre;
                }
                mem.prevIsKicking = p.isKicking;
                mem.prevKickCooldown = p.kickCooldown;
                mem.prevGap = gPre;
            }
        }

        baseWorld->step(1.0);
        totalActionsProcessed += actionsAtCurrentFrame;
        actionsAtCurrentFrame = 0;
        currentFrame++;
        baseWorld->currentTick = currentFrame;

        if (baseWorld->hasMatch && !baseWorld->discs.empty()) {
            for (const auto& p : baseWorld->players) {
                if (p.id == myPlayerId || p.discIndex < 0) continue;
                auto& mem = opponentFakeMemory[p.id];
                double gPost = computeMinBallGap(*baseWorld, p);

                bool realKickOccurred = (p.kickCooldown > mem.prevKickCooldown);
                if (realKickOccurred) {
                    mem.consecutiveOutOfRangeKickTicks = 0;
                    if (mem.recentFakeCount > 0) mem.recentFakeCount--;
                } else if (mem.prevIsKicking && !p.isKicking) {

                    if (mem.prevGap > 4.02 && mem.prevGap <= 35.0) {
                        mem.lastFakeFrame = currentFrame;
                        mem.recentFakeCount = std::min(mem.recentFakeCount + 1, 5);
                    }
                    mem.consecutiveOutOfRangeKickTicks = 0;
                } else if (p.isKicking) {
                    if (gPost > 4.05 && gPost <= 35.0) {
                        mem.consecutiveOutOfRangeKickTicks++;
                        if (mem.consecutiveOutOfRangeKickTicks >= 2 && mem.kickStartGap > 5.0) {
                            mem.lastFakeFrame = currentFrame;
                        }
                    } else if (gPost <= 4.05) {
                        mem.consecutiveOutOfRangeKickTicks = 0;
                    }
                } else {
                    mem.consecutiveOutOfRangeKickTicks = 0;

                    if (mem.recentFakeCount > 0 && currentFrame > mem.lastFakeFrame + 150) {
                        mem.recentFakeCount = 0;
                    }
                }
            }
        }
    }

    while (idx < n && actionQueue[idx].frame <= currentFrame && actionQueue[idx].order <= actionsAtCurrentFrame) {
        applyAction(*baseWorld, actionQueue[idx], true);
        actionsAtCurrentFrame++;
        idx++;
    }

    if (idx > 0) {
        actionQueue.erase(actionQueue.begin(), actionQueue.begin() + idx);
    }

    dD = std::clamp(dD - (double)steps, 0.0, 120.0);
}

void HaxballClient::applyAction(World& w, const GameAction& act, bool isAuthoritative) {
    const Player* sender = (act.senderId == 0) ? nullptr : w.getPlayer(act.senderId);
    bool isHostOrAdmin = (act.senderId == 0) || (sender != nullptr && sender->admin);

    switch (act.eventType) {
        case 0: {
            if (isAuthoritative && act.senderId == 0 && onAnnouncement) {
                AnnouncementMsg ann;
                ann.text = act.strVal;
                ann.color = (uint32_t)act.i32Val1;
                ann.style = act.u8Val1;
                ann.sound = act.u8Val2;
                onAnnouncement(ann);
            }
            break;
        }
        case 1: {
            if (act.hasExtraChatInd) break;
            if (auto* p = w.getPlayer(act.senderId)) {
                p->chatIndicator = (act.u8Val1 == 0);
            }
            break;
        }
        case 2: {
            if (isAuthoritative && act.rawBytes.size() > 4) {
                try {
                    BufferReader rec(act.rawBytes.data() + 4, act.rawBytes.size() - 4);
                    w.deserialize(rec);
                } catch (...) {}
            }
            break;
        }
        case 3: {
            w.setPlayerInput(act.senderId, act.u32Val);
            break;
        }
        case 4: {
            if (isAuthoritative) {
                if (auto* p = w.getPlayer(act.senderId)) {
                    if (onChatReceived) onChatReceived(p->name, p->team, p->admin, act.strVal);
                }
            }
            break;
        }
        case 5: {
            if (act.senderId == 0) {
                w.addPlayer((uint32_t)act.i32Val1,
                            act.optStr1.value_or("Player"),
                            act.optStr2.value_or("nl"),
                            act.optStr3.value_or(""));
            }
            break;
        }
        case 6: {
            if (isHostOrAdmin || (uint32_t)act.i32Val1 == act.senderId) {
                w.removePlayer((uint32_t)act.i32Val1);
                if (isAuthoritative) {
                    opponentFakeMemory.erase((uint32_t)act.i32Val1);
                }
            }
            break;
        }
        case 7: {
            if (isHostOrAdmin) {
                w.startGame();
            }
            break;
        }
        case 8: {
            if (isHostOrAdmin) {
                w.stopGame();
                if (isAuthoritative) {
                    lastSentKeyMask = 0;
                }
            }
            break;
        }
        case 9: {
            if (isHostOrAdmin) {
                w.pauseResumeGame(act.boolVal);
            }
            break;
        }
        case 10: {
            if (isHostOrAdmin && !w.hasMatch) {
                if (act.i32Val1 == 0) {
                    w.scoreLimit = std::clamp(act.i32Val2, 0, 99);
                } else if (act.i32Val1 == 1) {
                    w.timeLimit = std::clamp(act.i32Val2, 0, 99);
                }
            }
            break;
        }
        case 11: {
            if (isHostOrAdmin && !w.hasMatch && act.stadium) {
                w.stadium = act.stadium;
            }
            break;
        }
        case 12: {
            if (isHostOrAdmin || ((uint32_t)act.i32Val1 == act.senderId && !w.teamsLocked && !w.hasMatch)) {
                w.setPlayerTeam((uint32_t)act.i32Val1, act.i32Val2);
            }
            break;
        }
        case 13: {
            if (isHostOrAdmin) {
                w.teamsLocked = act.boolVal;
            }
            break;
        }
        case 14: {
            if (isHostOrAdmin && act.i32Val1 != 0) {
                if (auto* p = w.getPlayer((uint32_t)act.i32Val1)) {
                    p->admin = act.boolVal;
                }
            }
            break;
        }
        case 15: {
            if (isHostOrAdmin) {
                w.autoTeams();
            }
            break;
        }
        case 16: {
            if (auto* p = w.getPlayer(act.senderId)) {
                p->desynced = act.boolVal;
            }
            break;
        }
        case 17: {
            if (act.senderId == 0) {
                for (size_t i = 0; i < w.players.size() && i < act.u32List.size(); ++i) {
                    w.players[i].ping = (int32_t)act.u32List[i];
                }
            }
            break;
        }
        case 18: {
            if (auto* p = w.getPlayer(act.senderId)) {
                p->avatar = act.optStr1.value_or("");
            }
            break;
        }
        case 19: {
            if (isHostOrAdmin && (act.i32Val1 == 1 || act.i32Val1 == 2)) {
                w.teamColors[act.i32Val1] = act.teamColors;
            }
            break;
        }
        case 20: {
            if (act.senderId == 0) {
                w.reorderPlayers(act.i32List, act.boolVal);
            }
            break;
        }
        case 21: {
            if (isHostOrAdmin) {
                w.setKickRateLimit(act.i32Val1, act.i32Val2, act.i32Val3);
            }
            break;
        }
        case 22: {
            if (act.senderId == 0) {
                if (auto* p = w.getPlayer((uint32_t)act.i32Val1)) {
                    p->headlessAvatar = act.optStr1;
                }
            }
            break;
        }
        case 23: {
            if (act.senderId == 0) {
                w.setDiscProperties(act.i32Val1, act.boolVal, act.fProps, act.iProps);
                if (isAuthoritative && !act.boolVal && act.i32Val1 == 0 && !w.discs.empty() &&
                    (act.fProps[2].has_value() || act.fProps[3].has_value()) && onServerBallSpeedSet) {
                    onServerBallSpeedSet(w.currentTick, w.discs[0].speed);
                }
            }
            break;
        }
        default:
            break;
    }
}

double HaxballClient::computeOffsetLocked(double nowMs) const {
    return clockOffsetFilter.empty() ? ((double)currentFrame + 10.0 - nowMs * 0.06) : clockOffsetFilter.quantile(0.5);
}

uint32_t HaxballClient::computeTargetFrameLocked(double nowMs) const {
    double offset = computeOffsetLocked(nowMs);
    double predAhead = std::clamp(nowMs * 0.06 + offset - (double)currentFrame, 0.0, 120.0);
    return currentFrame + static_cast<uint32_t>(predAhead) + static_cast<uint32_t>(std::max(0, handicapFrames));
}

uint32_t HaxballClient::getTargetFrame() const {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (status != ConnectionStatus::Connected) return currentFrame;
    return computeTargetFrameLocked(getNowMs());
}

void HaxballClient::rebuildRenderWorld() {

    World localWork;
    std::vector<GameAction> merged;
    uint32_t snapFrame = 0;
    double snapOffset = 0.0;
    double snapExtrapMs = 0.0;
    int snapHandicap = 0;
    bool snapAntiFake = true;
    bool snapAntiOpMode = false;
    uint32_t snapMyPlayerId = 0;
    uint32_t snapPredictRelease = 0;
    double nowMs = getNowMs();

    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (!baseWorld) return;

        snapFrame = currentFrame;
        snapOffset = computeOffsetLocked(nowMs);
        snapExtrapMs = extrapolationMs;
        snapHandicap = std::max(0, handicapFrames);
        snapAntiFake = antiFakeShot;
        snapAntiOpMode = antiOpMode;
        snapMyPlayerId = myPlayerId;
        snapPredictRelease = predictedPulseReleaseFrame;
        dD = std::clamp(nowMs * 0.06 + snapOffset - (double)snapFrame, 0.0, 120.0);

        localWork = baseWorld->cloneState();

        merged.reserve(actionQueue.size() + localPredictions.size());
        size_t i = 0, j = 0;
        while (i < actionQueue.size() && actionQueue[i].frame < snapFrame) i++;
        while (j < localPredictions.size() && localPredictions[j].frame < snapFrame) j++;
        while (i < actionQueue.size() && j < localPredictions.size()) {
            if (actionQueue[i].frame <= localPredictions[j].frame) {
                merged.push_back(actionQueue[i++]);
            } else {
                merged.push_back(localPredictions[j++]);
            }
        }
        while (i < actionQueue.size()) merged.push_back(actionQueue[i++]);
        while (j < localPredictions.size()) merged.push_back(localPredictions[j++]);
    }

    double realFrames = std::clamp(nowMs * 0.06 + snapOffset - (double)snapFrame + (double)snapHandicap, 0.0, 120.0);
    double extrapFrames = (nowMs + snapExtrapMs) * 0.06 + snapOffset - (double)snapFrame + (double)snapHandicap;

    if (extrapFrames <= 0.0) {
        auto wPtr = std::make_shared<World>(std::move(localWork));
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        renderWorld = wPtr;
        realExtrapWorld = std::move(wPtr);
        return;
    }
    if (extrapFrames > 120.0) extrapFrames = 120.0;

    int wholeFrames = static_cast<int>(extrapFrames);
    double fracFrame = extrapFrames - wholeFrames;
    double realFrac = realFrames - std::floor(realFrames);
    uint32_t realEndFrame = snapFrame + static_cast<uint32_t>(realFrames);

    size_t c = 0;
    size_t f = merged.size();
    uint32_t L = snapFrame;
    uint32_t endFrame = snapFrame + (uint32_t)wholeFrames;
    bool realSnapshotTaken = false;
    std::shared_ptr<World> localReal;
    std::shared_ptr<World> localRealSmooth;

    auto computeGapInWorld = [](const World& w, const Player& p) -> double {
        if (!w.hasMatch || p.discIndex < 0 || p.discIndex >= (int)w.discs.size()) return 1e9;
        const auto& pD = w.discs[p.discIndex];
        double minG = 1e9;
        for (size_t di = 0; di < w.discs.size(); ++di) {
            if ((int)di == p.discIndex) continue;
            const auto& bD = w.discs[di];
            if ((bD.cGroup & 64) == 0) continue;
            double dx = bD.pos.x - pD.pos.x;
            double dy = bD.pos.y - pD.pos.y;
            double g = std::sqrt(dx * dx + dy * dy) - (bD.radius + pD.radius);
            if (g < minG) minG = g;
        }
        return minG;
    };

    if (snapAntiFake && localWork.hasMatch) {
        for (auto& p : localWork.players) {
            p.allowSpeculativeKick = (p.id == snapMyPlayerId);
            p.fakeShotDetected = false;
        }
    }

    struct ExplicitKickEntry {
        uint32_t id;
        bool kicking;
    };
    ExplicitKickEntry explicitKicks[64];

    for (; L <= endFrame; ++L) {
        int explicitKickCount = 0;
        while (c < f && merged[c].frame <= L) {
            if (merged[c].eventType == 3 || merged[c].eventType == 1 || merged[c].isPredicted) {
                if (merged[c].eventType == 3) {
                    uint32_t sid = merged[c].senderId;
                    bool kicking = ((merged[c].u32Val & 16u) != 0);
                    bool updated = false;
                    for (int k = 0; k < explicitKickCount; ++k) {
                        if (explicitKicks[k].id == sid) {
                            explicitKicks[k].kicking = kicking;
                            updated = true;
                            break;
                        }
                    }
                    if (!updated && explicitKickCount < 64) {
                        explicitKicks[explicitKickCount++] = {sid, kicking};
                    }
                }
                applyAction(localWork, merged[c], false);
            }
            c++;
        }

        if (snapAntiFake && snapPredictRelease > 0 && L >= snapPredictRelease) {
            if (auto* me = localWork.getPlayer(snapMyPlayerId)) {
                me->input &= ~16u;
                me->isKicking = false;
            }
        }

        if (!realSnapshotTaken && L >= realEndFrame) {
            realSnapshotTaken = true;
            localReal = std::make_shared<World>(localWork.cloneState());
            if (snapAntiOpMode && snapExtrapMs > 0.0) {
                localRealSmooth = std::make_shared<World>(localWork.cloneState());
                if (realFrac > 1e-4) {
                    localRealSmooth->step(realFrac);
                }
            }

            if (snapAntiFake && localWork.hasMatch && !localWork.discs.empty()) {
                for (auto& p : localWork.players) {
                    double minG0 = computeGapInWorld(localWork, p);
                    if (p.id == snapMyPlayerId) {

                        p.allowSpeculativeKick = (minG0 < 4.0);
                        p.fakeShotDetected = false;
                    } else {
                        if (!p.allowSpeculativeKick && p.isKicking && minG0 <= 32.0) {
                            p.fakeShotDetected = true;
                        }
                    }
                }
            }
        }

        if (snapAntiFake && localWork.hasMatch && !localWork.discs.empty()) {
            for (auto& p : localWork.players) {
                if (p.id == snapMyPlayerId) continue;
                double gNow = computeGapInWorld(localWork, p);
                bool hasExplicitKick = false;
                if (L <= realEndFrame && explicitKickCount > 0) {
                    for (int k = 0; k < explicitKickCount; ++k) {
                        if (explicitKicks[k].id == p.id) {
                            hasExplicitKick = explicitKicks[k].kicking;
                            break;
                        }
                    }
                }
                bool confirmedInRangeKick = (hasExplicitKick && gNow < 4.0);
                p.allowSpeculativeKick = confirmedInRangeKick;
                if (!p.allowSpeculativeKick && p.isKicking && gNow <= 32.0) {
                    p.fakeShotDetected = true;
                }
            }
        }

        if (snapAntiOpMode && snapExtrapMs > 0.0 && L >= realEndFrame && localWork.hasMatch) {
            for (auto& p : localWork.players) {
                if (p.id == snapMyPlayerId) continue;
                p.input &= ~15u;
            }
        }

        localWork.step(L != endFrame ? 1.0 : fracFrame);
    }

    if (snapAntiOpMode && snapExtrapMs > 0.0 && localRealSmooth && localWork.hasMatch && localRealSmooth->hasMatch) {
        for (auto& p : localWork.players) {
            if (p.id == snapMyPlayerId) continue;
            if (p.discIndex < 0 || p.discIndex >= (int)localWork.discs.size()) continue;
            const Player* rp = localRealSmooth->getPlayer(p.id);
            if (!rp || rp->discIndex < 0 || rp->discIndex >= (int)localRealSmooth->discs.size()) continue;
            localWork.discs[p.discIndex].pos = localRealSmooth->discs[rp->discIndex].pos;
            localWork.discs[p.discIndex].speed = localRealSmooth->discs[rp->discIndex].speed;
            if (!rp->isKicking) {
                p.isKicking = false;
            }
        }
    }

    auto freshRender = std::make_shared<World>(std::move(localWork));
    if (!localReal) {
        localReal = freshRender;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        renderWorld = std::move(freshRender);
        realExtrapWorld = std::move(localReal);
    }
}

std::shared_ptr<World> HaxballClient::getWorld() {
    if (status == ConnectionStatus::Connected) {
        rebuildRenderWorld();
    }
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    return renderWorld;
}

std::shared_ptr<World> HaxballClient::getRealExtrapWorld() const {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    return realExtrapWorld;
}

std::shared_ptr<World> HaxballClient::getAuthoritativeWorld() const {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    return baseWorld;
}

bool HaxballClient::isLocalAdmin() const {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (!baseWorld) return false;
    if (const auto* p = baseWorld->getPlayer(myPlayerId)) {
        return p->admin;
    }
    return false;
}

bool HaxballClient::canChangeOwnTeam() const {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (!baseWorld) return false;
    if (const auto* p = baseWorld->getPlayer(myPlayerId)) {
        if (p->admin) return true;
    }
    return !baseWorld->teamsLocked && !baseWorld->hasMatch;
}

void HaxballClient::sendPing() {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (status != ConnectionStatus::Connected || !dcs[2] || !dcs[2]->isOpen()) return;
    lastPingTime = std::chrono::steady_clock::now();
    initialPingsSent++;
    double nowMs = getNowMs();
    uint32_t reportedPing = static_cast<uint32_t>(std::max(0, pingMs));
    switch (fakePingMode) {
        case FakePingMode::Static:
            if (fakePingMs.has_value()) reportedPing = *fakePingMs;
            break;
        case FakePingMode::Flash:
            if (!fakePingList.empty()) {
                reportedPing = fakePingList[fakePingStep % fakePingList.size()];
                fakePingStep++;
            }
            break;
        case FakePingMode::Random: {
            uint64_t lo = std::min(fakePingMin, fakePingMax);
            uint64_t hi = std::max(fakePingMin, fakePingMax);
            uint64_t span = (hi - lo) + 1ULL;
            uint64_t h = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ (static_cast<uint64_t>(fakePingStep) * 2654435761ULL);
            h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
            reportedPing = static_cast<uint32_t>(lo + (h % span));
            fakePingStep++;
            break;
        }
        case FakePingMode::Wave: {
            uint64_t lo = std::min(fakePingMin, fakePingMax);
            uint64_t hi = std::max(fakePingMin, fakePingMax);
            double phase = (std::sin(fakePingStep * 0.45) + 1.0) * 0.5;
            reportedPing = static_cast<uint32_t>(lo + static_cast<uint64_t>(std::llround(phase * static_cast<double>(hi - lo))));
            fakePingStep++;
            break;
        }
        case FakePingMode::Count: {
            int64_t lo64 = static_cast<int64_t>(fakePingMin);
            int64_t hi64 = static_cast<int64_t>(fakePingMax);
            int64_t span = std::llabs(hi64 - lo64);
            int64_t stepSize = std::max<int64_t>(1, span / 20);
            int64_t dir = (hi64 >= lo64) ? 1 : -1;
            int64_t totalSteps = std::max<int64_t>(1, span / stepSize + 1);
            int64_t cur = lo64 + dir * ((static_cast<int64_t>(fakePingStep) % totalSteps) * stepSize);
            reportedPing = static_cast<uint32_t>(std::clamp(cur, std::min(lo64, hi64), std::max(lo64, hi64)));
            fakePingStep++;
            break;
        }
        case FakePingMode::Off:
        default:
            break;
    }
    BufferWriter a;
    a.writeU8(2);
    a.writeF64BE(nowMs);
    a.writeVarInt(reportedPing);
    dcs[2]->send((const std::byte*)a.buf.data(), a.buf.size());
}

void HaxballClient::update() {
    if (status != ConnectionStatus::Connected) return;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        double nowMs = getNowMs();
        double offset = clockOffsetFilter.empty() ? ((double)currentFrame + 10.0 - nowMs * 0.06) : clockOffsetFilter.quantile(0.5);
        dD = std::clamp(nowMs * 0.06 + offset - (double)currentFrame, 0.0, 120.0);
    }
    auto now = std::chrono::steady_clock::now();
    bool isDynamicFake = (fakePingMode != FakePingMode::Off && fakePingMode != FakePingMode::Static);
    int intervalMs = (initialPingsSent < 5) ? 200 : (isDynamicFake ? fakePingIntervalMs : 1000);
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPingTime).count() >= intervalMs) {
        sendPing();
    }
}

void HaxballClient::sendAction(uint8_t eventType, bool isTimed, bool isPredicted, const std::function<void(BufferWriter&)>& writePayload, const GameAction* localPred, std::optional<uint32_t> explicitFrame) {
    std::lock_guard<std::recursive_mutex> lock(clientMutex);
    if (status != ConnectionStatus::Connected || !dcs[0] || !dcs[0]->isOpen()) return;

    uint32_t seq = actionSequence++;
    uint32_t targetFrame = 0;
    if (isTimed) {
        double nowMs = getNowMs();
        double offset = computeOffsetLocked(nowMs);
        dD = std::clamp(nowMs * 0.06 + offset - (double)currentFrame, 0.0, 120.0);
        if (explicitFrame.has_value()) {
            targetFrame = std::max(*explicitFrame, currentFrame);
        } else {
            targetFrame = computeTargetFrameLocked(nowMs);
        }
    }

    BufferWriter u;
    u.writeU8(1);
    u.writeU32BE(targetFrame);
    u.writeU32BE(seq);
    u.writeU8(eventType);
    if (writePayload) writePayload(u);
    dcs[0]->send((const std::byte*)u.buf.data(), u.buf.size());

    if (isPredicted && localPred) {
        GameAction pred = *localPred;
        pred.frame = targetFrame;
        pred.clientSeq = seq;
        pred.senderId = (uint16_t)myPlayerId;
        pred.eventType = eventType;
        pred.isPredicted = true;
        pred.isTimed = isTimed;

        if (localPredictions.size() >= 256) {
            localPredictions.erase(localPredictions.begin(), localPredictions.begin() + 128);
        }
        auto it = std::upper_bound(localPredictions.begin(), localPredictions.end(), pred,
            [](const GameAction& a, const GameAction& b) { return a.frame < b.frame; });
        localPredictions.insert(it, std::move(pred));
    }
}

void HaxballClient::sendInput(uint32_t keyMask, std::optional<uint32_t> explicitFrame, bool isOneTickPulse) {
    if (status != ConnectionStatus::Connected) return;
    if (keyMask == lastSentKeyMask) return;
    lastSentKeyMask = keyMask;

    GameAction pred;
    pred.u32Val = keyMask;
    sendAction(3, true, true, [keyMask](BufferWriter& u) {
        u.writeU32BE(keyMask);
    }, &pred, explicitFrame);

    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (isOneTickPulse && (keyMask & 16u) != 0 && !localPredictions.empty()) {
            predictedPulseReleaseFrame = localPredictions.back().frame + 1;
        } else if ((keyMask & 16u) == 0) {
            predictedPulseReleaseFrame = 0;
        }
    }
}

void HaxballClient::sendChat(const std::string& msg) {
    if (msg.empty()) return;
    std::string clipped = msg.substr(0, 140);
    sendAction(4, false, false, [&clipped](BufferWriter& u) {
        u.writeStr(clipped);
    });
}

void HaxballClient::sendChatIndicator(bool active) {
    if (active == lastSentChatIndicator) return;
    lastSentChatIndicator = active;
    sendAction(1, false, false, [active](BufferWriter& u) {
        u.writeU8(active ? 0 : 1);
    });
}

void HaxballClient::setAvatar(const std::string& avatar) {

    std::string clipped;
    size_t i = 0, utf16Units = 0;
    while (i < avatar.size()) {
        uint8_t b = static_cast<uint8_t>(avatar[i]);
        size_t len = 1;
        size_t u16 = 1;
        if ((b & 0x80) == 0) { len = 1; u16 = 1; }
        else if ((b & 0xE0) == 0xC0) { len = 2; u16 = 1; }
        else if ((b & 0xF0) == 0xE0) {
            len = 3;

            u16 = 1;
        } else if ((b & 0xF8) == 0xF0) {
            len = 4;
            u16 = 2;
        }
        if (i + len > avatar.size()) break;

        if (len == 3 && static_cast<uint8_t>(avatar[i]) == 0xEF &&
            static_cast<uint8_t>(avatar[i + 1]) == 0xB8 &&
            (static_cast<uint8_t>(avatar[i + 2]) >= 0x80 && static_cast<uint8_t>(avatar[i + 2]) <= 0x8F)) {
            i += 3;
            continue;
        }
        if (utf16Units + u16 > 2) break;
        clipped.append(avatar, i, len);
        utf16Units += u16;
        i += len;
    }
    playerAvatar = clipped;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (baseWorld) {
            if (auto* p = baseWorld->getPlayer(myPlayerId)) {
                p->avatar = clipped;
            }
        }
    }
    sendAction(18, false, false, [&clipped](BufferWriter& u) {
        u.writeOptStr(&clipped);
    });
}

void HaxballClient::setTeam(int teamId) {
    if (!canChangeOwnTeam()) return;
    setPlayerTeam(myPlayerId, teamId);
}

void HaxballClient::setPlayerTeam(uint32_t playerId, int teamId) {
    if (!isLocalAdmin() && (playerId != myPlayerId || !canChangeOwnTeam())) return;
    sendAction(12, false, false, [playerId, teamId](BufferWriter& u) {
        u.writeI32BE((int32_t)playerId);
        u.writeI8((int8_t)teamId);
    });
}

void HaxballClient::startGame() {
    if (!isLocalAdmin()) return;
    sendAction(7, false, false, nullptr);
}

void HaxballClient::stopGame() {
    if (!isLocalAdmin()) return;
    sendAction(8, false, false, nullptr);
}

void HaxballClient::pauseGame() {
    if (!isLocalAdmin()) return;
    bool shouldPause = true;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (!baseWorld || !baseWorld->hasMatch) return;
        shouldPause = (baseWorld->pauseTickCounter != 120);
    }
    GameAction pred;
    pred.boolVal = shouldPause;
    sendAction(9, true, true, [shouldPause](BufferWriter& u) {
        u.writeU8(shouldPause ? 1 : 0);
    }, &pred);
}

void HaxballClient::autoTeams() {
    if (!isLocalAdmin()) return;
    sendAction(15, false, false, nullptr);
}

void HaxballClient::toggleLockTeams() {
    if (!isLocalAdmin()) return;
    bool newLock = true;
    {
        std::lock_guard<std::recursive_mutex> lock(clientMutex);
        if (baseWorld) newLock = !baseWorld->teamsLocked;
    }
    sendAction(13, false, false, [newLock](BufferWriter& u) {
        u.writeU8(newLock ? 1 : 0);
    });
}

}
