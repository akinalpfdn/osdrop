// Runs the published Noise_XX_25519_ChaChaPoly_BLAKE2b vectors (cacophony and noise-c, see
// core/tests/data/extract_noise_vectors.py): fixed static and ephemeral keys must reproduce every
// handshake and transport ciphertext and the handshake hash, byte for byte.
#include "crypto/noise.hpp"
#include "crypto_test_support.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace crypto = osdrop::crypto;
using osdrop::test::from_hex;
using osdrop::test::key_from_hex;

struct Vector {
    std::string source;
    std::map<std::string, std::string> fields;
    std::vector<std::pair<std::string, std::string>> messages; // payload, ciphertext
};

std::vector<Vector> load_vectors() {
    std::ifstream file(OSDROP_TEST_DATA_DIR "/noise_xx_vectors.txt");
    std::vector<Vector> vectors;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "vector") {
            vectors.emplace_back();
            words >> vectors.back().source;
        } else if (key == "message") {
            std::string payload;
            std::string ciphertext;
            words >> payload >> ciphertext;
            vectors.back().messages.emplace_back(payload, ciphertext);
        } else if (key != "end") {
            words >> vectors.back().fields[key];
        }
    }
    return vectors;
}

crypto::KeyPair pair_from(const std::string& secret_hex) {
    auto pair = crypto::KeyPair::from_secret(key_from_hex(secret_hex));
    if (!pair) {
        throw std::runtime_error("invalid vector key");
    }
    return pair.value();
}

TEST(NoiseVectors, PublishedXxVectorsReproduceByteForByte) {
    const auto vectors = load_vectors();
    ASSERT_EQ(vectors.size(), 2U);
    for (const Vector& vector : vectors) {
        SCOPED_TRACE(vector.source);
        ASSERT_EQ(vector.messages.size(), 6U);
        const auto prologue = from_hex(vector.fields.at("init_prologue"));

        osdrop::test::ScriptedRandom initiator_random;
        osdrop::test::ScriptedRandom responder_random;
        initiator_random.push(from_hex(vector.fields.at("init_ephemeral")));
        responder_random.push(from_hex(vector.fields.at("resp_ephemeral")));
        crypto::HandshakeState initiator(crypto::HandshakeRole::Initiator,
                                         pair_from(vector.fields.at("init_static")), prologue,
                                         initiator_random);
        crypto::HandshakeState responder(crypto::HandshakeRole::Responder,
                                         pair_from(vector.fields.at("resp_static")), prologue,
                                         responder_random);

        // Handshake: messages 0 and 2 from the initiator, 1 from the responder.
        for (std::size_t index = 0; index < 3; ++index) {
            SCOPED_TRACE(index);
            auto& writer = index % 2 == 0 ? initiator : responder;
            auto& reader = index % 2 == 0 ? responder : initiator;
            const auto payload = from_hex(vector.messages[index].first);
            const auto message = writer.write_message(payload);
            ASSERT_TRUE(message.has_value());
            EXPECT_EQ(message.value(), from_hex(vector.messages[index].second));
            const auto received = reader.read_message(message.value());
            ASSERT_TRUE(received.has_value());
            EXPECT_EQ(received.value(), payload);
        }
        ASSERT_TRUE(initiator.is_complete());
        ASSERT_TRUE(responder.is_complete());
        const auto expected_hash = from_hex(vector.fields.at("handshake_hash"));
        EXPECT_EQ(std::vector<std::uint8_t>(initiator.handshake_hash().begin(),
                                            initiator.handshake_hash().end()),
                  expected_hash);
        EXPECT_EQ(responder.handshake_hash(), initiator.handshake_hash());

        // Transport: messages continue alternating, starting with the responder.
        auto initiator_keys = initiator.finish();
        auto responder_keys = responder.finish();
        ASSERT_TRUE(initiator_keys.has_value());
        ASSERT_TRUE(responder_keys.has_value());
        for (std::size_t index = 3; index < vector.messages.size(); ++index) {
            SCOPED_TRACE(index);
            const bool from_responder = index % 2 == 1;
            auto& send = from_responder ? responder_keys.value().control_send
                                        : initiator_keys.value().control_send;
            auto& receive = from_responder ? initiator_keys.value().control_receive
                                           : responder_keys.value().control_receive;
            const auto payload = from_hex(vector.messages[index].first);
            const auto ciphertext = send.encrypt_with_ad({}, payload);
            ASSERT_TRUE(ciphertext.has_value());
            EXPECT_EQ(ciphertext.value(), from_hex(vector.messages[index].second));
            const auto plaintext = receive.decrypt_with_ad({}, ciphertext.value());
            ASSERT_TRUE(plaintext.has_value());
            EXPECT_EQ(plaintext.value(), payload);
        }
    }
}

} // namespace
