#include "protocol.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

}  // namespace

int wmain() {
    lightnote::ProtocolError error = lightnote::ProtocolError::None;

    lightnote::ProtocolMessage insert;
    insert.type = lightnote::MessageType::Insert;
    insert.session = L"482913";
    insert.client = L"A";
    insert.SetNumber(L"base_rev", 104);
    insert.SetNumber(L"pos", 12);
    insert.SetString(L"text", L"안녕하세요");

    std::string json;
    if (!Expect(lightnote::SerializeMessage(insert, &json, &error), L"serialize failed")) return 1;

    lightnote::ProtocolMessage parsed;
    if (!Expect(lightnote::DeserializeMessage(json, &parsed, &error), L"deserialize failed")) return 1;
    std::wstring text;
    uint64_t pos = 0;
    if (!Expect(parsed.type == lightnote::MessageType::Insert, L"type mismatch")) return 1;
    if (!Expect(parsed.session == L"482913" && parsed.client == L"A", L"common fields mismatch")) return 1;
    if (!Expect(parsed.GetString(L"text", &text) && text == L"안녕하세요", L"text field mismatch")) return 1;
    if (!Expect(parsed.GetNumber(L"pos", &pos) && pos == 12, L"number field mismatch")) return 1;

    const std::string frame = lightnote::EncodeFrame(json);
    lightnote::FrameDecoder decoder;
    std::string payload;
    if (!Expect(decoder.Append(std::string_view(frame.data(), 2), &error), L"append first split failed")) return 1;
    if (!Expect(!decoder.TryReadFrame(&payload, &error), L"partial frame should not decode")) return 1;
    if (!Expect(decoder.Append(std::string_view(frame.data() + 2, frame.size() - 2), &error), L"append second split failed")) return 1;
    if (!Expect(decoder.TryReadFrame(&payload, &error) && payload == json, L"split frame decode failed")) return 1;

    const std::string joined = frame + frame;
    if (!Expect(decoder.Append(joined, &error), L"append joined frames failed")) return 1;
    if (!Expect(decoder.TryReadFrame(&payload, &error) && payload == json, L"first joined frame failed")) return 1;
    if (!Expect(decoder.TryReadFrame(&payload, &error) && payload == json, L"second joined frame failed")) return 1;
    if (!Expect(!decoder.TryReadFrame(&payload, &error), L"decoder should be empty")) return 1;

    lightnote::ProtocolMessage bad;
    if (!Expect(!lightnote::DeserializeMessage("{\"session\":\"482913\",\"client\":\"A\"}", &bad, &error) &&
                    error == lightnote::ProtocolError::InvalidMessageType,
                L"missing type should fail")) return 1;

    if (!Expect(!lightnote::DeserializeMessage("{\"type\":\"NOPE\",\"session\":\"482913\",\"client\":\"A\"}", &bad, &error) &&
                    error == lightnote::ProtocolError::InvalidMessageType,
                L"invalid type should fail")) return 1;

    if (!Expect(!lightnote::DeserializeMessage("{\"type\":\"PING\",\"session\":\"482913\",", &bad, &error) &&
                    error == lightnote::ProtocolError::InvalidJson,
                L"broken json should fail")) return 1;

    std::string oversized_header(4, '\0');
    const uint32_t oversized = static_cast<uint32_t>(lightnote::kMaxFramePayloadBytes + 1);
    oversized_header[0] = static_cast<char>((oversized >> 24) & 0xFF);
    oversized_header[1] = static_cast<char>((oversized >> 16) & 0xFF);
    oversized_header[2] = static_cast<char>((oversized >> 8) & 0xFF);
    oversized_header[3] = static_cast<char>(oversized & 0xFF);
    if (!Expect(decoder.Append(oversized_header, &error), L"oversized header append should fit")) return 1;
    if (!Expect(!decoder.TryReadFrame(&payload, &error) && error == lightnote::ProtocolError::FrameTooLarge,
                L"oversized frame should fail")) return 1;

    std::wcout << L"protocol smoke passed\n";
    return 0;
}

