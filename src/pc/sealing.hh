#pragma once

#include "base/base.hh"

namespace pc::sealing {

	static constexpr uint32_t trailer_size = 128;
	static constexpr uint32_t signed_head = 24;
	static constexpr uint32_t key_size = 32;
	static constexpr uint64_t magic = 0x4C4145534C45454Bull;

	enum class outcome {

		sealed,
		unsealed,
		stranger,
		broken,

	};

	struct digest {

		uint8_t bytes [ 64 ];

	};

	class sha512 {

	public:

		sha512 ( );

		void feed ( const uint8_t* data, uint64_t size );

		digest done ( );

	private:

		void block ( const uint8_t* data );

		uint64_t h [ 8 ];
		uint8_t buffer [ 128 ];
		uint64_t filled = 0;
		uint64_t length = 0;

	};

	bool verify ( const uint8_t* key, const uint8_t* first, uint64_t first_size, const uint8_t* second, uint64_t second_size, const uint8_t* signature );

	outcome check ( const uint8_t* file, uint64_t size, const uint8_t* keys, uint64_t keys_size );

	const char* describe ( outcome found );

	bool self_test ( );

}