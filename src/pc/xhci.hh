#pragma once

#include "pc/common.hh"
#include "pc/pci.hh"
#include "pc/usb.hh"

namespace pc {

	class xhci : public hv::device, public pci_function, public ticking {

	public:

		static constexpr uint32_t default_memory = 0xE1050000;
		static constexpr uint32_t memory_size = 0x10000;
		static constexpr int32_t port_count = 4;
		static constexpr uint32_t slot_count = 16;
		static constexpr uint32_t endpoint_count = 32;
		static constexpr uint32_t segment_room = 4;

		xhci ( platform_bus& owner, hv::machine& target, uint32_t line_number );

		void plug ( int32_t port, usb_device* device );

		uint64_t read ( uint64_t address, uint32_t size ) override;

		void write ( uint64_t address, uint32_t size, uint64_t value ) override;

		void written ( uint32_t offset ) override;

		void advance ( uint64_t now ) override;

		uint32_t memory_base = default_memory;
		uint64_t commands_run = 0;
		uint64_t transfers_run = 0;
		uint64_t events_posted = 0;

	private:

		struct trb {

			uint64_t parameter = 0;
			uint32_t status = 0;
			uint32_t control = 0;

		};

		struct ring {

			uint64_t dequeue = 0;
			bool cycle = true;

		};

		struct endpoint_state {

			uint8_t state = 0;
			uint8_t type = 0;
			uint16_t max_packet = 8;
			uint32_t progress = 0;
			bool skipping = false;
			ring transfer;

		};

		struct slot_state {

			bool enabled = false;
			uint8_t state = 0;
			uint8_t address = 0;
			int32_t port = -1;
			endpoint_state endpoints [ endpoint_count ];

		};

		void reset ( );

		uint32_t register_read ( uint32_t offset );

		void register_write ( uint32_t offset, uint32_t value );

		uint32_t port_read ( int32_t port ) const;

		void port_write ( int32_t port, uint32_t value );

		uint32_t read32 ( uint64_t address );

		void write32 ( uint64_t address, uint32_t value );

		uint64_t read64 ( uint64_t address );

		trb fetch ( uint64_t address );

		bool peek ( ring& item, trb& out, uint64_t& at );

		void run_commands ( );

		uint32_t run_command ( const trb& item, uint32_t& slot );

		uint32_t enable_slot ( uint32_t& slot );

		uint32_t address_device ( uint32_t slot, uint64_t input, bool block );

		uint32_t configure_endpoints ( uint32_t slot, uint64_t input, bool deconfigure );

		uint32_t evaluate_context ( uint32_t slot, uint64_t input );

		uint32_t reset_endpoint ( uint32_t slot, uint32_t target );

		uint32_t stop_endpoint ( uint32_t slot, uint32_t target );

		uint32_t set_dequeue ( uint32_t slot, uint32_t target, uint64_t pointer );

		void ring_doorbell ( uint32_t slot, uint32_t target );

		void run_endpoint ( uint32_t slot, uint32_t target );

		int32_t move_data ( usb_device* device, uint32_t endpoint, const trb& item, endpoint_state& state );

		void post ( const trb& event );

		void transfer_event ( uint64_t at, uint32_t code, uint32_t residual, uint32_t slot, uint32_t target, bool data );

		void port_event ( int32_t port );

		uint64_t output_context ( uint32_t slot );

		void set_endpoint_state ( uint32_t slot, uint32_t target, uint8_t state );

		usb_device* device_of ( uint32_t slot );

		void load_segment ( );

		void update_line ( );

		platform_bus& bus;
		hv::machine& vm;
		uint32_t irq = 21;
		bool line = false;
		uint32_t command = 0;
		uint32_t status = 1;
		uint32_t notification = 0;
		uint32_t configured_slots = 0;
		uint64_t device_contexts = 0;
		ring commands;
		bool command_running = false;
		uint32_t legacy = 0;
		uint32_t ports [ port_count ] = { };
		usb_device* devices [ port_count ] = { };
		uint32_t management = 0;
		uint32_t moderation = 0;
		uint32_t table_size = 0;
		uint64_t table_base = 0;
		uint64_t dequeue_pointer = 0;
		uint32_t segment_index = 0;
		uint64_t segment_base = 0;
		uint32_t segment_size = 0;
		uint32_t enqueue = 0;
		bool producer_cycle = true;
		uint64_t last_retry = 0;
		slot_state slots [ slot_count + 1 ];
		uint32_t waiting [ slot_count + 1 ] = { };
		uint32_t rung [ slot_count + 1 ] = { };
		uint8_t scratch [ 1024 ] = { };

	};

}