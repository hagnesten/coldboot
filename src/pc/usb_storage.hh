#pragma once

#include "pc/usb.hh"
#include "pc/storage.hh"

namespace pc {

	class usb_storage : public usb_device {

	public:

		static constexpr uint32_t block_size = 512;
		static constexpr uint32_t buffer_size = 65536;
		static constexpr int32_t packet = 64;
		static constexpr int32_t fast_packet = 512;

		usb_storage ( );

		~usb_storage ( ) override;

		void attach ( disk_file* target ) {

			disk = target;

		}

		uint64_t commands_run = 0;
		uint64_t blocks_moved = 0;

	protected:

		int32_t descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) override;

		int32_t request ( const uint8_t* setup, uint8_t* reply, int32_t room ) override;

		int32_t poll ( uint8_t endpoint, uint8_t* data, int32_t room ) override;

		int32_t receive ( uint8_t endpoint, const uint8_t* data, int32_t length ) override;

		void clear_halt ( uint8_t endpoint ) override;

		void restart ( ) override;

	private:

		enum phase : uint8_t {

			WAIT_COMMAND,
			DATA_IN,
			DATA_OUT,
			SEND_STATUS,
			HALTED_IN,
			HALTED_OUT,

		};

		void command ( const uint8_t* block );

		void fail ( uint8_t key, uint8_t code );

		void reply ( uint32_t length );

		bool present ( ) const;

		disk_file* disk = nullptr;
		phase stage = WAIT_COMMAND;
		uint8_t tag [ 4 ] = { };
		uint32_t expected = 0;
		bool inbound = false;
		uint8_t* data = nullptr;
		uint32_t length = 0;
		uint32_t cursor = 0;
		uint8_t result = 0;
		uint8_t sense_key = 0;
		uint8_t sense_code = 0;
		uint64_t write_block = 0;
		uint8_t status [ 13 ] = { };

	};

}