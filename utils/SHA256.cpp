/*=====================================================================
SHA256.cpp
----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "SHA256.h"


#include "Exception.h"
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <assert.h>


SHA256::SHA256()
{

}


SHA256::~SHA256()
{

}


void SHA256::hash(
		const unsigned char* message_text_begin,
		const unsigned char* message_text_end,
		std::vector<unsigned char>& digest_out
	)
{
	SHA256_CTX context;
	digest_out.resize(SHA256_DIGEST_LENGTH);

	if(SHA256_Init(&context) == 0)
		throw glare::Exception("Hash init failed");

	if(message_text_end > message_text_begin)
		if(SHA256_Update(&context, message_text_begin, message_text_end - message_text_begin) == 0)
			throw glare::Exception("Hash update failed");

	if(SHA256_Final(&digest_out[0], &context) == 0)
		throw glare::Exception("Hash finalise failed");
}


void SHA256::SHA1Hash(
		const unsigned char* message_text_begin,
		const unsigned char* message_text_end,
		std::vector<unsigned char>& digest_out
	)
{
	SHA_CTX context;
	digest_out.resize(SHA_DIGEST_LENGTH);

	if(SHA1_Init(&context) == 0)
		throw glare::Exception("Hash init failed");

	if(message_text_end > message_text_begin)
		if(SHA1_Update(&context, message_text_begin, message_text_end - message_text_begin) == 0)
			throw glare::Exception("Hash update failed");

	if(SHA1_Final(&digest_out[0], &context) == 0)
		throw glare::Exception("Hash finalise failed");
}


void SHA256::SHA256HMAC(
	const ArrayRef<unsigned char>& key,
	const ArrayRef<unsigned char>& message,
	std::vector<unsigned char>& digest_out
)
{
	digest_out.resize(EVP_MAX_MD_SIZE);
	unsigned int result_len = 0;
	unsigned char* result = HMAC(EVP_sha256(), key.data(), (int)key.size(), message.data(), (int)message.size(), digest_out.data(), &result_len);
	if(result == NULL)
		throw glare::Exception("HMAC failed.");
	digest_out.resize(result_len);
}


#if BUILD_TESTS


#include "../utils/StringUtils.h"
#include "../utils/Timer.h"
#include "../maths/mathstypes.h"
#include "../utils/TestUtils.h"
#include "../utils/ConPrint.h"


static std::vector<unsigned char> stringToByteArray(const std::string& s)
{
	std::vector<unsigned char> bytes(s.size());
	for(size_t i=0; i<s.size(); ++i)
		bytes[i] = s[i];
	return bytes;
}


static std::vector<unsigned char> hexToByteArray(const std::string& s)
{
	std::vector<unsigned char> bytes(s.size() / 2);
	for(size_t i=0; i<s.size()/2; ++i)
	{
		unsigned int nibble_a = hexCharToUInt(s[i*2]);
		unsigned int nibble_b = hexCharToUInt(s[i*2 + 1]);
		bytes[i] = (unsigned char)((nibble_a << 4) + nibble_b);
	}
	return bytes;
}


void SHA256::test()
{
	//==================================== Test SHA-256 =======================================
	// Test against example hashes on wikipedia (http://en.wikipedia.org/wiki/SHA-2)

	// Test empty message
	{
		const std::vector<unsigned char> message;
		std::vector<unsigned char> digest;
		hash(message, digest);

		const std::string target_hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);

		testAssert(hash("") == target);
	}

	// Test "The quick brown fox jumps over the lazy dog"
	{
		const std::vector<unsigned char> message = stringToByteArray("The quick brown fox jumps over the lazy dog");
		std::vector<unsigned char> digest;
		hash(message, digest);

		const std::string target_hex = "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592";
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);
	}

	// Test "The quick brown fox jumps over the lazy dog."
	{
		const std::vector<unsigned char> message = stringToByteArray("The quick brown fox jumps over the lazy dog.");
		std::vector<unsigned char> digest;
		hash(message, digest);

		const std::string target_hex = "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c";
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);
	}

	// Do a negative test
	{
		const std::vector<unsigned char> message = stringToByteArray("AAAA The quick brown fox jumps over the lazy dog.");
		std::vector<unsigned char> digest;
		hash(message, digest);

		const std::string target_hex = "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c";
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest != target);
	}

	//==================================== Test SHA-1 =======================================
	// Test empty message
	{
		const std::vector<unsigned char> message;
		std::vector<unsigned char> digest;
		SHA1Hash(message.data(), message.data() + message.size(), digest);

		const std::string target_hex = "da39a3ee5e6b4b0d3255bfef95601890afd80709"; // From https://en.wikipedia.org/wiki/SHA-1
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);
	}

	// Test "The quick brown fox jumps over the lazy dog."
	{
		const std::vector<unsigned char> message = stringToByteArray("The quick brown fox jumps over the lazy dog");
		std::vector<unsigned char> digest;
		SHA1Hash(message.data(), message.data() + message.size(), digest);

		const std::string target_hex = "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"; // From https://en.wikipedia.org/wiki/SHA-1
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);
	}

	//==================================== Test SHA256HMAC =======================================
	{
		const std::vector<unsigned char> key = stringToByteArray("key");
		const std::vector<unsigned char> message = stringToByteArray("The quick brown fox jumps over the lazy dog");
		std::vector<unsigned char> digest;
		SHA256HMAC(key, message, digest);

		const std::string target_hex = "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"; // From https://en.wikipedia.org/wiki/HMAC
		const std::vector<unsigned char> target = hexToByteArray(target_hex);
		testAssert(digest == target);
	}
}


#endif
