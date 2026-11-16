#pragma once

#include "hv/machine.hh"
#include "pc/common.hh"

namespace pc {

	class pci_function {

	public:

		static constexpr uint8_t msi_id = 0x05;
		static constexpr uint8_t msix_id = 0x11;
		static constexpr uint32_t msi_window = 0xFEE00000;
		static constexpr uint32_t msix_room = 32;
		static constexpr uint32_t msix_entry = 16;

		virtual ~pci_function ( ) = default;

		void add_msi ( uint8_t offset );

		void add_msix ( uint8_t offset, uint32_t vectors, uint8_t bar, uint32_t table, uint32_t pending );

		bool msi_enabled ( ) const;

		bool msix_enabled ( ) const;

		bool send_msi ( hv::machine& target ) const;

		bool deliver ( hv::machine& target, uint32_t address, uint32_t high, uint32_t data ) const;

		void signal ( platform_bus& bus, hv::machine& target, uint32_t irq, bool wanted, bool& line );

		void signal_vector ( hv::machine& target, uint32_t vector, bool wanted, bool& line );

		bool msix_access ( uint32_t offset, uint32_t size, bool writing, uint64_t& value, hv::machine& target );

		uint8_t msi_offset = 0;
		uint8_t msix_offset = 0;
		uint32_t msix_vectors = 0;
		uint8_t msix_bar = 0;
		uint32_t msix_table = 0;
		uint32_t msix_pending = 0;
		uint8_t msix_entries [ msix_room * msix_entry ] = { };
		uint32_t msix_waiting = 0;
		bool pin_high = false;
		uint64_t messages_sent = 0;

		void identify ( uint16_t vendor, uint16_t device, uint8_t base_class, uint8_t sub_class, uint8_t interface, uint8_t revision, uint8_t header );

		void set_bar ( int32_t index, uint32_t size, bool io, uint32_t address );

		void set_interrupt ( uint8_t pin, uint8_t line );

		uint32_t bar_address ( int32_t index ) const;

		uint32_t read_config ( uint32_t offset, uint32_t size ) const;

		void write_config ( uint32_t offset, uint32_t size, uint32_t value );

		virtual void written ( uint32_t ) { }

		uint8_t config [ 256 ] = { };
		uint32_t bar_masks [ 6 ] = { };

	};

	class pci_bus : public hv::device {

	public:

		void attach ( uint32_t device, uint32_t function, pci_function* target );

		uint64_t read ( uint64_t port, uint32_t size ) override;

		void write ( uint64_t port, uint32_t size, uint64_t value ) override;

		uint32_t address = 0;

	private:

		pci_function* selected ( ) const;

		pci_function* slots [ 32 ] [ 8 ] = { };

	};

}