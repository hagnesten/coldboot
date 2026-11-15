#include "pc/xhci.hh"

namespace pc {

	enum : uint32_t {

		CAPABILITY_LENGTH = 0x40,
		OPERATIONAL = 0x40,
		PORT_REGISTERS = OPERATIONAL + 0x400,
		EXTENDED = 0x800,
		RUNTIME = 0x1000,
		INTERRUPTER = RUNTIME + 0x20,
		DOORBELLS = 0x2000,
		CONTEXT_SIZE = 32,

		USB_COMMAND = 0x00,
		USB_STATUS = 0x04,
		PAGE_SIZE = 0x08,
		NOTIFICATION = 0x14,
		COMMAND_RING = 0x18,
		COMMAND_RING_HIGH = 0x1C,
		CONTEXT_ARRAY = 0x30,
		CONTEXT_ARRAY_HIGH = 0x34,
		CONFIGURE = 0x38,

		COMMAND_RUN = 1u << 0,
		COMMAND_RESET = 1u << 1,
		COMMAND_INTERRUPTS = 1u << 2,
		STATUS_HALTED = 1u << 0,
		STATUS_SYSTEM_ERROR = 1u << 2,
		STATUS_EVENT = 1u << 3,
		STATUS_PORT_CHANGE = 1u << 4,
		STATUS_RESTORE_ERROR = 1u << 10,
		RING_CYCLE = 1u << 0,
		RING_STOP = 1u << 1,
		RING_ABORT = 1u << 2,
		RING_RUNNING = 1u << 3,

		PORT_CONNECTED = 1u << 0,
		PORT_ENABLED = 1u << 1,
		PORT_RESET = 1u << 4,
		PORT_LINK_SHIFT = 5,
		PORT_LINK_MASK = 0xFu << 5,
		PORT_POWER = 1u << 9,
		PORT_SPEED_SHIFT = 10,
		PORT_LINK_WRITE = 1u << 16,
		PORT_CONNECT_CHANGE = 1u << 17,
		PORT_ENABLE_CHANGE = 1u << 18,
		PORT_RESET_CHANGE = 1u << 21,
		PORT_CHANGES = 0x7Fu << 17,
		PORT_WAKES = 7u << 25,
		LINK_U0 = 0,
		LINK_RX_DETECT = 5,
		LINK_POLLING = 7,
		SPEED_FULL = 1,

		LEGACY_BIOS = 1u << 16,
		LEGACY_OS = 1u << 24,

		INTERRUPT_PENDING = 1u << 0,
		INTERRUPT_ENABLE = 1u << 1,
		EVENT_BUSY = 1u << 3,

		TRB_CYCLE = 1u << 0,
		TRB_TOGGLE = 1u << 1,
		TRB_SHORT = 1u << 2,
		TRB_CHAIN = 1u << 4,
		TRB_IOC = 1u << 5,
		TRB_IMMEDIATE = 1u << 6,
		TRB_BLOCK = 1u << 9,
		TRB_IN = 1u << 16,
		TRB_EVENT_DATA = 1u << 2,

		TYPE_NORMAL = 1,
		TYPE_SETUP = 2,
		TYPE_DATA = 3,
		TYPE_STATUS = 4,
		TYPE_LINK = 6,
		TYPE_EVENT_DATA = 7,
		TYPE_NOOP = 8,
		TYPE_ENABLE_SLOT = 9,
		TYPE_DISABLE_SLOT = 10,
		TYPE_ADDRESS_DEVICE = 11,
		TYPE_CONFIGURE_ENDPOINT = 12,
		TYPE_EVALUATE_CONTEXT = 13,
		TYPE_RESET_ENDPOINT = 14,
		TYPE_STOP_ENDPOINT = 15,
		TYPE_SET_DEQUEUE = 16,
		TYPE_RESET_DEVICE = 17,
		TYPE_NOOP_COMMAND = 23,
		TYPE_TRANSFER_EVENT = 32,
		TYPE_COMMAND_EVENT = 33,
		TYPE_PORT_EVENT = 34,

		CODE_SUCCESS = 1,
		CODE_TRANSACTION = 4,
		CODE_TRB = 5,
		CODE_STALL = 6,
		CODE_NO_SLOTS = 9,
		CODE_SLOT_DISABLED = 11,
		CODE_ENDPOINT_DISABLED = 12,
		CODE_SHORT = 13,
		CODE_PARAMETER = 17,
		CODE_CONTEXT_STATE = 19,
		CODE_RING_STOPPED = 24,

		ENDPOINT_DISABLED = 0,
		ENDPOINT_RUNNING = 1,
		ENDPOINT_HALTED = 2,
		ENDPOINT_STOPPED = 3,
		ENDPOINT_ERROR = 4,
		ENDPOINT_CONTROL = 4,

		SLOT_ENABLED = 0,
		SLOT_DEFAULT = 1,
		SLOT_ADDRESSED = 2,
		SLOT_CONFIGURED = 3,

	};

	static constexpr uint64_t retry_time = 1000000;

