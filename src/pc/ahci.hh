#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"
#include "pc/storage.hh"

namespace pc {

	class ahci : public hv::device, public pci_function {

	public:

		static constexpr uint32_t default_memory = 0xE1030000;
		static constexpr uint32_t memory_size = 0x2000;
		static constexpr uint32_t port_base = 0x100;
		static constexpr uint32_t port_stride = 0x80;
		static constexpr uint32_t slot_count = 32;
		static constexpr uint32_t sector_size = 512;
		static constexpr uint32_t transfer_limit = 0x400000;

		ahci ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		~ahci ( ) override;

		void attach ( disk_file* target ) {

			disk = target;
			reset_port ( );

		}

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		uint32_t memory_base = default_memory;
		uint32_t commands_run = 0;
		uint64_t sectors_moved = 0;

	private:

		struct port_state {

			uint64_t list = 0;
			uint64_t received = 0;
			uint32_t status = 0;
			uint32_t enable = 0;
			uint32_t command = 0;
			uint32_t task = 0x7F;
			uint32_t control = 0;
			uint32_t error = 0;
			uint32_t active = 0;
			uint32_t issued = 0;

		};

		void reset_controller ( );

		void reset_port ( );

		uint32_t port_read ( uint32_t offset );

		void port_write ( uint32_t offset, uint32_t value );

		void run_commands ( );

		bool run_slot ( uint32_t slot );

		bool transfer ( uint64_t table, uint32_t entries, uint64_t lba, uint32_t count, bool writing, uint32_t& moved );

		void describe_disk ( uint8_t* out ) const;

		void finish ( uint8_t status, uint8_t error, bool failed );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		disk_file* disk = nullptr;
		uint32_t irq = 18;
		bool line = false;
		uint32_t global_control = 0;
		uint32_t interrupt_status = 0;
		port_state port;
		uint8_t* buffer = nullptr;

	};

}