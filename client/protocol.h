#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace lightnote {

constexpr size_t kMaxFramePayloadBytes = 64 * 1024;

enum class MessageType {
    Hello,
    Join,
    JoinOk,
    JoinFail,
    Insert,
    Delete,
    Apply,
    SyncRequest,
    SyncResponse,
    Ping,
    Pong,
    Error,
    Unknown,
};

enum class ProtocolError {
    None,
    FrameTooLarge,
    InvalidUtf8,
    InvalidJson,
    MissingRequiredField,
    InvalidMessageType,
};

struct ProtocolMessage {
    MessageType type = MessageType::Unknown;
    std::wstring session;
    std::wstring client;
    std::map<std::wstring, std::wstring> strings;
    std::map<std::wstring, uint64_t> numbers;

    void SetString(std::wstring key, std::wstring value);
    void SetNumber(std::wstring key, uint64_t value);
    bool GetString(const std::wstring& key, std::wstring* value) const;
    bool GetNumber(const std::wstring& key, uint64_t* value) const;
};

std::wstring MessageTypeToString(MessageType type);
MessageType MessageTypeFromString(std::wstring_view value);
std::wstring ProtocolErrorToString(ProtocolError error);

bool SerializeMessage(const ProtocolMessage& message, std::string* utf8_json, ProtocolError* error);
bool DeserializeMessage(std::string_view utf8_json, ProtocolMessage* message, ProtocolError* error);

std::string EncodeFrame(std::string_view payload);

class FrameDecoder {
public:
    bool Append(std::string_view bytes, ProtocolError* error);
    bool TryReadFrame(std::string* payload, ProtocolError* error);
    void Clear();

private:
    std::string buffer_;
};

}  // namespace lightnote

