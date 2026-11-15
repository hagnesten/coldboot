#include "pc/ehci.hh"

namespace pc {

	enum : uint32_t {

		CAPABILITY_LENGTH = 0x20,
		OPERATIONAL = 0x20,
		USB_COMMAND = 0x00,
		USB_STATUS = 0x04,
		USB_INTERRUPTS = 0x08,
		FRAME_INDEX = 0x0C,
		SEGMENT = 0x10,
		PERIODIC_BASE = 0x14,
		ASYNC_ADDRESS = 0x18,
		CONFIGURE_FLAG = 0x40,
		PORT_REGISTERS = 0x44,

		COMMAND_RUN = 1u << 0,
		COMMAND_RESET = 1u << 1,
		COMMAND_PERIODIC = 1u << 4,
		COMMAND_ASYNC = 1u << 5,
		COMMAND_DOORBELL = 1u << 6,
		COMMAND_DEFAULT = 0x00080000u,
		STATUS_INTERRUPT = 1u << 0,
		STATUS_ERROR = 1u << 1,
		STATUS_PORT_CHANGE = 1u << 2,
		STATUS_ADVANCE = 1u << 5,
		STATUS_CLEARABLE = 0x3Fu,
		STATUS_HALTED = 1u << 12,
		STATUS_PERIODIC = 1u << 14,
		STATUS_ASYNC = 1u << 15,

		PORT_CONNECTED = 1u << 0,
		PORT_CONNECT_CHANGE = 1u << 1,
		PORT_ENABLED = 1u << 2,
		PORT_ENABLE_CHANGE = 1u << 3,
		PORT_OVER_CURRENT_CHANGE = 1u << 5,
		PORT_RESUME = 1u << 6,
		PORT_SUSPEND = 1u << 7,
		PORT_RESET = 1u << 8,
		PORT_LINE_J = 2u << 10,
		PORT_POWER = 1u << 12,
		PORT_OWNER = 1u << 13,
		PORT_WRITABLE = PORT_POWER | PORT_OWNER | 0x7FC000u | PORT_RESUME | PORT_SUSPEND,
		PORT_CHANGES = PORT_CONNECT_CHANGE | PORT_ENABLE_CHANGE | PORT_OVER_CURRENT_CHANGE,

		LINK_TERMINATE = 1u << 0,
		TOKEN_PING = 1u << 0,
		TOKEN_TRANSACTION = 1u << 3,
		TOKEN_HALTED = 1u << 6,
		TOKEN_ACTIVE = 1u << 7,
		TOKEN_IOC = 1u << 15,
		TOKEN_TOGGLE = 1u << 31,
		TOKEN_STATUS = 0xFFu,

		HEAD_LINK = 0x00,
		HEAD_CHARACTERISTICS = 0x04,
		HEAD_CURRENT = 0x0C,
		HEAD_NEXT = 0x10,
		HEAD_ALTERNATE = 0x14,
		HEAD_TOKEN = 0x18,
		HEAD_BUFFERS = 0x1C,

	};

