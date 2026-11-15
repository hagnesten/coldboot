#include "pc/ahci.hh"
#include "runtime/runtime.hh"

namespace pc {

	enum : uint32_t {

		HOST_CAPABILITIES = 0x00,
		HOST_CONTROL = 0x04,
		HOST_INTERRUPTS = 0x08,
		HOST_PORTS = 0x0C,
		HOST_VERSION = 0x10,
		HOST_CAPABILITIES_2 = 0x24,

		PORT_LIST = 0x00,
		PORT_LIST_HIGH = 0x04,
		PORT_RECEIVED = 0x08,
		PORT_RECEIVED_HIGH = 0x0C,
		PORT_STATUS = 0x10,
		PORT_ENABLE = 0x14,
		PORT_COMMAND = 0x18,
		PORT_TASK = 0x20,
		PORT_SIGNATURE = 0x24,
		PORT_LINK = 0x28,
		PORT_LINK_CONTROL = 0x2C,
		PORT_LINK_ERROR = 0x30,
		PORT_ACTIVE = 0x34,
		PORT_ISSUED = 0x38,

		CONTROL_RESET = 1u << 0,
		CONTROL_INTERRUPTS = 1u << 1,
		CONTROL_AHCI = 1u << 31,

		COMMAND_START = 1u << 0,
		COMMAND_RECEIVE = 1u << 4,
		COMMAND_RECEIVING = 1u << 14,
		COMMAND_RUNNING = 1u << 15,

		STATUS_REGISTER_FIS = 1u << 0,
		STATUS_TASK_ERROR = 1u << 30,

		DISK_SIGNATURE = 0x00000101,
		LINK_UP = 0x00000113,

		FIS_HOST_TO_DEVICE = 0x27,
		FIS_DEVICE_TO_HOST = 0x34,

		ATA_READY = 0x50,
		ATA_ERROR = 0x01,
		ATA_ABORTED = 0x04,
		ATA_IDENTIFY = 0xEC,
		ATA_READ_DMA = 0xC8,
		ATA_READ_DMA_EXT = 0x25,
		ATA_WRITE_DMA = 0xCA,
		ATA_WRITE_DMA_EXT = 0x35,
		ATA_FLUSH = 0xE7,
		ATA_FLUSH_EXT = 0xEA,
		ATA_SET_FEATURES = 0xEF,
		ATA_IDLE = 0xE3,
		ATA_CHECK_POWER = 0xE5,

	};

