#include "protocol.h"

#include "text_encoding.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace lightnote {
namespace {

bool IsWhitespace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

void SkipWhitespace(std::wstring_view text, size_t* index) {
    while (*index < text.size() && IsWhitespace(text[*index])) {
        ++(*index);
    }
}

bool ParseJsonString(std::wstring_view text, size_t* index, std::wstring* value) {
    value->clear();
    if (*index >= text.size() || text[*index] != L'"') {
        return false;
    }

    ++(*index);
    while (*index < text.size()) {
        const wchar_t ch = text[*index];
        ++(*index);

        if (ch == L'"') {
            return true;
        }

        if (ch != L'\\') {
            value->push_back(ch);
            continue;
        }

        if (*index >= text.size()) {
            return false;
        }

        const wchar_t escaped = text[*index];
        ++(*index);
        switch (escaped) {
            case L'"':
            case L'\\':
            case L'/':
                value->push_back(escaped);
                break;
            case L'b':
                value->push_back(L'\b');
                break;
            case L'f':
                value->push_back(L'\f');
                break;
            case L'n':
                value->push_back(L'\n');
                break;
            case L'r':
                value->push_back(L'\r');
                break;
            case L't':
                value->push_back(L'\t');
                break;
            default:
                return false;
        }
    }

    return false;
}

bool ParseUnsigned(std::wstring_view text, size_t* index, uint64_t* value) {
    if (*index >= text.size() || !iswdigit(text[*index])) {
        return false;
    }

    uint64_t result = 0;
    while (*index < text.size() && iswdigit(text[*index])) {
        const uint64_t digit = static_cast<uint64_t>(text[*index] - L'0');
        if (result > (UINT64_MAX - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
        ++(*index);
    }

    *value = result;
    return true;
}

std::wstring EscapeJsonString(std::wstring_view value) {
    std::wstring output;
    output.reserve(value.size() + 8);

    for (const wchar_t ch : value) {
        switch (ch) {
            case L'"':
                output += L"\\\"";
                break;
            case L'\\':
                output += L"\\\\";
                break;
            case L'\b':
                output += L"\\b";
                break;
            case L'\f':
                output += L"\\f";
                break;
            case L'\n':
                output += L"\\n";
                break;
            case L'\r':
                output += L"\\r";
                break;
            case L'\t':
                output += L"\\t";
                break;
            default:
                output.push_back(ch);
                break;
        }
    }

    return output;
}

void AppendJsonStringField(std::wostringstream* builder, const std::wstring& key, const std::wstring& value, bool* first) {
    if (!*first) {
        *builder << L",";
    }
    *first = false;
    *builder << L"\"" << EscapeJsonString(key) << L"\":\"" << EscapeJsonString(value) << L"\"";
}

void AppendJsonNumberField(std::wostringstream* builder, const std::wstring& key, uint64_t value, bool* first) {
    if (!*first) {
        *builder << L",";
    }
    *first = false;
    *builder << L"\"" << EscapeJsonString(key) << L"\":" << value;
}

bool IsCommonField(const std::wstring& key) {
    return key == L"type" || key == L"session" || key == L"client";
}

uint32_t ReadBigEndianLength(std::string_view bytes) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(bytes[0])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(bytes[3]));
}

void SetError(ProtocolError* error, ProtocolError value) {
    if (error != nullptr) {
        *error = value;
    }
}

}  // namespace

void ProtocolMessage::SetString(std::wstring key, std::wstring value) {
    if (key == L"type") {
        type = MessageTypeFromString(value);
    } else if (key == L"session") {
        session = value;
    } else if (key == L"client") {
        client = value;
    }

    strings[std::move(key)] = std::move(value);
}

void ProtocolMessage::SetNumber(std::wstring key, uint64_t value) {
    numbers[std::move(key)] = value;
}

bool ProtocolMessage::GetString(const std::wstring& key, std::wstring* value) const {
    const auto found = strings.find(key);
    if (found == strings.end()) {
        return false;
    }

    *value = found->second;
    return true;
}

bool ProtocolMessage::GetNumber(const std::wstring& key, uint64_t* value) const {
    const auto found = numbers.find(key);
    if (found == numbers.end()) {
        return false;
    }

    *value = found->second;
    return true;
}

