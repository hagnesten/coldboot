#include "pc/pci.hh"

namespace pc {

	void pci_function::identify ( uint16_t vendor, uint16_t device, uint8_t base_class, uint8_t sub_class, uint8_t interface, uint8_t revision, uint8_t header ) {

		config [ 0x00 ] = static_cast < uint8_t > ( vendor );
		config [ 0x01 ] = static_cast < uint8_t > ( vendor >> 8 );
		config [ 0x02 ] = static_cast < uint8_t > ( device );
		config [ 0x03 ] = static_cast < uint8_t > ( device >> 8 );
		config [ 0x06 ] = 0x00;
		config [ 0x07 ] = 0x02;
		config [ 0x08 ] = revision;
		config [ 0x09 ] = interface;
		config [ 0x0A ] = sub_class;
		config [ 0x0B ] = base_class;
		config [ 0x0E ] = header;

	}

	void pci_function::set_bar ( int32_t index, uint32_t size, bool io, uint32_t address ) {

		uint32_t mask = ~( size - 1 );
		uint32_t type = io ? 1u : 0u;
		uint32_t value = ( address & mask ) | type;

		bar_masks [ index ] = mask | type;

		for ( uint32_t i = 0; i < 4; i++ )
			config [ 0x10 + index * 4 + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	void pci_function::set_interrupt ( uint8_t pin, uint8_t line ) {

		config [ 0x3C ] = line;
		config [ 0x3D ] = pin;

	}

	void pci_function::add_msi ( uint8_t offset ) {

		msi_offset = offset;
		config [ 0x06 ] = static_cast < uint8_t > ( config [ 0x06 ] | 0x10 );
		config [ 0x34 ] = offset;
		config [ offset ] = msi_id;
		config [ offset + 1 ] = 0;
		config [ offset + 2 ] = 0x80;
		config [ offset + 3 ] = 0;

	}

	bool pci_function::msi_enabled ( ) const {

		return msi_offset && ( config [ msi_offset + 2 ] & 1 );

	}

	bool pci_function::send_msi ( hv::machine& target ) const {

		uint32_t address = read_config ( msi_offset + 4u, 4 );
		uint32_t high = read_config ( msi_offset + 8u, 4 );
		uint32_t data = read_config ( msi_offset + 12u, 2 );
		uint32_t mode = data >> 8 & 7;
		uint32_t type = WHvX64InterruptTypeFixed;

		if ( high || ( address & 0xFFF00000u ) != msi_window )
			return false;

		if ( mode == 1 )
			type = WHvX64InterruptTypeLowestPriority;
		else if ( mode == 4 )
			type = WHvX64InterruptTypeNmi;
		else if ( mode != 0 )
			return false;

		return target.interrupt ( address >> 12 & 0xFF, data & 0xFF, ( address & 4 ) != 0, false, type );

	}

	void pci_function::signal ( platform_bus& bus, hv::machine& target, uint32_t irq, bool wanted, bool& line ) {

		if ( msi_enabled ( ) ) {

			if ( pin_high ) {

				pin_high = false;
				bus.set_irq ( irq, false );

			}

			if ( wanted && !line && send_msi ( target ) )
				messages_sent++;

			line = wanted;

			return;

		}

		bool level = wanted && !( config [ 0x05 ] & 0x04 );

		line = wanted;

		if ( level == pin_high )
			return;

		pin_high = level;
		bus.set_irq ( irq, level );

	}

	uint32_t pci_function::bar_address ( int32_t index ) const {

		uint32_t value = read_config ( 0x10 + static_cast < uint32_t > ( index ) * 4, 4 );

		return value & ( value & 1 ? ~3u : ~15u );

	}

	uint32_t pci_function::read_config ( uint32_t offset, uint32_t size ) const {

		uint32_t value = 0;

		for ( uint32_t i = 0; i < size && offset + i < 256; i++ )
			value |= static_cast < uint32_t > ( config [ offset + i ] ) << ( i * 8 );

		return value;

	}

	void pci_function::write_config ( uint32_t offset, uint32_t size, uint32_t value ) {

		for ( uint32_t i = 0; i < size && offset + i < 256; i++ ) {

			uint32_t at = offset + i;
			uint8_t byte = static_cast < uint8_t > ( value >> ( i * 8 ) );

			if ( msi_offset && at >= msi_offset && at < msi_offset + 14u ) {

				uint32_t relative = at - msi_offset;

				if ( relative == 2 )
					config [ at ] = static_cast < uint8_t > ( 0x80 | ( byte & 0x71 ) );
				else if ( relative == 4 )
					config [ at ] = static_cast < uint8_t > ( byte & 0xFC );
				else if ( relative >= 4 )
					config [ at ] = byte;

			} else if ( at == 0x04 )
				config [ at ] = static_cast < uint8_t > ( byte & 0x47 );
			else if ( at == 0x05 )
				config [ at ] = static_cast < uint8_t > ( byte & 0x05 );
			else if ( at == 0x06 || at == 0x07 )
				config [ at ] = static_cast < uint8_t > ( config [ at ] & ~( byte & ( at == 0x07 ? 0xF9 : 0x00 ) ) );
			else if ( at == 0x0C || at == 0x0D || at == 0x3C || at >= 0x40 )
				config [ at ] = byte;
			else if ( at >= 0x10 && at < 0x28 ) {

				uint32_t index = ( at - 0x10 ) / 4;
				uint32_t shift = ( at & 3 ) * 8;
				uint32_t mask = bar_masks [ index ];
				uint32_t type = mask & 1 ? 1u : ( read_config ( 0x10 + index * 4, 4 ) & 0x0F );
				uint32_t current = read_config ( 0x10 + index * 4, 4 );
				uint32_t merged = ( current & ~( 0xFFu << shift ) ) | static_cast < uint32_t > ( byte ) << shift;
				uint32_t stored = mask ? ( merged & mask & ( mask & 1 ? ~3u : ~15u ) ) | type : 0;

				for ( uint32_t b = 0; b < 4; b++ )
					config [ 0x10 + index * 4 + b ] = static_cast < uint8_t > ( stored >> ( b * 8 ) );

			}

		}

		written ( offset );

	}

	void pci_bus::attach ( uint32_t device, uint32_t function, pci_function* target ) {

		if ( device < 32 && function < 8 )
			slots [ device ] [ function ] = target;

	}

	pci_function* pci_bus::selected ( ) const {

		if ( !( address & 0x80000000 ) || ( address >> 16 & 0xFF ) )
			return nullptr;

		return slots [ address >> 11 & 31 ] [ address >> 8 & 7 ];

	}

	uint64_t pci_bus::read ( uint64_t port, uint32_t size ) {

		if ( port >= 0xCF8 && port < 0xCFC )
			return size == 4 ? address : 0xFFFFFFFF;

		pci_function* target = selected ( );
		uint32_t offset = ( address & 0xFC ) + static_cast < uint32_t > ( port - 0xCFC );

		if ( !target )
			return size >= 4 ? 0xFFFFFFFFu : ( 1u << ( size * 8 ) ) - 1;

		return target->read_config ( offset, size );

	}

	void pci_bus::write ( uint64_t port, uint32_t size, uint64_t value ) {

		if ( port == 0xCF8 && size == 4 ) {

			address = static_cast < uint32_t > ( value ) & 0x80FFFFFC;

			return;

		}

		if ( port < 0xCFC )
			return;

		pci_function* target = selected ( );

		if ( target )
			target->write_config ( ( address & 0xFC ) + static_cast < uint32_t > ( port - 0xCFC ), size, static_cast < uint32_t > ( value ) );

	}

}