	ehci::ehci ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		identify ( 0x8086, 0x293A, 0x0C, 0x03, 0x20, 0x03, 0x00 );
		set_bar ( 0, memory_size, false, default_memory );
		set_interrupt ( 1, 11 );
		add_msi ( 0x50 );
		config [ 0x04 ] = 0x06;
		config [ 0x60 ] = 0x20;
		config [ 0x61 ] = 0x20;
		reset ( );

	}

	void ehci::reset ( ) {

		command = COMMAND_DEFAULT;
		status = STATUS_HALTED;
		enabled = 0;
		frame = 0;
		segment = 0;
		periodic = 0;
		async_list = 0;
		configured = 0;

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( devices [ i ] )
				devices [ i ]->reset ( );

			ports [ i ] = PORT_OWNER | ( devices [ i ] ? PORT_CONNECT_CHANGE : 0 );

		}

		update_line ( );

	}

	void ehci::plug ( int32_t port, usb_device* device ) {

		if ( port < 0 || port >= port_count )
			return;

		devices [ port ] = device;
		ports [ port ] = ( ports [ port ] & ~( PORT_ENABLED | PORT_RESET ) ) | PORT_CONNECT_CHANGE;

		if ( device )
			device->reset ( );

		status |= STATUS_PORT_CHANGE;
		update_line ( );

	}

	void ehci::written ( uint32_t offset ) {

		if ( offset < 0x10 || offset >= 0x14 )
			return;

		uint32_t wanted = bar_address ( 0 );

		if ( wanted && wanted != memory_base ) {

			vm.relocate_memory ( this, memory_base, wanted );
			memory_base = wanted;

		}

	}

	uint32_t ehci::read32 ( uint32_t address ) {

		uint8_t bytes [ 4 ] = { };

		vm.read ( address, bytes, 4 );

		return static_cast < uint32_t > ( bytes [ 0 ] ) | static_cast < uint32_t > ( bytes [ 1 ] ) << 8 | static_cast < uint32_t > ( bytes [ 2 ] ) << 16 | static_cast < uint32_t > ( bytes [ 3 ] ) << 24;

	}

	void ehci::write32 ( uint32_t address, uint32_t value ) {

		uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( value ), static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value >> 16 ), static_cast < uint8_t > ( value >> 24 ) };

		vm.write ( address, bytes, 4 );

	}

	uint32_t ehci::port_read ( int32_t port ) const {

		uint32_t value = ports [ port ];

		if ( !configured )
			value |= PORT_OWNER;

		if ( !( value & PORT_OWNER ) && ( value & PORT_POWER ) && devices [ port ] ) {

			value |= PORT_CONNECTED;

			if ( !( value & ( PORT_ENABLED | PORT_RESET ) ) )
				value |= PORT_LINE_J;

		} else
			value &= ~( PORT_CONNECTED | PORT_ENABLED );

		return value;

	}

	void ehci::port_write ( int32_t port, uint32_t value ) {

		uint32_t& state = ports [ port ];
		bool resetting = ( state & PORT_RESET ) != 0;

		state &= ~( value & PORT_CHANGES );

		if ( !( value & PORT_ENABLED ) )
			state &= ~PORT_ENABLED;

		if ( ( value & PORT_OWNER ) && !( state & PORT_OWNER ) )
			state &= ~PORT_ENABLED;

		state = ( state & ~PORT_WRITABLE ) | ( value & PORT_WRITABLE );

		if ( ( value & PORT_RESET ) && !resetting ) {

			state = ( state | PORT_RESET ) & ~PORT_ENABLED;

			if ( devices [ port ] )
				devices [ port ]->reset ( );

		} else if ( !( value & PORT_RESET ) && resetting ) {

			state &= ~PORT_RESET;

			if ( devices [ port ] && devices [ port ]->high_speed && !( state & PORT_OWNER ) && ( state & PORT_POWER ) )
				state |= PORT_ENABLED;

		}

	}

	uint32_t ehci::register_read ( uint32_t offset ) {

		switch ( offset ) {

			case 0x00:
				return CAPABILITY_LENGTH | 0x0100u << 16;

			case 0x04:
				return static_cast < uint32_t > ( port_count ) | 1u << 4;

			case 0x08:
				return 0;

			case 0x0C:
				return 0;

			case OPERATIONAL + USB_COMMAND:
				return command;

			case OPERATIONAL + USB_STATUS:
				return status | ( command & COMMAND_ASYNC ? STATUS_ASYNC : 0 ) | ( command & COMMAND_PERIODIC ? STATUS_PERIODIC : 0 );

			case OPERATIONAL + USB_INTERRUPTS:
				return enabled;

			case OPERATIONAL + FRAME_INDEX:
				return command & COMMAND_RUN ? static_cast < uint32_t > ( ( bus.now ( ) - started_at ) / 125000 + frame ) & 0x3FFF : frame;

			case OPERATIONAL + SEGMENT:
				return segment;

			case OPERATIONAL + PERIODIC_BASE:
				return periodic;

			case OPERATIONAL + ASYNC_ADDRESS:
				return async_list;

			case OPERATIONAL + CONFIGURE_FLAG:
				return configured;

			default:
				break;

		}

		if ( offset >= OPERATIONAL + PORT_REGISTERS && offset < OPERATIONAL + PORT_REGISTERS + port_count * 4 )
			return port_read ( static_cast < int32_t > ( ( offset - OPERATIONAL - PORT_REGISTERS ) / 4 ) );

		return 0;

	}

	void ehci::register_write ( uint32_t offset, uint32_t value ) {

		switch ( offset ) {

			case OPERATIONAL + USB_COMMAND:

				if ( value & COMMAND_RESET ) {

					reset ( );

					return;

				}

				if ( ( value & COMMAND_RUN ) && !( command & COMMAND_RUN ) )
					started_at = bus.now ( );

				if ( !( value & COMMAND_RUN ) && ( command & COMMAND_RUN ) )
					frame = register_read ( OPERATIONAL + FRAME_INDEX );

				command = value & ~( COMMAND_RESET | COMMAND_DOORBELL );

				if ( command & COMMAND_RUN )
					status &= ~STATUS_HALTED;
				else
					status |= STATUS_HALTED;

				if ( value & COMMAND_DOORBELL )
					status |= STATUS_ADVANCE;

				update_line ( );

				return;

			case OPERATIONAL + USB_STATUS:
				status &= ~( value & STATUS_CLEARABLE );
				update_line ( );
				return;

			case OPERATIONAL + USB_INTERRUPTS:
				enabled = value & STATUS_CLEARABLE;
				update_line ( );
				return;

			case OPERATIONAL + FRAME_INDEX:

				if ( !( command & COMMAND_RUN ) )
					frame = value & 0x3FFF;

				return;

			case OPERATIONAL + SEGMENT:
				segment = value;
				return;

			case OPERATIONAL + PERIODIC_BASE:
				periodic = value & ~0xFFFu;
				return;

			case OPERATIONAL + ASYNC_ADDRESS:
				async_list = value & ~0x1Fu;
				return;

			case OPERATIONAL + CONFIGURE_FLAG:

				if ( ( value & 1 ) && !configured ) {

					for ( int32_t i = 0; i < port_count; i++ )
						ports [ i ] = ( ports [ i ] & ~PORT_OWNER ) | ( devices [ i ] ? PORT_CONNECT_CHANGE : 0 );

					status |= STATUS_PORT_CHANGE;

				}

				configured = value & 1;
				update_line ( );
				return;

			default:
				break;

		}

		if ( offset >= OPERATIONAL + PORT_REGISTERS && offset < OPERATIONAL + PORT_REGISTERS + port_count * 4 )
			port_write ( static_cast < int32_t > ( ( offset - OPERATIONAL - PORT_REGISTERS ) / 4 ), value );

	}

	uint64_t ehci::read ( uint64_t address, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );
		uint32_t value = register_read ( offset & ~3u ) >> ( offset % 4 * 8 );

		return size >= 4 ? value : value & ( ( 1u << ( size * 8 ) ) - 1 );

	}

	void ehci::write ( uint64_t address, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( size == 8 && !( offset & 7 ) ) {

			register_write ( offset, static_cast < uint32_t > ( value ) );
			register_write ( offset + 4, static_cast < uint32_t > ( value >> 32 ) );

			return;

		}

		if ( size == 4 && !( offset & 3 ) )
			register_write ( offset, static_cast < uint32_t > ( value ) );

	}

	usb_device* ehci::find ( uint8_t address ) {

		for ( int32_t i = 0; i < port_count; i++ ) {

			uint32_t state = port_read ( i );

			if ( devices [ i ] && ( state & PORT_ENABLED ) && !( state & ( PORT_RESET | PORT_SUSPEND | PORT_OWNER ) ) ) {

				usb_device* found = devices [ i ]->route ( address );

				if ( found )
					return found;

			}

		}

		return nullptr;

	}

	void ehci::move ( uint32_t head, uint32_t page, uint32_t offset, uint8_t* data, uint32_t count, bool to_guest ) {

		while ( count && page < 5 ) {

			uint32_t base = read32 ( head + HEAD_BUFFERS + page * 4 ) & ~0xFFFu;
			uint32_t piece = 4096 - offset < count ? 4096 - offset : count;

			if ( to_guest )
				vm.write ( base + offset, data, piece );
			else
				vm.read ( base + offset, data, piece );

			data += piece;
			count -= piece;
			offset = 0;
			page++;

		}

	}

	ehci::outcome ehci::execute ( uint32_t head, usb_device* device, uint8_t endpoint, uint32_t max_packet ) {

		uint32_t token = read32 ( head + HEAD_TOKEN );
		uint32_t first = read32 ( head + HEAD_BUFFERS );
		uint32_t code = token >> 8 & 3;
		uint8_t pid = code == 0 ? pid_out : ( code == 1 ? pid_in : pid_setup );
		uint32_t remaining = token >> 16 & 0x7FFF;
		uint32_t page = token >> 12 & 7;
		uint32_t offset = first & 0xFFF;
		bool toggle = ( token & TOKEN_TOGGLE ) != 0;
		bool shorted = false;
		bool failed = false;
		bool stalled = false;
		bool waiting = false;

		if ( !max_packet )
			max_packet = 8;

		if ( max_packet > sizeof ( scratch ) )
			max_packet = sizeof ( scratch );

		do {

			uint32_t count = remaining < max_packet ? remaining : max_packet;
			int32_t result = usb_timeout;

			if ( pid == pid_setup )
				count = 8;

			if ( device ) {

				if ( pid == pid_in ) {

					result = device->transfer ( pid, endpoint, scratch, static_cast < int32_t > ( count ) );

					if ( result > 0 )
						move ( head, page, offset, scratch, static_cast < uint32_t > ( result ) < count ? static_cast < uint32_t > ( result ) : count, true );

				} else {

					move ( head, page, offset, scratch, count, false );
					result = device->transfer ( pid, endpoint, scratch, static_cast < int32_t > ( count ) );

				}

			}

			if ( result == usb_nak ) {

				waiting = true;

				break;

			}

			if ( result < 0 ) {

				failed = true;
				stalled = result == usb_stall;

				break;

			}

			uint32_t moved = pid == pid_in ? ( static_cast < uint32_t > ( result ) < count ? static_cast < uint32_t > ( result ) : count ) : count;

			offset += moved;
			page += offset / 4096;
			offset %= 4096;
			remaining = moved > remaining ? 0 : remaining - moved;
			toggle = !toggle;
			transfers_run++;

			if ( pid == pid_in && moved < count ) {

				shorted = true;

				break;

			}

		} while ( remaining );

		token &= ~( 0x7FFFu << 16 | 7u << 12 | TOKEN_TOGGLE | TOKEN_STATUS );
		token |= remaining << 16 | ( page & 7 ) << 12 | ( toggle ? TOKEN_TOGGLE : 0 );

		if ( waiting )
			token |= TOKEN_ACTIVE;
		else if ( failed )
			token |= TOKEN_HALTED | ( stalled ? 0 : TOKEN_TRANSACTION );

		write32 ( head + HEAD_TOKEN, token );
		write32 ( head + HEAD_BUFFERS, ( first & ~0xFFFu ) | offset );

		uint32_t current = read32 ( head + HEAD_CURRENT ) & ~0x1Fu;

		if ( current )
			write32 ( current + 8, token );

		if ( waiting )
			return OUTCOME_WAITING;

		if ( failed ) {

			status |= STATUS_ERROR;

			if ( token & TOKEN_IOC )
				status |= STATUS_INTERRUPT;

			return OUTCOME_HALTED;

		}

		if ( shorted ) {

			uint32_t alternate = read32 ( head + HEAD_ALTERNATE );

			if ( !( alternate & LINK_TERMINATE ) )
				write32 ( head + HEAD_NEXT, alternate );

			status |= STATUS_INTERRUPT;

			return OUTCOME_SHORT;

		}

		if ( token & TOKEN_IOC )
			status |= STATUS_INTERRUPT;

		return OUTCOME_DONE;

	}

	bool ehci::run_head ( uint32_t head ) {

		uint32_t characteristics = read32 ( head + HEAD_CHARACTERISTICS );
		uint8_t target = static_cast < uint8_t > ( characteristics & 0x7F );
		uint8_t endpoint = static_cast < uint8_t > ( characteristics >> 8 & 0xF );
		bool from_queue = ( characteristics >> 14 & 1 ) != 0;
		uint32_t max_packet = characteristics >> 16 & 0x7FF;
		usb_device* device = find ( target );
		bool progressed = false;

		for ( int32_t step = 0; step < 128; step++ ) {

			uint32_t token = read32 ( head + HEAD_TOKEN );

			if ( token & TOKEN_HALTED )
				return progressed;

			if ( !( token & TOKEN_ACTIVE ) ) {

				uint32_t next = read32 ( head + HEAD_NEXT );

				if ( next & LINK_TERMINATE )
					return progressed;

				uint32_t item = next & ~0x1Fu;
				uint32_t loaded = read32 ( item + 8 );

				if ( !( loaded & TOKEN_ACTIVE ) )
					return progressed;

				write32 ( head + HEAD_CURRENT, item );

				for ( uint32_t i = 0; i < 8; i++ )
					write32 ( head + HEAD_NEXT + i * 4, read32 ( item + i * 4 ) );

				if ( !from_queue )
					write32 ( head + HEAD_TOKEN, ( loaded & ~TOKEN_TOGGLE ) | ( token & TOKEN_TOGGLE ) );

				continue;

			}

			outcome result = execute ( head, device, endpoint, max_packet );

			if ( result == OUTCOME_WAITING )
				return progressed;

			progressed = true;

			if ( result == OUTCOME_HALTED )
				return progressed;

		}

		return progressed;

	}

	void ehci::run_async ( ) {

		if ( !( command & COMMAND_RUN ) || !( command & COMMAND_ASYNC ) || !async_list )
			return;

		for ( int32_t pass = 0; pass < 8; pass++ ) {

			uint32_t at = async_list;
			bool progressed = false;

			passes++;

			for ( int32_t visits = 0; visits < 64; visits++ ) {

				progressed = run_head ( at ) || progressed;

				uint32_t link = read32 ( at + HEAD_LINK );

				if ( link & LINK_TERMINATE )
					break;

				at = link & ~0x1Fu;

				if ( at == async_list )
					break;

			}

			if ( !progressed )
				break;

		}

		update_line ( );

	}

	void ehci::advance ( uint64_t ) {

		run_async ( );

	}

	void ehci::update_line ( ) {

		bool wanted = ( status & enabled & STATUS_CLEARABLE ) != 0;

		signal ( bus, vm, irq, wanted, line );

	}

}