std::wstring MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::Hello:
            return L"HELLO";
        case MessageType::Join:
            return L"JOIN";
        case MessageType::JoinOk:
            return L"JOIN_OK";
        case MessageType::JoinFail:
            return L"JOIN_FAIL";
        case MessageType::Insert:
            return L"INSERT";
        case MessageType::Delete:
            return L"DELETE";
        case MessageType::Apply:
            return L"APPLY";
        case MessageType::SyncRequest:
            return L"SYNC_REQUEST";
        case MessageType::SyncResponse:
            return L"SYNC_RESPONSE";
        case MessageType::Ping:
            return L"PING";
        case MessageType::Pong:
            return L"PONG";
        case MessageType::Error:
            return L"ERROR";
        default:
            return L"UNKNOWN";
    }
}

MessageType MessageTypeFromString(std::wstring_view value) {
    if (value == L"HELLO") return MessageType::Hello;
    if (value == L"JOIN") return MessageType::Join;
    if (value == L"JOIN_OK") return MessageType::JoinOk;
    if (value == L"JOIN_FAIL") return MessageType::JoinFail;
    if (value == L"INSERT") return MessageType::Insert;
    if (value == L"DELETE") return MessageType::Delete;
    if (value == L"APPLY") return MessageType::Apply;
    if (value == L"SYNC_REQUEST") return MessageType::SyncRequest;
    if (value == L"SYNC_RESPONSE") return MessageType::SyncResponse;
    if (value == L"PING") return MessageType::Ping;
    if (value == L"PONG") return MessageType::Pong;
    if (value == L"ERROR") return MessageType::Error;
    return MessageType::Unknown;
}

std::wstring ProtocolErrorToString(ProtocolError error) {
    switch (error) {
        case ProtocolError::None:
            return L"none";
        case ProtocolError::FrameTooLarge:
            return L"frame_too_large";
        case ProtocolError::InvalidUtf8:
            return L"invalid_utf8";
        case ProtocolError::InvalidJson:
            return L"invalid_json";
        case ProtocolError::MissingRequiredField:
            return L"missing_required_field";
        case ProtocolError::InvalidMessageType:
            return L"invalid_message_type";
        default:
            return L"unknown";
    }
}

bool SerializeMessage(const ProtocolMessage& message, std::string* utf8_json, ProtocolError* error) {
    if (message.type == MessageType::Unknown) {
        SetError(error, ProtocolError::InvalidMessageType);
        return false;
    }

    if (message.session.empty() || message.client.empty()) {
        SetError(error, ProtocolError::MissingRequiredField);
        return false;
    }

    std::wostringstream builder;
    builder << L"{";

    bool first = true;
    AppendJsonStringField(&builder, L"type", MessageTypeToString(message.type), &first);
    AppendJsonStringField(&builder, L"session", message.session, &first);
    AppendJsonStringField(&builder, L"client", message.client, &first);

    for (const auto& field : message.strings) {
        if (!IsCommonField(field.first)) {
            AppendJsonStringField(&builder, field.first, field.second, &first);
        }
    }

    for (const auto& field : message.numbers) {
        AppendJsonNumberField(&builder, field.first, field.second, &first);
    }

    builder << L"}";

    *utf8_json = WideToUtf8(builder.str());
    if (utf8_json->empty()) {
        SetError(error, ProtocolError::InvalidUtf8);
        return false;
    }

    if (utf8_json->size() > kMaxFramePayloadBytes) {
        SetError(error, ProtocolError::FrameTooLarge);
        return false;
    }

    SetError(error, ProtocolError::None);
    return true;
}

