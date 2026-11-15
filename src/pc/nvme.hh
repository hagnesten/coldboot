#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"
#include "pc/storage.hh"

namespace pc {

	class nvme : public hv::device, public pci_function, public ticking {

	public:

		static constexpr uint32_t default_memory = 0xE1040000;
		static constexpr uint32_t memory_size = 0x4000;
		static constexpr uint32_t queue_count = 16;
		static constexpr uint32_t queue_entries = 1024;
		static constexpr uint32_t page_size = 4096;
		static constexpr uint32_t sector_size = 512;
		static constexpr uint32_t transfer_pages = 32;
		static constexpr uint32_t transfer_limit = transfer_pages * page_size;

		nvme ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		~nvme ( ) override;

		void attach ( disk_file* target ) {

			disk = target;

		}

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void advance ( uint64_t now ) override;

		uint32_t memory_base = default_memory;
		uint32_t commands_run = 0;
		uint64_t blocks_moved = 0;

	private:

		struct submission {

			bool used = false;
			uint64_t base = 0;
			uint32_t size = 0;
			uint32_t head = 0;
			uint32_t tail = 0;
			uint16_t completion = 0;

		};

		struct completion {

			bool used = false;
			uint64_t base = 0;
			uint32_t size = 0;
			uint32_t head = 0;
			uint32_t tail = 0;
			bool phase = true;
			bool interrupts = false;

		};

		struct result {

			uint32_t value = 0;
			uint16_t status = 0;
			bool post = true;

		};

		void reset ( );

		void enable ( );

		uint32_t register_read ( uint32_t offset ) const;

		void register_write ( uint32_t offset, uint32_t value );

		void ring ( uint32_t doorbell, uint32_t value );

		void run_queue ( uint32_t queue );

		result run_admin ( const uint8_t* command );

		result run_io ( const uint8_t* command );

		bool move ( uint64_t first, uint64_t second, uint32_t length, bool to_guest );

		void describe_controller ( uint8_t* out ) const;

		void describe_namespace ( uint8_t* out ) const;

		void post ( uint32_t queue, uint16_t identifier, const result& done );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		disk_file* disk = nullptr;
		uint32_t irq = 20;
		bool line = false;
		uint32_t control = 0;
		uint32_t status = 0;
		uint32_t attributes = 0;
		uint64_t admin_submission = 0;
		uint64_t admin_completion = 0;
		uint32_t masked = 0;
		uint32_t granted = queue_count - 1;
		uint32_t rung = 0;
		submission submissions [ queue_count ];
		completion completions [ queue_count ];
		uint8_t* buffer = nullptr;

	};

}