	ahci::ahci ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		buffer = static_cast < uint8_t* > ( runtime_allocate_zero ( transfer_limit ) );
		identify ( 0x8086, 0x2922, 0x01, 0x06, 0x01, 0x02, 0x00 );
		set_bar ( 5, memory_size, false, default_memory );
		set_interrupt ( 1, 11 );
		config [ 0x04 ] = 0x06;
		config [ 0x2C ] = 0x86;
		config [ 0x2D ] = 0x80;
		config [ 0x2E ] = 0x22;
		config [ 0x2F ] = 0x29;
		reset_controller ( );

	}

	ahci::~ahci ( ) {

		runtime_release ( buffer );

	}

	void ahci::reset_controller ( ) {

		global_control = CONTROL_AHCI;
		interrupt_status = 0;
		reset_port ( );
		update_line ( );

	}

	void ahci::reset_port ( ) {

		port = port_state ( );
		port.task = disk && disk->present ( ) ? ATA_READY : 0x7F;

	}

	void ahci::written ( uint32_t offset ) {

		if ( offset < 0x24 || offset >= 0x28 )
			return;

		uint32_t wanted = bar_address ( 5 );

		if ( wanted && wanted != memory_base ) {

			vm.relocate_memory ( this, memory_base, wanted );
			memory_base = wanted;

		}

	}

	uint32_t ahci::port_read ( uint32_t offset ) {

		bool attached = disk && disk->present ( );

		switch ( offset ) {

			case PORT_LIST:
				return static_cast < uint32_t > ( port.list );

			case PORT_LIST_HIGH:
				return static_cast < uint32_t > ( port.list >> 32 );

			case PORT_RECEIVED:
				return static_cast < uint32_t > ( port.received );

			case PORT_RECEIVED_HIGH:
				return static_cast < uint32_t > ( port.received >> 32 );

			case PORT_STATUS:
				return port.status;

			case PORT_ENABLE:
				return port.enable;

			case PORT_COMMAND:
				return port.command;

			case PORT_TASK:
				return port.task;

			case PORT_SIGNATURE:
				return attached ? DISK_SIGNATURE : 0xFFFFFFFF;

			case PORT_LINK:
				return attached ? LINK_UP : 0;

			case PORT_LINK_CONTROL:
				return port.control;

			case PORT_LINK_ERROR:
				return port.error;

			case PORT_ACTIVE:
				return port.active;

			case PORT_ISSUED:
				return port.issued;

			default:
				return 0;

		}

	}

	void ahci::port_write ( uint32_t offset, uint32_t value ) {

		switch ( offset ) {

			case PORT_LIST:
				port.list = ( port.list & 0xFFFFFFFF00000000ull ) | ( value & ~0x3FFu );
				break;

			case PORT_LIST_HIGH:
				port.list = ( port.list & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				break;

			case PORT_RECEIVED:
				port.received = ( port.received & 0xFFFFFFFF00000000ull ) | ( value & ~0xFFu );
				break;

			case PORT_RECEIVED_HIGH:
				port.received = ( port.received & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				break;

			case PORT_STATUS:
				port.status &= ~value;
				break;

			case PORT_ENABLE:
				port.enable = value;
				break;

			case PORT_COMMAND: {

				uint32_t kept = value & ~( COMMAND_RECEIVING | COMMAND_RUNNING );

				port.command = kept | ( kept & COMMAND_START ? COMMAND_RUNNING : 0 ) | ( kept & COMMAND_RECEIVE ? COMMAND_RECEIVING : 0 );

				if ( !( kept & COMMAND_START ) )
					port.issued = 0;

				break;

			}

			case PORT_LINK_CONTROL:
				port.control = value;

				if ( ( value & 0xF ) == 1 )
					reset_port ( );

				break;

			case PORT_LINK_ERROR:
				port.error &= ~value;
				break;

			case PORT_ACTIVE:
				port.active |= value;
				break;

			case PORT_ISSUED:
				if ( port.command & COMMAND_START ) {

					port.issued |= value;
					run_commands ( );

				}

				break;

			default:
				break;

		}

		if ( port.status & port.enable )
			interrupt_status |= 1;

		update_line ( );

	}

	uint64_t ahci::read ( uint64_t address, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base ) & ~3u;
		uint32_t value = 0;

		if ( offset >= port_base && offset < port_base + port_stride )
			value = port_read ( offset - port_base );
		else if ( offset == HOST_CAPABILITIES )
			value = 1u << 31 | 1u << 18 | 1u << 20 | ( slot_count - 1 ) << 8;
		else if ( offset == HOST_CONTROL )
			value = global_control;
		else if ( offset == HOST_INTERRUPTS )
			value = interrupt_status;
		else if ( offset == HOST_PORTS )
			value = 1;
		else if ( offset == HOST_VERSION )
			value = 0x00010300;

		uint32_t shift = static_cast < uint32_t > ( address - memory_base ) % 4 * 8;

		value >>= shift;

		return size >= 4 ? value : value & ( ( 1u << ( size * 8 ) ) - 1 );

	}

	void ahci::write ( uint64_t address, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( size != 4 || ( offset & 3 ) )
			return;

		uint32_t word = static_cast < uint32_t > ( value );

		if ( offset >= port_base && offset < port_base + port_stride ) {

			port_write ( offset - port_base, word );

			return;

		}

		if ( offset == HOST_CONTROL ) {

			if ( word & CONTROL_RESET ) {

				reset_controller ( );

				return;

			}

			global_control = ( word & CONTROL_INTERRUPTS ) | CONTROL_AHCI;
			update_line ( );

		} else if ( offset == HOST_INTERRUPTS ) {

			interrupt_status &= ~word;

			if ( port.status & port.enable )
				interrupt_status |= 1;

			update_line ( );

		}

	}

	void ahci::run_commands ( ) {

		for ( uint32_t slot = 0; slot < slot_count && port.issued; slot++ ) {

			if ( !( port.issued & 1u << slot ) )
				continue;

			bool ok = run_slot ( slot );

			port.issued &= ~( 1u << slot );
			port.active &= ~( 1u << slot );
			commands_run++;

			if ( !ok ) {

				port.issued = 0;

				break;

			}

		}

	}

	static void put16 ( uint8_t* at, uint16_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );

	}

	static void put_text ( uint8_t* words, uint32_t first, uint32_t count, const char* text ) {

		bool ended = false;

		for ( uint32_t i = 0; i < count * 2; i++ ) {

			char value = ' ';

			if ( !ended && text [ i ] )
				value = text [ i ];
			else
				ended = true;

			words [ first * 2 + ( i ^ 1 ) ] = static_cast < uint8_t > ( value );

		}

	}

	void ahci::describe_disk ( uint8_t* out ) const {

		uint64_t sectors = disk ? disk->size / sector_size : 0;
		uint64_t small = sectors > 0x0FFFFFFF ? 0x0FFFFFFF : sectors;

		for ( uint32_t i = 0; i < sector_size; i++ )
			out [ i ] = 0;

		put16 ( out + 0, 0x0040 );
		put_text ( out, 10, 10, "CB0001" );
		put_text ( out, 23, 4, "1.0" );
		put_text ( out, 27, 20, "Coldboot SATA Disk" );
		put16 ( out + 47 * 2, 0x8001 );
		put16 ( out + 49 * 2, 0x0300 );
		put16 ( out + 53 * 2, 0x0006 );
		put16 ( out + 60 * 2, static_cast < uint16_t > ( small ) );
		put16 ( out + 61 * 2, static_cast < uint16_t > ( small >> 16 ) );
		put16 ( out + 63 * 2, 0x0007 );
		put16 ( out + 76 * 2, 0x0002 );
		put16 ( out + 80 * 2, 0x01F0 );
		put16 ( out + 82 * 2, 0x4000 );
		put16 ( out + 83 * 2, 0x7400 );
		put16 ( out + 84 * 2, 0x4000 );
		put16 ( out + 85 * 2, 0x4000 );
		put16 ( out + 86 * 2, 0x3400 );
		put16 ( out + 87 * 2, 0x4000 );
		put16 ( out + 88 * 2, 0x007F );

		for ( uint32_t i = 0; i < 4; i++ )
			put16 ( out + ( 100 + i ) * 2, static_cast < uint16_t > ( sectors >> ( i * 16 ) ) );

		put16 ( out + 106 * 2, 0x4000 );
		put16 ( out + 217 * 2, 0x0001 );

	}

	bool ahci::transfer ( uint64_t table, uint32_t entries, uint64_t lba, uint32_t count, bool writing, uint32_t& moved ) {

		uint64_t total = static_cast < uint64_t > ( count ) * sector_size;
		uint64_t done = 0;

		moved = 0;

		if ( !disk || !disk->present ( ) || total > transfer_limit || ( lba + count ) * sector_size > disk->size || ( writing && !disk->writable ) )
			return false;

		if ( !writing && !disk->read ( lba * sector_size, buffer, static_cast < uint32_t > ( total ) ) )
			return false;

		for ( uint32_t i = 0; i < entries && done < total; i++ ) {

			uint8_t entry [ 16 ];

			vm.read ( table + 0x80 + i * 16ull, entry, 16 );

			uint64_t base = static_cast < uint64_t > ( entry [ 0 ] | entry [ 1 ] << 8 | entry [ 2 ] << 16 ) | static_cast < uint64_t > ( entry [ 3 ] ) << 24 | static_cast < uint64_t > ( entry [ 4 ] | entry [ 5 ] << 8 | entry [ 6 ] << 16 | static_cast < uint32_t > ( entry [ 7 ] ) << 24 ) << 32;
			uint32_t length = ( static_cast < uint32_t > ( entry [ 12 ] | entry [ 13 ] << 8 | entry [ 14 ] << 16 ) & 0x3FFFFF ) + 1;
			uint32_t piece = static_cast < uint32_t > ( total - done < length ? total - done : length );

			if ( writing )
				vm.read ( base, buffer + done, piece );
			else
				vm.write ( base, buffer + done, piece );

			done += piece;

		}

		if ( done < total )
			return false;

		if ( writing && !disk->write ( lba * sector_size, buffer, static_cast < uint32_t > ( total ) ) )
			return false;

		moved = static_cast < uint32_t > ( total );
		sectors_moved += count;

		return true;

	}

	bool ahci::run_slot ( uint32_t slot ) {

		uint8_t header [ 32 ];
		uint8_t fis [ 64 ];

		vm.read ( port.list + slot * 32ull, header, 32 );

		uint32_t entries = header [ 2 ] | header [ 3 ] << 8;
		uint64_t table = static_cast < uint64_t > ( header [ 8 ] | header [ 9 ] << 8 | header [ 10 ] << 16 | static_cast < uint32_t > ( header [ 11 ] ) << 24 ) | static_cast < uint64_t > ( header [ 12 ] | header [ 13 ] << 8 | header [ 14 ] << 16 | static_cast < uint32_t > ( header [ 15 ] ) << 24 ) << 32;

		vm.read ( table, fis, 64 );

		if ( fis [ 0 ] != FIS_HOST_TO_DEVICE || !( fis [ 1 ] & 0x80 ) ) {

			finish ( ATA_READY | ATA_ERROR, ATA_ABORTED, true );

			return false;

		}

		uint8_t command = fis [ 2 ];
		uint64_t lba = static_cast < uint64_t > ( fis [ 4 ] ) | static_cast < uint64_t > ( fis [ 5 ] ) << 8 | static_cast < uint64_t > ( fis [ 6 ] ) << 16;
		uint32_t count = fis [ 12 ] | fis [ 13 ] << 8;
		uint32_t moved = 0;
		bool ok = true;

		if ( command == ATA_READ_DMA_EXT || command == ATA_WRITE_DMA_EXT ) {

			lba |= static_cast < uint64_t > ( fis [ 8 ] ) << 24 | static_cast < uint64_t > ( fis [ 9 ] ) << 32 | static_cast < uint64_t > ( fis [ 10 ] ) << 40;

			if ( !count )
				count = 65536;

		} else if ( command == ATA_READ_DMA || command == ATA_WRITE_DMA ) {

			lba |= static_cast < uint64_t > ( fis [ 7 ] & 0x0F ) << 24;
			count &= 0xFF;

			if ( !count )
				count = 256;

		}

		switch ( command ) {

			case ATA_IDENTIFY: {

				describe_disk ( buffer );

				uint8_t entry [ 16 ];

				vm.read ( table + 0x80, entry, 16 );

				uint64_t base = static_cast < uint64_t > ( entry [ 0 ] | entry [ 1 ] << 8 | entry [ 2 ] << 16 ) | static_cast < uint64_t > ( entry [ 3 ] ) << 24 | static_cast < uint64_t > ( entry [ 4 ] | entry [ 5 ] << 8 | entry [ 6 ] << 16 | static_cast < uint32_t > ( entry [ 7 ] ) << 24 ) << 32;

				ok = entries > 0 && disk && disk->present ( );

				if ( ok ) {

					vm.write ( base, buffer, sector_size );
					moved = sector_size;

				}

				break;

			}

			case ATA_READ_DMA:
			case ATA_READ_DMA_EXT:
				ok = transfer ( table, entries, lba, count, false, moved );
				break;

			case ATA_WRITE_DMA:
			case ATA_WRITE_DMA_EXT:
				ok = transfer ( table, entries, lba, count, true, moved );
				break;

			case ATA_FLUSH:
			case ATA_FLUSH_EXT:
				if ( disk )
					disk->flush ( );

				break;

			case ATA_SET_FEATURES:
			case ATA_IDLE:
			case ATA_CHECK_POWER:
				break;

			default:
				ok = false;
				break;

		}

		uint8_t written [ 4 ] = { static_cast < uint8_t > ( moved ), static_cast < uint8_t > ( moved >> 8 ), static_cast < uint8_t > ( moved >> 16 ), static_cast < uint8_t > ( moved >> 24 ) };

		vm.write ( port.list + slot * 32ull + 4, written, 4 );
		finish ( ok ? ATA_READY : ATA_READY | ATA_ERROR, ok ? 0 : ATA_ABORTED, !ok );

		return ok;

	}

	void ahci::finish ( uint8_t status, uint8_t error, bool failed ) {

		port.task = status | static_cast < uint32_t > ( error ) << 8;

		if ( port.received && ( port.command & COMMAND_RECEIVE ) ) {

			uint8_t fis [ 20 ] = { };

			fis [ 0 ] = FIS_DEVICE_TO_HOST;
			fis [ 1 ] = 0x40;
			fis [ 2 ] = status;
			fis [ 3 ] = error;
			vm.write ( port.received + 0x40, fis, sizeof ( fis ) );

		}

		port.status |= STATUS_REGISTER_FIS | ( failed ? STATUS_TASK_ERROR : 0 );

		if ( port.status & port.enable )
			interrupt_status |= 1;

		update_line ( );

	}

	void ahci::update_line ( ) {

		bool wanted = ( global_control & CONTROL_INTERRUPTS ) && interrupt_status;

		if ( wanted == line )
			return;

		line = wanted;
		bus.set_irq ( irq, wanted );

	}

}