bool DeserializeMessage(std::string_view utf8_json, ProtocolMessage* message, ProtocolError* error) {
    message->type = MessageType::Unknown;
    message->session.clear();
    message->client.clear();
    message->strings.clear();
    message->numbers.clear();

    if (utf8_json.size() > kMaxFramePayloadBytes) {
        SetError(error, ProtocolError::FrameTooLarge);
        return false;
    }

    std::wstring text;
    if (!Utf8ToWide(utf8_json, &text)) {
        SetError(error, ProtocolError::InvalidUtf8);
        return false;
    }

    size_t index = 0;
    SkipWhitespace(text, &index);
    if (index >= text.size() || text[index] != L'{') {
        SetError(error, ProtocolError::InvalidJson);
        return false;
    }
    ++index;

    SkipWhitespace(text, &index);
    if (index < text.size() && text[index] == L'}') {
        SetError(error, ProtocolError::MissingRequiredField);
        return false;
    }

    while (index < text.size()) {
        std::wstring key;
        if (!ParseJsonString(text, &index, &key)) {
            SetError(error, ProtocolError::InvalidJson);
            return false;
        }

        SkipWhitespace(text, &index);
        if (index >= text.size() || text[index] != L':') {
            SetError(error, ProtocolError::InvalidJson);
            return false;
        }
        ++index;
        SkipWhitespace(text, &index);

        if (index < text.size() && text[index] == L'"') {
            std::wstring value;
            if (!ParseJsonString(text, &index, &value)) {
                SetError(error, ProtocolError::InvalidJson);
                return false;
            }
            message->SetString(std::move(key), std::move(value));
        } else {
            uint64_t value = 0;
            if (!ParseUnsigned(text, &index, &value)) {
                SetError(error, ProtocolError::InvalidJson);
                return false;
            }
            message->SetNumber(std::move(key), value);
        }

        SkipWhitespace(text, &index);
        if (index >= text.size()) {
            SetError(error, ProtocolError::InvalidJson);
            return false;
        }

        if (text[index] == L'}') {
            ++index;
            break;
        }

        if (text[index] != L',') {
            SetError(error, ProtocolError::InvalidJson);
            return false;
        }
        ++index;
        SkipWhitespace(text, &index);
        if (index >= text.size() || text[index] == L'}') {
            SetError(error, ProtocolError::InvalidJson);
            return false;
        }
    }

    SkipWhitespace(text, &index);
    if (index != text.size()) {
        SetError(error, ProtocolError::InvalidJson);
        return false;
    }

    if (message->type == MessageType::Unknown) {
        SetError(error, ProtocolError::InvalidMessageType);
        return false;
    }

    if (message->session.empty() || message->client.empty()) {
        SetError(error, ProtocolError::MissingRequiredField);
        return false;
    }

    SetError(error, ProtocolError::None);
    return true;
}

std::string EncodeFrame(std::string_view payload) {
    std::string frame;
    frame.resize(4);

    const uint32_t length = static_cast<uint32_t>(payload.size());
    frame[0] = static_cast<char>((length >> 24) & 0xFF);
    frame[1] = static_cast<char>((length >> 16) & 0xFF);
    frame[2] = static_cast<char>((length >> 8) & 0xFF);
    frame[3] = static_cast<char>(length & 0xFF);
    frame.append(payload.data(), payload.size());
    return frame;
}

bool FrameDecoder::Append(std::string_view bytes, ProtocolError* error) {
    if (buffer_.size() + bytes.size() > kMaxFramePayloadBytes + 4) {
        Clear();
        SetError(error, ProtocolError::FrameTooLarge);
        return false;
    }

    buffer_.append(bytes.data(), bytes.size());
    SetError(error, ProtocolError::None);
    return true;
}

bool FrameDecoder::TryReadFrame(std::string* payload, ProtocolError* error) {
    payload->clear();
    if (buffer_.size() < 4) {
        SetError(error, ProtocolError::None);
        return false;
    }

    const uint32_t length = ReadBigEndianLength(std::string_view(buffer_.data(), 4));
    if (length > kMaxFramePayloadBytes) {
        Clear();
        SetError(error, ProtocolError::FrameTooLarge);
        return false;
    }

    if (buffer_.size() < static_cast<size_t>(length) + 4) {
        SetError(error, ProtocolError::None);
        return false;
    }

    payload->assign(buffer_.data() + 4, length);
    buffer_.erase(0, static_cast<size_t>(length) + 4);
    SetError(error, ProtocolError::None);
    return true;
}

void FrameDecoder::Clear() {
    buffer_.clear();
}

}  // namespace lightnote
