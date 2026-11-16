#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"
#include "pc/usb.hh"

namespace pc {

	class ehci : public hv::device, public pci_function, public ticking {

	public:

		static constexpr uint32_t default_memory = 0xE1060000;
		static constexpr uint32_t memory_size = 0x1000;
		static constexpr int32_t port_count = 4;

		ehci ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		void plug ( int32_t port, usb_device* device );

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void advance ( uint64_t now ) override;

		uint32_t memory_base = default_memory;
		uint64_t transfers_run = 0;
		uint64_t passes = 0;
		uint64_t periodic_frames = 0;

	private:

		enum outcome : uint8_t {

			OUTCOME_DONE,
			OUTCOME_SHORT,
			OUTCOME_WAITING,
			OUTCOME_HALTED,

		};

		void reset ( );

		uint32_t register_read ( uint32_t offset );

		void register_write ( uint32_t offset, uint32_t value );

		uint32_t port_read ( int32_t port ) const;

		void port_write ( int32_t port, uint32_t value );

		uint32_t read32 ( uint32_t address );

		void write32 ( uint32_t address, uint32_t value );

		void move ( uint32_t head, uint32_t page, uint32_t offset, uint8_t* data, uint32_t count, bool to_guest );

		usb_device* find ( uint8_t address );

		void run_async ( );

		void run_periodic ( );

		bool run_head ( uint32_t head );

		outcome execute ( uint32_t head, usb_device* device, uint8_t endpoint, uint32_t max_packet );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		uint32_t irq = 22;
		bool line = false;
		uint32_t command = 0;
		uint32_t status = 0;
		uint32_t enabled = 0;
		uint32_t frame = 0;
		uint32_t segment = 0;
		uint32_t periodic = 0;
		uint32_t async_list = 0;
		uint32_t configured = 0;
		uint32_t ports [ port_count ] = { };
		usb_device* devices [ port_count ] = { };
		uint64_t started_at = 0;
		uint32_t last_frame = 0;
		bool periodic_started = false;
		uint8_t scratch [ 1024 ] = { };

	};

}