	xhci::xhci ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		identify ( 0x8086, 0x1E31, 0x0C, 0x03, 0x30, 0x04, 0x00 );
		set_bar ( 0, memory_size, false, default_memory );
		set_interrupt ( 1, 11 );
		config [ 0x04 ] = 0x06;
		config [ 0x60 ] = 0x30;
		config [ 0x61 ] = 0x20;
		reset ( );

	}

	void xhci::reset ( ) {

		command = 0;
		status = STATUS_HALTED;
		notification = 0;
		configured_slots = 0;
		device_contexts = 0;
		commands = ring ( );
		command_running = false;
		management = 0;
		moderation = 4000;
		table_size = 0;
		table_base = 0;
		dequeue_pointer = 0;
		segment_index = 0;
		segment_base = 0;
		segment_size = 0;
		enqueue = 0;
		producer_cycle = true;

		for ( uint32_t i = 0; i <= slot_count; i++ ) {

			slots [ i ] = slot_state ( );
			waiting [ i ] = 0;
			rung [ i ] = 0;

		}

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( devices [ i ] ) {

				devices [ i ]->reset ( );
				ports [ i ] = PORT_POWER | PORT_CONNECTED | PORT_CONNECT_CHANGE | SPEED_FULL << PORT_SPEED_SHIFT | LINK_POLLING << PORT_LINK_SHIFT;

			} else
				ports [ i ] = PORT_POWER | LINK_RX_DETECT << PORT_LINK_SHIFT;

		}

		update_line ( );

	}

	void xhci::plug ( int32_t port, usb_device* device ) {

		if ( port < 0 || port >= port_count )
			return;

		devices [ port ] = device;

		if ( device ) {

			device->reset ( );
			ports [ port ] = PORT_POWER | PORT_CONNECTED | PORT_CONNECT_CHANGE | SPEED_FULL << PORT_SPEED_SHIFT | LINK_POLLING << PORT_LINK_SHIFT;

		} else
			ports [ port ] = PORT_POWER | PORT_CONNECT_CHANGE | ( ports [ port ] & PORT_ENABLED ? PORT_ENABLE_CHANGE : 0 ) | LINK_RX_DETECT << PORT_LINK_SHIFT;

		port_event ( port );

	}

	void xhci::written ( uint32_t offset ) {

		if ( offset < 0x10 || offset >= 0x14 )
			return;

		uint32_t wanted = bar_address ( 0 );

		if ( wanted && wanted != memory_base ) {

			vm.relocate_memory ( this, memory_base, wanted );
			memory_base = wanted;

		}

	}

	uint32_t xhci::read32 ( uint64_t address ) {

		uint8_t bytes [ 4 ] = { };

		vm.read ( address, bytes, 4 );

		return static_cast < uint32_t > ( bytes [ 0 ] ) | static_cast < uint32_t > ( bytes [ 1 ] ) << 8 | static_cast < uint32_t > ( bytes [ 2 ] ) << 16 | static_cast < uint32_t > ( bytes [ 3 ] ) << 24;

	}

	void xhci::write32 ( uint64_t address, uint32_t value ) {

		uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( value ), static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value >> 16 ), static_cast < uint8_t > ( value >> 24 ) };

		vm.write ( address, bytes, 4 );

	}

	uint64_t xhci::read64 ( uint64_t address ) {

		return static_cast < uint64_t > ( read32 ( address ) ) | static_cast < uint64_t > ( read32 ( address + 4 ) ) << 32;

	}

	uint32_t xhci::port_read ( int32_t port ) const {

		return ports [ port ];

	}

	void xhci::port_write ( int32_t port, uint32_t value ) {

		uint32_t& state = ports [ port ];

		state &= ~( value & PORT_CHANGES );
		state = ( state & ~PORT_WAKES ) | ( value & PORT_WAKES );

		if ( value & PORT_ENABLED )
			state &= ~PORT_ENABLED;

		if ( ( value & PORT_LINK_WRITE ) && ( state & PORT_ENABLED ) )
			state = ( state & ~PORT_LINK_MASK ) | ( value & PORT_LINK_MASK );

		if ( ( value & PORT_RESET ) && ( state & PORT_CONNECTED ) && devices [ port ] ) {

			devices [ port ]->reset ( );

			for ( uint32_t i = 1; i <= slot_count; i++ ) {

				if ( slots [ i ].enabled && slots [ i ].port == port )
					waiting [ i ] = 0;

			}

			state = ( state & ~( PORT_LINK_MASK | PORT_RESET ) ) | PORT_ENABLED | PORT_RESET_CHANGE | LINK_U0 << PORT_LINK_SHIFT;
			port_event ( port );

		}

	}

	uint32_t xhci::register_read ( uint32_t offset ) {

		switch ( offset ) {

			case 0x00:
				return CAPABILITY_LENGTH | 0x0100u << 16;

			case 0x04:
				return slot_count | 1u << 8 | static_cast < uint32_t > ( port_count ) << 24;

			case 0x08:
				return 2u << 4;

			case 0x0C:
				return 0;

			case 0x10:
				return 1u | ( EXTENDED / 4 ) << 16;

			case 0x14:
				return DOORBELLS;

			case 0x18:
				return RUNTIME;

			case EXTENDED:
				return 1u | 4u << 8 | legacy;

			case EXTENDED + 0x10:
				return 2u | 0x02u << 24;

			case EXTENDED + 0x14:
				return 0x20425355u;

			case EXTENDED + 0x18:
				return 1u | static_cast < uint32_t > ( port_count ) << 8;

			case OPERATIONAL + USB_COMMAND:
				return command;

			case OPERATIONAL + USB_STATUS:
				return status;

			case OPERATIONAL + PAGE_SIZE:
				return 1;

			case OPERATIONAL + NOTIFICATION:
				return notification;

			case OPERATIONAL + COMMAND_RING:
				return command_running ? RING_RUNNING : 0;

			case OPERATIONAL + CONTEXT_ARRAY:
				return static_cast < uint32_t > ( device_contexts );

			case OPERATIONAL + CONTEXT_ARRAY_HIGH:
				return static_cast < uint32_t > ( device_contexts >> 32 );

			case OPERATIONAL + CONFIGURE:
				return configured_slots;

			case RUNTIME:
				return static_cast < uint32_t > ( bus.now ( ) / 125000 ) & 0x3FFF;

			case INTERRUPTER:
				return management;

			case INTERRUPTER + 0x04:
				return moderation;

			case INTERRUPTER + 0x08:
				return table_size;

			case INTERRUPTER + 0x10:
				return static_cast < uint32_t > ( table_base );

			case INTERRUPTER + 0x14:
				return static_cast < uint32_t > ( table_base >> 32 );

			case INTERRUPTER + 0x18:
				return static_cast < uint32_t > ( dequeue_pointer );

			case INTERRUPTER + 0x1C:
				return static_cast < uint32_t > ( dequeue_pointer >> 32 );

			default:
				break;

		}

		if ( offset >= PORT_REGISTERS && offset < PORT_REGISTERS + port_count * 0x10 && !( offset & 0xF ) )
			return port_read ( static_cast < int32_t > ( ( offset - PORT_REGISTERS ) / 0x10 ) );

		return 0;

	}

	void xhci::load_segment ( ) {

		if ( !table_size || !table_base ) {

			segment_base = 0;
			segment_size = 0;

			return;

		}

		uint64_t entry = table_base + static_cast < uint64_t > ( segment_index ) * 16;

		segment_base = read64 ( entry ) & ~0x3Full;
		segment_size = read32 ( entry + 8 ) & 0xFFFF;
		enqueue = 0;

	}

	void xhci::register_write ( uint32_t offset, uint32_t value ) {

		switch ( offset ) {

			case EXTENDED:

				if ( value & LEGACY_OS )
					legacy = LEGACY_OS;
				else
					legacy &= ~LEGACY_OS;

				return;

			case OPERATIONAL + USB_COMMAND: {

				if ( value & COMMAND_RESET ) {

					reset ( );

					return;

				}

				bool starting = !( command & COMMAND_RUN ) && ( value & COMMAND_RUN );

				command = value & 0x3F0Fu & ~COMMAND_RESET;

				if ( command & COMMAND_RUN )
					status &= ~STATUS_HALTED;
				else {

					status |= STATUS_HALTED;
					command_running = false;

				}

				if ( starting ) {

					last_retry = bus.now ( );

					for ( int32_t i = 0; i < port_count; i++ ) {

						if ( ports [ i ] & PORT_CHANGES )
							port_event ( i );

					}

				}

				update_line ( );

				return;

			}

			case OPERATIONAL + USB_STATUS:
				status &= ~( value & ( STATUS_SYSTEM_ERROR | STATUS_EVENT | STATUS_PORT_CHANGE | STATUS_RESTORE_ERROR ) );
				update_line ( );
				return;

			case OPERATIONAL + NOTIFICATION:
				notification = value & 0xFFFF;
				return;

			case OPERATIONAL + COMMAND_RING:

				if ( command_running && ( value & ( RING_STOP | RING_ABORT ) ) ) {

					trb stopped;

					command_running = false;
					stopped.parameter = commands.dequeue;
					stopped.status = CODE_RING_STOPPED << 24;
					stopped.control = TYPE_COMMAND_EVENT << 10;
					post ( stopped );

					return;

				}

				if ( !command_running ) {

					commands.dequeue = ( commands.dequeue & 0xFFFFFFFF00000000ull ) | ( value & ~0x3Fu );
					commands.cycle = ( value & RING_CYCLE ) != 0;

				}

				return;

			case OPERATIONAL + COMMAND_RING_HIGH:

				if ( !command_running )
					commands.dequeue = ( commands.dequeue & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;

				return;

			case OPERATIONAL + CONTEXT_ARRAY:
				device_contexts = ( device_contexts & 0xFFFFFFFF00000000ull ) | ( value & ~0x3Fu );
				return;

			case OPERATIONAL + CONTEXT_ARRAY_HIGH:
				device_contexts = ( device_contexts & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				return;

			case OPERATIONAL + CONFIGURE:
				configured_slots = ( value & 0xFF ) > slot_count ? slot_count : value & 0xFF;
				return;

			case INTERRUPTER:
				management = ( management & INTERRUPT_PENDING & ~( value & INTERRUPT_PENDING ) ) | ( value & INTERRUPT_ENABLE );
				update_line ( );
				return;

			case INTERRUPTER + 0x04:
				moderation = value;
				return;

			case INTERRUPTER + 0x08:
				table_size = ( value & 0xFFFF ) > segment_room ? segment_room : value & 0xFFFF;
				return;

			case INTERRUPTER + 0x10:
				table_base = ( table_base & 0xFFFFFFFF00000000ull ) | ( value & ~0x3Fu );
				segment_index = 0;
				producer_cycle = true;
				load_segment ( );
				return;

			case INTERRUPTER + 0x14:
				table_base = ( table_base & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				segment_index = 0;
				producer_cycle = true;
				load_segment ( );
				return;

			case INTERRUPTER + 0x18:
				dequeue_pointer = ( dequeue_pointer & 0xFFFFFFFF00000000ull ) | ( value & ~0xFu );
				return;

			case INTERRUPTER + 0x1C:
				dequeue_pointer = ( dequeue_pointer & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				return;

			default:
				break;

		}

		if ( offset >= PORT_REGISTERS && offset < PORT_REGISTERS + port_count * 0x10 && !( offset & 0xF ) ) {

			port_write ( static_cast < int32_t > ( ( offset - PORT_REGISTERS ) / 0x10 ), value );

			return;

		}

		if ( offset >= DOORBELLS && offset <= DOORBELLS + slot_count * 4 ) {

			uint32_t target = ( offset - DOORBELLS ) / 4;

			if ( !target ) {

				if ( !( value & 0xFF ) )
					run_commands ( );

			} else
				ring_doorbell ( target, value & 0xFF );

		}

	}

	uint64_t xhci::read ( uint64_t address, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( size == 8 && !( offset & 7 ) )
			return static_cast < uint64_t > ( register_read ( offset ) ) | static_cast < uint64_t > ( register_read ( offset + 4 ) ) << 32;

		uint32_t value = register_read ( offset & ~3u ) >> ( offset % 4 * 8 );

		return size >= 4 ? value : value & ( ( 1u << ( size * 8 ) ) - 1 );

	}

	void xhci::write ( uint64_t address, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( offset & 3 )
			return;

		if ( size == 8 ) {

			write ( address, 4, static_cast < uint32_t > ( value ) );
			write ( address + 4, 4, static_cast < uint32_t > ( value >> 32 ) );

			return;

		}

		if ( size == 4 )
			register_write ( offset, static_cast < uint32_t > ( value ) );

	}

	xhci::trb xhci::fetch ( uint64_t address ) {

		uint8_t bytes [ 16 ] = { };
		trb item;

		vm.read ( address, bytes, 16 );

		for ( int32_t i = 0; i < 8; i++ )
			item.parameter |= static_cast < uint64_t > ( bytes [ i ] ) << ( i * 8 );

		for ( int32_t i = 0; i < 4; i++ ) {

			item.status |= static_cast < uint32_t > ( bytes [ 8 + i ] ) << ( i * 8 );
			item.control |= static_cast < uint32_t > ( bytes [ 12 + i ] ) << ( i * 8 );

		}

		return item;

	}

	bool xhci::peek ( ring& item, trb& out, uint64_t& at ) {

		for ( int32_t links = 0; links < 8; links++ ) {

			if ( !item.dequeue )
				return false;

			out = fetch ( item.dequeue );

			if ( ( ( out.control & TRB_CYCLE ) != 0 ) != item.cycle )
				return false;

			if ( ( out.control >> 10 & 0x3F ) != TYPE_LINK ) {

				at = item.dequeue;

				return true;

			}

			if ( out.control & TRB_TOGGLE )
				item.cycle = !item.cycle;

			item.dequeue = out.parameter & ~0xFull;

		}

		return false;

	}

	void xhci::post ( const trb& event ) {

		if ( !segment_size || !( command & COMMAND_RUN ) )
			return;

		uint64_t address = segment_base + static_cast < uint64_t > ( enqueue ) * 16;
		uint32_t control = ( event.control & ~TRB_CYCLE ) | ( producer_cycle ? TRB_CYCLE : 0 );

		write32 ( address, static_cast < uint32_t > ( event.parameter ) );
		write32 ( address + 4, static_cast < uint32_t > ( event.parameter >> 32 ) );
		write32 ( address + 8, event.status );
		write32 ( address + 12, control );
		events_posted++;

		if ( ++enqueue >= segment_size ) {

			if ( ++segment_index >= table_size ) {

				segment_index = 0;
				producer_cycle = !producer_cycle;

			}

			load_segment ( );

		}

		management |= INTERRUPT_PENDING;
		status |= STATUS_EVENT;
		update_line ( );

	}

	void xhci::transfer_event ( uint64_t at, uint32_t code, uint32_t residual, uint32_t slot, uint32_t target, bool data ) {

		trb event;

		event.parameter = at;
		event.status = code << 24 | ( residual & 0xFFFFFF );
		event.control = TYPE_TRANSFER_EVENT << 10 | target << 16 | slot << 24 | ( data ? TRB_EVENT_DATA : 0 );
		post ( event );

	}

	void xhci::port_event ( int32_t port ) {

		trb event;

		event.parameter = static_cast < uint64_t > ( port + 1 ) << 24;
		event.status = CODE_SUCCESS << 24;
		event.control = TYPE_PORT_EVENT << 10;
		status |= STATUS_PORT_CHANGE;
		post ( event );

	}

	uint64_t xhci::output_context ( uint32_t slot ) {

		if ( !device_contexts )
			return 0;

		return read64 ( device_contexts + static_cast < uint64_t > ( slot ) * 8 ) & ~0x3Full;

	}

	void xhci::set_endpoint_state ( uint32_t slot, uint32_t target, uint8_t state ) {

		uint64_t out = output_context ( slot );

		if ( !out )
			return;

		uint64_t at = out + static_cast < uint64_t > ( target ) * CONTEXT_SIZE;
		const endpoint_state& item = slots [ slot ].endpoints [ target ];
		uint64_t pointer = item.transfer.dequeue | ( item.transfer.cycle ? 1 : 0 );

		write32 ( at, ( read32 ( at ) & ~7u ) | state );
		write32 ( at + 8, static_cast < uint32_t > ( pointer ) );
		write32 ( at + 12, static_cast < uint32_t > ( pointer >> 32 ) );

	}

	usb_device* xhci::device_of ( uint32_t slot ) {

		const slot_state& item = slots [ slot ];

		if ( !item.enabled || item.port < 0 || !devices [ item.port ] || !( ports [ item.port ] & PORT_ENABLED ) )
			return nullptr;

		return devices [ item.port ]->route ( item.address );

	}

	void xhci::run_commands ( ) {

		if ( !( command & COMMAND_RUN ) )
			return;

		command_running = true;

		for ( int32_t step = 0; step < 256 && command_running; step++ ) {

			trb item;
			uint64_t at = 0;

			if ( !peek ( commands, item, at ) )
				break;

			commands.dequeue += 16;

			uint32_t slot = item.control >> 24;
			uint32_t code = run_command ( item, slot );
			trb done;

			commands_run++;
			done.parameter = at;
			done.status = code << 24;
			done.control = TYPE_COMMAND_EVENT << 10 | slot << 24;
			post ( done );

		}

	}

	uint32_t xhci::run_command ( const trb& item, uint32_t& slot ) {

		uint32_t type = item.control >> 10 & 0x3F;
		uint32_t target = item.control >> 16 & 0x1F;
		bool known = slot >= 1 && slot <= slot_count && slots [ slot ].enabled;

		if ( type == TYPE_ENABLE_SLOT )
			return enable_slot ( slot );

		if ( type == TYPE_NOOP_COMMAND ) {

			slot = 0;

			return CODE_SUCCESS;

		}

		if ( type < TYPE_DISABLE_SLOT || type > TYPE_RESET_DEVICE )
			return CODE_TRB;

		if ( !known )
			return CODE_SLOT_DISABLED;

		switch ( type ) {

			case TYPE_DISABLE_SLOT: {

				uint64_t out = output_context ( slot );

				slots [ slot ] = slot_state ( );
				waiting [ slot ] = 0;
				rung [ slot ] = 0;

				if ( out )
					write32 ( out + 12, 0 );

				return CODE_SUCCESS;

			}

			case TYPE_ADDRESS_DEVICE:
				return address_device ( slot, item.parameter & ~0xFull, ( item.control & TRB_BLOCK ) != 0 );

			case TYPE_CONFIGURE_ENDPOINT:
				return configure_endpoints ( slot, item.parameter & ~0xFull, ( item.control & TRB_BLOCK ) != 0 );

			case TYPE_EVALUATE_CONTEXT:
				return evaluate_context ( slot, item.parameter & ~0xFull );

			case TYPE_RESET_ENDPOINT:
				return reset_endpoint ( slot, target );

			case TYPE_STOP_ENDPOINT:
				return stop_endpoint ( slot, target );

			case TYPE_SET_DEQUEUE:
				return set_dequeue ( slot, target, item.parameter );

			case TYPE_RESET_DEVICE: {

				slot_state& owner = slots [ slot ];

				for ( uint32_t i = 2; i < endpoint_count; i++ ) {

					if ( owner.endpoints [ i ].state ) {

						owner.endpoints [ i ] = endpoint_state ( );
						set_endpoint_state ( slot, i, ENDPOINT_DISABLED );

					}

				}

				owner.state = SLOT_DEFAULT;
				owner.address = 0;
				waiting [ slot ] = 0;

				uint64_t out = output_context ( slot );

				if ( out )
					write32 ( out + 12, static_cast < uint32_t > ( SLOT_DEFAULT ) << 27 );

				return CODE_SUCCESS;

			}

			default:
				return CODE_TRB;

		}

	}

	uint32_t xhci::enable_slot ( uint32_t& slot ) {

		for ( uint32_t i = 1; i <= configured_slots; i++ ) {

			if ( slots [ i ].enabled )
				continue;

			slots [ i ] = slot_state ( );
			slots [ i ].enabled = true;
			slots [ i ].state = SLOT_ENABLED;
			waiting [ i ] = 0;
			slot = i;

			return CODE_SUCCESS;

		}

		slot = 0;

		return CODE_NO_SLOTS;

	}

	uint32_t xhci::address_device ( uint32_t slot, uint64_t input, bool block ) {

		slot_state& owner = slots [ slot ];
		uint64_t out = output_context ( slot );

		if ( owner.state >= SLOT_ADDRESSED || ( owner.state == SLOT_DEFAULT && block ) )
			return CODE_CONTEXT_STATE;

		if ( !out || ( read32 ( input + 4 ) & 3 ) != 3 )
			return CODE_PARAMETER;

		uint32_t words [ 8 ];
		uint32_t zero [ 8 ];

		for ( uint32_t i = 0; i < 8; i++ ) {

			words [ i ] = read32 ( input + CONTEXT_SIZE + i * 4 );
			zero [ i ] = read32 ( input + CONTEXT_SIZE * 2 + i * 4 );

		}

		int32_t port = static_cast < int32_t > ( words [ 1 ] >> 16 & 0xFF ) - 1;

		if ( port < 0 || port >= port_count || !devices [ port ] || !( ports [ port ] & PORT_ENABLED ) )
			return CODE_TRANSACTION;

		endpoint_state& control = owner.endpoints [ 1 ];
		uint64_t pointer = static_cast < uint64_t > ( zero [ 2 ] ) | static_cast < uint64_t > ( zero [ 3 ] ) << 32;

		owner.port = port;
		control = endpoint_state ( );
		control.state = ENDPOINT_RUNNING;
		control.type = ENDPOINT_CONTROL;
		control.max_packet = static_cast < uint16_t > ( zero [ 1 ] >> 16 ? zero [ 1 ] >> 16 : 8 );
		control.transfer.dequeue = pointer & ~0xFull;
		control.transfer.cycle = ( pointer & 1 ) != 0;

		if ( !block ) {

			usb_device* device = devices [ port ]->route ( 0 );
			uint8_t setup [ 8 ] = { 0x00, 0x05, static_cast < uint8_t > ( slot ), 0, 0, 0, 0, 0 };

			if ( !device || device->transfer ( pid_setup, 0, setup, 8 ) < 0 || device->transfer ( pid_in, 0, scratch, 0 ) < 0 ) {

				control = endpoint_state ( );

				return CODE_TRANSACTION;

			}

			owner.address = static_cast < uint8_t > ( slot );
			owner.state = SLOT_ADDRESSED;

		} else {

			owner.address = 0;
			owner.state = SLOT_DEFAULT;

		}

		words [ 3 ] = owner.address | static_cast < uint32_t > ( owner.state ) << 27;
		zero [ 0 ] = ( zero [ 0 ] & ~7u ) | ENDPOINT_RUNNING;

		for ( uint32_t i = 0; i < 8; i++ ) {

			write32 ( out + i * 4, words [ i ] );
			write32 ( out + CONTEXT_SIZE + i * 4, zero [ i ] );

		}

		return CODE_SUCCESS;

	}

	uint32_t xhci::configure_endpoints ( uint32_t slot, uint64_t input, bool deconfigure ) {

		slot_state& owner = slots [ slot ];
		uint64_t out = output_context ( slot );

		if ( owner.state < SLOT_ADDRESSED )
			return CODE_CONTEXT_STATE;

		if ( !out )
			return CODE_PARAMETER;

		uint32_t drop = deconfigure ? 0xFFFFFFFCu : read32 ( input );
		uint32_t add = deconfigure ? 0 : read32 ( input + 4 );

		for ( uint32_t target = 2; target < endpoint_count; target++ ) {

			uint32_t bit = 1u << target;

			if ( add & bit ) {

				uint32_t words [ 8 ];

				for ( uint32_t i = 0; i < 8; i++ )
					words [ i ] = read32 ( input + CONTEXT_SIZE * ( target + 1 ) + i * 4 );

				if ( !( words [ 1 ] >> 16 ) || !( words [ 1 ] >> 3 & 7 ) )
					return CODE_PARAMETER;

			}

		}

		for ( uint32_t target = 2; target < endpoint_count; target++ ) {

			uint32_t bit = 1u << target;
			endpoint_state& item = owner.endpoints [ target ];

			if ( ( drop & bit ) && item.state ) {

				item = endpoint_state ( );
				waiting [ slot ] &= ~bit;
				set_endpoint_state ( slot, target, ENDPOINT_DISABLED );

			}

			if ( !( add & bit ) )
				continue;

			uint32_t words [ 8 ];

			for ( uint32_t i = 0; i < 8; i++ )
				words [ i ] = read32 ( input + CONTEXT_SIZE * ( target + 1 ) + i * 4 );

			uint64_t pointer = static_cast < uint64_t > ( words [ 2 ] ) | static_cast < uint64_t > ( words [ 3 ] ) << 32;

			item = endpoint_state ( );
			item.state = ENDPOINT_RUNNING;
			item.type = static_cast < uint8_t > ( words [ 1 ] >> 3 & 7 );
			item.max_packet = static_cast < uint16_t > ( words [ 1 ] >> 16 );
			item.transfer.dequeue = pointer & ~0xFull;
			item.transfer.cycle = ( pointer & 1 ) != 0;
			waiting [ slot ] &= ~bit;
			words [ 0 ] = ( words [ 0 ] & ~7u ) | ENDPOINT_RUNNING;

			for ( uint32_t i = 0; i < 8; i++ )
				write32 ( out + CONTEXT_SIZE * target + i * 4, words [ i ] );

		}

		if ( add & 1 ) {

			for ( uint32_t i = 0; i < 3; i++ )
				write32 ( out + i * 4, read32 ( input + CONTEXT_SIZE + i * 4 ) );

		}

		bool any = false;

		for ( uint32_t target = 2; target < endpoint_count; target++ )
			any = any || owner.endpoints [ target ].state != ENDPOINT_DISABLED;

		owner.state = any ? SLOT_CONFIGURED : SLOT_ADDRESSED;
		write32 ( out + 12, owner.address | static_cast < uint32_t > ( owner.state ) << 27 );

		return CODE_SUCCESS;

	}

	uint32_t xhci::evaluate_context ( uint32_t slot, uint64_t input ) {

		slot_state& owner = slots [ slot ];
		uint64_t out = output_context ( slot );

		if ( owner.state == SLOT_ENABLED )
			return CODE_CONTEXT_STATE;

		if ( !out )
			return CODE_PARAMETER;

		uint32_t add = read32 ( input + 4 );

		if ( add & 1 ) {

			uint32_t first = read32 ( input + CONTEXT_SIZE );

			write32 ( out, ( first & 0x07FFFFFFu ) | ( read32 ( out ) & 0xF8000000u ) );
			write32 ( out + 4, read32 ( input + CONTEXT_SIZE + 4 ) );
			write32 ( out + 8, read32 ( input + CONTEXT_SIZE + 8 ) );

		}

		if ( add & 2 ) {

			uint32_t packet = read32 ( input + CONTEXT_SIZE * 2 + 4 ) >> 16;

			if ( !packet )
				return CODE_PARAMETER;

			owner.endpoints [ 1 ].max_packet = static_cast < uint16_t > ( packet );
			write32 ( out + CONTEXT_SIZE + 4, ( read32 ( out + CONTEXT_SIZE + 4 ) & 0xFFFF ) | packet << 16 );

		}

		return CODE_SUCCESS;

	}

	uint32_t xhci::reset_endpoint ( uint32_t slot, uint32_t target ) {

		if ( target < 1 || target >= endpoint_count )
			return CODE_TRB;

		endpoint_state& item = slots [ slot ].endpoints [ target ];

		if ( item.state != ENDPOINT_HALTED )
			return CODE_CONTEXT_STATE;

		item.state = ENDPOINT_STOPPED;
		item.progress = 0;
		item.skipping = false;
		set_endpoint_state ( slot, target, ENDPOINT_STOPPED );

		return CODE_SUCCESS;

	}

	uint32_t xhci::stop_endpoint ( uint32_t slot, uint32_t target ) {

		if ( target < 1 || target >= endpoint_count )
			return CODE_TRB;

		endpoint_state& item = slots [ slot ].endpoints [ target ];

		if ( item.state == ENDPOINT_DISABLED )
			return CODE_CONTEXT_STATE;

		if ( item.state == ENDPOINT_RUNNING )
			item.state = ENDPOINT_STOPPED;

		waiting [ slot ] &= ~( 1u << target );
		set_endpoint_state ( slot, target, item.state );

		return CODE_SUCCESS;

	}

	uint32_t xhci::set_dequeue ( uint32_t slot, uint32_t target, uint64_t pointer ) {

		if ( target < 1 || target >= endpoint_count )
			return CODE_TRB;

		endpoint_state& item = slots [ slot ].endpoints [ target ];

		if ( item.state != ENDPOINT_STOPPED && item.state != ENDPOINT_ERROR )
			return CODE_CONTEXT_STATE;

		if ( !( pointer & ~0xFull ) )
			return CODE_PARAMETER;

		item.transfer.dequeue = pointer & ~0xFull;
		item.transfer.cycle = ( pointer & 1 ) != 0;
		item.progress = 0;
		item.skipping = false;
		set_endpoint_state ( slot, target, item.state );

		return CODE_SUCCESS;

	}

	void xhci::ring_doorbell ( uint32_t slot, uint32_t target ) {

		if ( !( command & COMMAND_RUN ) || slot > slot_count || !slots [ slot ].enabled || target < 1 || target >= endpoint_count )
			return;

		endpoint_state& item = slots [ slot ].endpoints [ target ];

		if ( item.state == ENDPOINT_STOPPED ) {

			item.state = ENDPOINT_RUNNING;
			set_endpoint_state ( slot, target, ENDPOINT_RUNNING );

		}

		if ( item.state == ENDPOINT_RUNNING )
			rung [ slot ] |= 1u << target;

	}

	int32_t xhci::move_data ( usb_device* device, uint32_t endpoint, const trb& item, endpoint_state& state ) {

		uint32_t type = item.control >> 10 & 0x3F;
		uint32_t length = item.status & 0x1FFFF;
		bool in = type == TYPE_DATA ? ( item.control & TRB_IN ) != 0 : ( endpoint & 0x80 ) != 0;
		uint8_t number = static_cast < uint8_t > ( endpoint & 0x0F );
		uint32_t packet = state.max_packet ? state.max_packet : 8;
		uint8_t immediate [ 8 ];

		if ( packet > sizeof ( scratch ) )
			packet = sizeof ( scratch );

		for ( int32_t i = 0; i < 8; i++ )
			immediate [ i ] = static_cast < uint8_t > ( item.parameter >> ( i * 8 ) );

		while ( state.progress < length ) {

			uint32_t count = length - state.progress < packet ? length - state.progress : packet;
			int32_t result = 0;

			if ( in ) {

				result = device->transfer ( pid_in, number, scratch, static_cast < int32_t > ( count ) );

				if ( result == usb_nak )
					return usb_nak;

				if ( result < 0 ) {

					state.progress = 0;

					return result;

				}

				if ( static_cast < uint32_t > ( result ) > count )
					result = static_cast < int32_t > ( count );

				if ( result )
					vm.write ( item.parameter + state.progress, scratch, static_cast < uint32_t > ( result ) );

				state.progress += static_cast < uint32_t > ( result );

				if ( static_cast < uint32_t > ( result ) < count )
					break;

			} else {

				if ( item.control & TRB_IMMEDIATE ) {

					for ( uint32_t i = 0; i < count && state.progress + i < 8; i++ )
						scratch [ i ] = immediate [ state.progress + i ];

				} else
					vm.read ( item.parameter + state.progress, scratch, count );

				result = device->transfer ( pid_out, number, scratch, static_cast < int32_t > ( count ) );

				if ( result == usb_nak )
					return usb_nak;

				if ( result < 0 ) {

					state.progress = 0;

					return result;

				}

				state.progress += count;

			}

		}

		int32_t moved = static_cast < int32_t > ( state.progress );

		state.progress = 0;

		return moved;

	}

	void xhci::run_endpoint ( uint32_t slot, uint32_t target ) {

		endpoint_state& item = slots [ slot ].endpoints [ target ];
		usb_device* device = device_of ( slot );
		uint32_t endpoint = target / 2 | ( target & 1 ? 0x80 : 0 );

		waiting [ slot ] &= ~( 1u << target );

		for ( int32_t step = 0; step < 1024 && item.state == ENDPOINT_RUNNING; step++ ) {

			trb work;
			uint64_t at = 0;

			if ( !peek ( item.transfer, work, at ) )
				return;

			uint32_t type = work.control >> 10 & 0x3F;

			if ( item.skipping ) {

				item.skipping = ( work.control & TRB_CHAIN ) != 0;
				item.transfer.dequeue += 16;

				continue;

			}

			uint32_t length = work.status & 0x1FFFF;
			bool data = false;
			int32_t result = 0;

			switch ( type ) {

				case TYPE_SETUP: {

					uint8_t packet [ 8 ];

					for ( int32_t i = 0; i < 8; i++ )
						packet [ i ] = static_cast < uint8_t > ( work.parameter >> ( i * 8 ) );

					result = device ? device->transfer ( pid_setup, 0, packet, 8 ) : usb_timeout;
					break;

				}

				case TYPE_DATA:
				case TYPE_NORMAL:
					data = true;
					result = device ? move_data ( device, endpoint, work, item ) : usb_timeout;
					break;

				case TYPE_STATUS:
					result = device ? device->transfer ( work.control & TRB_IN ? pid_in : pid_out, 0, scratch, 0 ) : usb_timeout;
					break;

				case TYPE_EVENT_DATA:
					item.transfer.dequeue += 16;

					if ( work.control & TRB_IOC )
						transfer_event ( work.parameter, CODE_SUCCESS, 0, slot, target, true );

					continue;

				case TYPE_NOOP:
					break;

				default:
					item.transfer.dequeue += 16;
					transfer_event ( at, CODE_TRB, 0, slot, target, false );
					continue;

			}

			if ( result == usb_nak ) {

				waiting [ slot ] |= 1u << target;

				return;

			}

			transfers_run++;

			if ( result < 0 ) {

				item.state = ENDPOINT_HALTED;
				item.progress = 0;
				set_endpoint_state ( slot, target, ENDPOINT_HALTED );
				transfer_event ( at, result == usb_stall ? CODE_STALL : CODE_TRANSACTION, data ? length : 0, slot, target, false );

				return;

			}

			item.transfer.dequeue += 16;

			uint32_t residual = data && static_cast < uint32_t > ( result ) < length ? length - static_cast < uint32_t > ( result ) : 0;
			uint32_t code = CODE_SUCCESS;
			bool report = ( work.control & TRB_IOC ) != 0;

			if ( residual ) {

				code = CODE_SHORT;
				report = report || ( work.control & TRB_SHORT );
				item.skipping = ( work.control & TRB_CHAIN ) != 0;

			}

			if ( report )
				transfer_event ( at, code, residual, slot, target, false );

		}

	}

	void xhci::advance ( uint64_t now ) {

		if ( !( command & COMMAND_RUN ) ) {

			last_retry = now;

			return;

		}

		for ( uint32_t slot = 1; slot <= slot_count; slot++ ) {

			uint32_t mask = rung [ slot ];

			rung [ slot ] = 0;

			for ( uint32_t target = 1; mask && target < endpoint_count; target++ ) {

				if ( ( mask & 1u << target ) && slots [ slot ].enabled && slots [ slot ].endpoints [ target ].state == ENDPOINT_RUNNING )
					run_endpoint ( slot, target );

			}

		}

		if ( now < last_retry + retry_time )
			return;

		last_retry = now;

		for ( uint32_t slot = 1; slot <= slot_count; slot++ ) {

			uint32_t mask = waiting [ slot ];

			for ( uint32_t target = 1; mask && target < endpoint_count; target++ ) {

				if ( mask & 1u << target )
					run_endpoint ( slot, target );

			}

		}

	}

	void xhci::update_line ( ) {

		bool wanted = ( command & COMMAND_INTERRUPTS ) && ( management & INTERRUPT_ENABLE ) && ( management & INTERRUPT_PENDING );

		if ( wanted == line )
			return;

		line = wanted;
		bus.set_irq ( irq, wanted );

	}

}