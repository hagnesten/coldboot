#include "pc/nvme.hh"
#include "runtime/runtime.hh"

namespace pc {

	enum : uint32_t {

		CAPABILITIES = 0x00,
		CAPABILITIES_HIGH = 0x04,
		VERSION = 0x08,
		MASK_SET = 0x0C,
		MASK_CLEAR = 0x10,
		CONFIGURATION = 0x14,
		STATUS = 0x1C,
		ADMIN_ATTRIBUTES = 0x24,
		ADMIN_SUBMISSION = 0x28,
		ADMIN_SUBMISSION_HIGH = 0x2C,
		ADMIN_COMPLETION = 0x30,
		ADMIN_COMPLETION_HIGH = 0x34,
		DOORBELLS = 0x1000,

		CONFIG_ENABLE = 1u << 0,
		CONFIG_SHUTDOWN = 3u << 14,
		STATUS_READY = 1u << 0,
		STATUS_SHUTDOWN_MASK = 3u << 2,
		STATUS_SHUTDOWN_DONE = 2u << 2,
		NVME_VERSION = 0x00010400,
		TIMEOUT_UNITS = 20,
		COMMAND_SET_NVM = 1u << 5,

		ADMIN_DELETE_SUBMISSION = 0x00,
		ADMIN_CREATE_SUBMISSION = 0x01,
		ADMIN_LOG = 0x02,
		ADMIN_DELETE_COMPLETION = 0x04,
		ADMIN_CREATE_COMPLETION = 0x05,
		ADMIN_IDENTIFY = 0x06,
		ADMIN_ABORT = 0x08,
		ADMIN_SET_FEATURES = 0x09,
		ADMIN_GET_FEATURES = 0x0A,
		ADMIN_EVENTS = 0x0C,

		IO_FLUSH = 0x00,
		IO_WRITE = 0x01,
		IO_READ = 0x02,

		IDENTIFY_NAMESPACE = 0,
		IDENTIFY_CONTROLLER = 1,
		IDENTIFY_NAMESPACES = 2,
		FEATURE_QUEUES = 0x07,

		SUCCESS = 0x0000,
		INVALID_OPCODE = 0x0001,
		INVALID_FIELD = 0x0002,
		INTERNAL = 0x0006,
		INVALID_NAMESPACE = 0x000B,
		LBA_RANGE = 0x0080,
		INVALID_COMPLETION = 0x0100,
		INVALID_QUEUE = 0x0101,
		INVALID_SIZE = 0x0102,
		INVALID_DELETION = 0x010C,
		DO_NOT_RETRY = 1u << 14,

		COMMAND_SIZE = 64,
		COMPLETION_SIZE = 16,
		IDENTIFY_SIZE = 4096,

	};

	static void put16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );

	}

	static void put32 ( uint8_t* at, uint32_t value ) {

		put16 ( at, value );
		put16 ( at + 2, value >> 16 );

	}

	static void put64 ( uint8_t* at, uint64_t value ) {

		put32 ( at, static_cast < uint32_t > ( value ) );
		put32 ( at + 4, static_cast < uint32_t > ( value >> 32 ) );

	}

	static uint32_t get32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint64_t get64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( get32 ( at ) ) | static_cast < uint64_t > ( get32 ( at + 4 ) ) << 32;

	}

	static void put_text ( uint8_t* at, uint32_t room, const char* text ) {

		bool ended = false;

		for ( uint32_t i = 0; i < room; i++ ) {

			if ( !ended && !text [ i ] )
				ended = true;

			at [ i ] = static_cast < uint8_t > ( ended ? ' ' : text [ i ] );

		}

	}

	nvme::nvme ( platform_bus& owner, hv::machine& target, uint32_t line_number ) : bus ( owner ), vm ( target ), irq ( line_number ) {

		buffer = static_cast < uint8_t* > ( runtime_allocate_zero ( transfer_limit ) );
		identify ( 0x8086, 0x0953, 0x01, 0x08, 0x02, 0x01, 0x00 );
		set_bar ( 0, memory_size, false, default_memory );
		set_interrupt ( 1, 11 );
		add_msi ( 0x50 );
		config [ 0x04 ] = 0x06;
		config [ 0x2C ] = 0x86;
		config [ 0x2D ] = 0x80;
		config [ 0x2E ] = 0x53;
		config [ 0x2F ] = 0x09;
		reset ( );

	}

	nvme::~nvme ( ) {

		runtime_release ( buffer );

	}

	void nvme::reset ( ) {

		for ( uint32_t i = 0; i < queue_count; i++ ) {

			submissions [ i ] = submission ( );
			completions [ i ] = completion ( );

		}

		status = 0;
		masked = 0;
		rung = 0;
		granted = queue_count - 1;
		update_line ( );

	}

	void nvme::enable ( ) {

		submission& admin = submissions [ 0 ];
		completion& done = completions [ 0 ];

		admin.used = true;
		admin.base = admin_submission;
		admin.size = ( attributes & 0xFFF ) + 1;
		admin.completion = 0;
		done.used = true;
		done.base = admin_completion;
		done.size = ( attributes >> 16 & 0xFFF ) + 1;
		done.phase = true;
		done.interrupts = true;
		status |= STATUS_READY;

	}

	void nvme::written ( uint32_t offset ) {

		if ( offset < 0x10 || offset >= 0x14 )
			return;

		uint32_t wanted = bar_address ( 0 );

		if ( wanted && wanted != memory_base ) {

			vm.relocate_memory ( this, memory_base, wanted );
			memory_base = wanted;

		}

	}

	uint32_t nvme::register_read ( uint32_t offset ) const {

		switch ( offset ) {

			case CAPABILITIES:
				return ( queue_entries - 1 ) | 1u << 16 | TIMEOUT_UNITS << 24;

			case CAPABILITIES_HIGH:
				return COMMAND_SET_NVM;

			case VERSION:
				return NVME_VERSION;

			case MASK_SET:
			case MASK_CLEAR:
				return masked;

			case CONFIGURATION:
				return control;

			case STATUS:
				return status;

			case ADMIN_ATTRIBUTES:
				return attributes;

			case ADMIN_SUBMISSION:
				return static_cast < uint32_t > ( admin_submission );

			case ADMIN_SUBMISSION_HIGH:
				return static_cast < uint32_t > ( admin_submission >> 32 );

			case ADMIN_COMPLETION:
				return static_cast < uint32_t > ( admin_completion );

			case ADMIN_COMPLETION_HIGH:
				return static_cast < uint32_t > ( admin_completion >> 32 );

			default:
				return 0;

		}

	}

	void nvme::register_write ( uint32_t offset, uint32_t value ) {

		switch ( offset ) {

			case MASK_SET:
				masked |= value;
				break;

			case MASK_CLEAR:
				masked &= ~value;
				break;

			case CONFIGURATION: {

				uint32_t before = control;

				control = value;

				if ( !( before & CONFIG_ENABLE ) && ( value & CONFIG_ENABLE ) )
					enable ( );
				else if ( ( before & CONFIG_ENABLE ) && !( value & CONFIG_ENABLE ) )
					reset ( );

				if ( value & CONFIG_SHUTDOWN ) {

					if ( disk )
						disk->flush ( );

					status = ( status & ~STATUS_SHUTDOWN_MASK ) | STATUS_SHUTDOWN_DONE;

				} else
					status &= ~STATUS_SHUTDOWN_MASK;

				break;

			}

			case ADMIN_ATTRIBUTES:
				attributes = value & 0x0FFF0FFF;
				break;

			case ADMIN_SUBMISSION:
				admin_submission = ( admin_submission & 0xFFFFFFFF00000000ull ) | ( value & ~0xFFFu );
				break;

			case ADMIN_SUBMISSION_HIGH:
				admin_submission = ( admin_submission & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				break;

			case ADMIN_COMPLETION:
				admin_completion = ( admin_completion & 0xFFFFFFFF00000000ull ) | ( value & ~0xFFFu );
				break;

			case ADMIN_COMPLETION_HIGH:
				admin_completion = ( admin_completion & 0xFFFFFFFFull ) | static_cast < uint64_t > ( value ) << 32;
				break;

			default:
				break;

		}

		update_line ( );

	}

	uint64_t nvme::read ( uint64_t address, uint32_t size ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( size == 8 && !( offset & 7 ) )
			return static_cast < uint64_t > ( register_read ( offset ) ) | static_cast < uint64_t > ( register_read ( offset + 4 ) ) << 32;

		uint32_t value = register_read ( offset & ~3u ) >> ( offset % 4 * 8 );

		return size >= 4 ? value : value & ( ( 1u << ( size * 8 ) ) - 1 );

	}

	void nvme::write ( uint64_t address, uint32_t size, uint64_t value ) {

		uint32_t offset = static_cast < uint32_t > ( address - memory_base );

		if ( offset & 3 )
			return;

		if ( size == 8 ) {

			write ( address, 4, static_cast < uint32_t > ( value ) );
			write ( address + 4, 4, static_cast < uint32_t > ( value >> 32 ) );

			return;

		}

		if ( size != 4 )
			return;

		if ( offset >= DOORBELLS )
			ring ( ( offset - DOORBELLS ) / 4, static_cast < uint32_t > ( value ) );
		else
			register_write ( offset, static_cast < uint32_t > ( value ) );

	}

	void nvme::ring ( uint32_t doorbell, uint32_t value ) {

		uint32_t queue = doorbell / 2;

		if ( queue >= queue_count || !( status & STATUS_READY ) )
			return;

		if ( doorbell & 1 ) {

			completion& done = completions [ queue ];

			if ( done.used && value < done.size )
				done.head = value;

			update_line ( );

			return;

		}

		submission& work = submissions [ queue ];

		if ( !work.used || value >= work.size )
			return;

		work.tail = value;

		if ( queue )
			rung |= 1u << queue;
		else
			run_queue ( queue );

	}

	void nvme::advance ( uint64_t ) {

		uint32_t waiting = rung;

		rung = 0;

		for ( uint32_t queue = 1; waiting && queue < queue_count; queue++ ) {

			if ( waiting & 1u << queue )
				run_queue ( queue );

		}

	}

	void nvme::run_queue ( uint32_t queue ) {

		submission& work = submissions [ queue ];

		while ( work.used && work.head != work.tail ) {

			uint8_t command [ COMMAND_SIZE ];

			vm.read ( work.base + static_cast < uint64_t > ( work.head ) * COMMAND_SIZE, command, COMMAND_SIZE );
			work.head = ( work.head + 1 ) % work.size;

			result done = queue ? run_io ( command ) : run_admin ( command );

			commands_run++;

			if ( done.post )
				post ( queue, static_cast < uint16_t > ( get32 ( command ) >> 16 ), done );

		}

	}

	nvme::result nvme::run_admin ( const uint8_t* command ) {

		result done;
		uint8_t opcode = command [ 0 ];
		uint32_t space = get32 ( command + 4 );
		uint64_t first = get64 ( command + 24 );
		uint64_t second = get64 ( command + 32 );
		uint32_t ten = get32 ( command + 40 );
		uint32_t eleven = get32 ( command + 44 );
		uint32_t queue = ten & 0xFFFF;
		uint32_t size = ( ten >> 16 ) + 1;

		switch ( opcode ) {

			case ADMIN_IDENTIFY: {

				uint32_t kind = ten & 0xFF;

				for ( uint32_t i = 0; i < IDENTIFY_SIZE; i++ )
					buffer [ i ] = 0;

				if ( kind == IDENTIFY_CONTROLLER )
					describe_controller ( buffer );
				else if ( kind == IDENTIFY_NAMESPACE && space == 1 )
					describe_namespace ( buffer );
				else if ( kind == IDENTIFY_NAMESPACES )
					put32 ( buffer, space < 1 ? 1 : 0 );
				else {

					done.status = kind == IDENTIFY_NAMESPACE ? INVALID_NAMESPACE : INVALID_FIELD;

					break;

				}

				if ( !move ( first, second, IDENTIFY_SIZE, true ) )
					done.status = INVALID_FIELD;

				break;

			}

			case ADMIN_CREATE_COMPLETION:
				if ( !queue || queue >= queue_count || completions [ queue ].used )
					done.status = INVALID_QUEUE;
				else if ( size < 2 || size > queue_entries )
					done.status = INVALID_SIZE;
				else if ( !( eleven & 1 ) || ( first & 0xFFF ) )
					done.status = INVALID_FIELD;
				else {

					completion& made = completions [ queue ];

					made.used = true;
					made.base = first;
					made.size = size;
					made.head = 0;
					made.tail = 0;
					made.phase = true;
					made.interrupts = eleven & 2;

				}

				break;

			case ADMIN_CREATE_SUBMISSION: {

				uint32_t target = eleven >> 16;

				if ( !queue || queue >= queue_count || submissions [ queue ].used )
					done.status = INVALID_QUEUE;
				else if ( size < 2 || size > queue_entries )
					done.status = INVALID_SIZE;
				else if ( !target || target >= queue_count || !completions [ target ].used )
					done.status = INVALID_COMPLETION;
				else if ( !( eleven & 1 ) || ( first & 0xFFF ) )
					done.status = INVALID_FIELD;
				else {

					submission& made = submissions [ queue ];

					made.used = true;
					made.base = first;
					made.size = size;
					made.head = 0;
					made.tail = 0;
					made.completion = static_cast < uint16_t > ( target );

				}

				break;

			}

			case ADMIN_DELETE_SUBMISSION:
				if ( !queue || queue >= queue_count || !submissions [ queue ].used )
					done.status = INVALID_QUEUE;
				else
					submissions [ queue ] = submission ( );

				break;

			case ADMIN_DELETE_COMPLETION: {

				bool busy = false;

				for ( uint32_t i = 1; i < queue_count; i++ ) {

					if ( submissions [ i ].used && submissions [ i ].completion == queue )
						busy = true;

				}

				if ( !queue || queue >= queue_count || !completions [ queue ].used )
					done.status = INVALID_QUEUE;
				else if ( busy )
					done.status = INVALID_DELETION;
				else
					completions [ queue ] = completion ( );

				break;

			}

			case ADMIN_SET_FEATURES:
				if ( ( ten & 0xFF ) == FEATURE_QUEUES ) {

					uint32_t wanted = ( eleven & 0xFFFF ) > ( eleven >> 16 ) ? eleven & 0xFFFF : eleven >> 16;

					granted = wanted + 1 < queue_count - 1 ? wanted + 1 : queue_count - 1;
					done.value = ( granted - 1 ) << 16 | ( granted - 1 );

				}

				break;

			case ADMIN_GET_FEATURES:
				if ( ( ten & 0xFF ) == FEATURE_QUEUES )
					done.value = ( granted - 1 ) << 16 | ( granted - 1 );

				break;

			case ADMIN_LOG: {

				uint32_t length = ( ( ten >> 16 & 0xFFF ) + 1 ) * 4;

				if ( length > IDENTIFY_SIZE )
					length = IDENTIFY_SIZE;

				for ( uint32_t i = 0; i < length; i++ )
					buffer [ i ] = 0;

				if ( !move ( first, second, length, true ) )
					done.status = INVALID_FIELD;

				break;

			}

			case ADMIN_ABORT:
				done.value = 1;
				break;

			case ADMIN_EVENTS:
				done.post = false;
				break;

			default:
				done.status = INVALID_OPCODE;
				break;

		}

		if ( done.status )
			done.status = static_cast < uint16_t > ( done.status | DO_NOT_RETRY );

		return done;

	}

	nvme::result nvme::run_io ( const uint8_t* command ) {

		result done;
		uint8_t opcode = command [ 0 ];
		uint32_t space = get32 ( command + 4 );
		uint64_t first = get64 ( command + 24 );
		uint64_t second = get64 ( command + 32 );
		uint64_t block = get64 ( command + 40 );
		uint32_t blocks = ( get32 ( command + 48 ) & 0xFFFF ) + 1;
		uint64_t bytes = static_cast < uint64_t > ( blocks ) * sector_size;
		bool attached = disk && disk->present ( );

		if ( space != 1 || !attached ) {

			done.status = INVALID_NAMESPACE | DO_NOT_RETRY;

			return done;

		}

		switch ( opcode ) {

			case IO_READ:
			case IO_WRITE:
				if ( bytes > transfer_limit )
					done.status = INVALID_FIELD;
				else if ( ( block + blocks ) * sector_size > disk->size )
					done.status = LBA_RANGE;
				else if ( opcode == IO_READ ) {

					if ( !disk->read ( block * sector_size, buffer, static_cast < uint32_t > ( bytes ) ) || !move ( first, second, static_cast < uint32_t > ( bytes ), true ) )
						done.status = INTERNAL;

				} else if ( !disk->writable || !move ( first, second, static_cast < uint32_t > ( bytes ), false ) || !disk->write ( block * sector_size, buffer, static_cast < uint32_t > ( bytes ) ) )
					done.status = INTERNAL;

				if ( !done.status )
					blocks_moved += blocks;

				break;

			case IO_FLUSH:
				disk->flush ( );
				break;

			default:
				done.status = INVALID_OPCODE;
				break;

		}

		if ( done.status )
			done.status = static_cast < uint16_t > ( done.status | DO_NOT_RETRY );

		return done;

	}

	bool nvme::move ( uint64_t first, uint64_t second, uint32_t length, bool to_guest ) {

		uint32_t done = 0;

		auto copy = [ & ] ( uint64_t address, uint32_t count ) {

			if ( to_guest )
				vm.write ( address, buffer + done, count );
			else
				vm.read ( address, buffer + done, count );

			done += count;

		};

		uint32_t offset = static_cast < uint32_t > ( first % page_size );

		copy ( first, length < page_size - offset ? length : page_size - offset );

		if ( done >= length )
			return true;

		if ( length - done <= page_size ) {

			if ( second % page_size )
				return false;

			copy ( second, length - done );

			return true;

		}

		uint64_t list = second;

		while ( done < length ) {

			if ( list & 7 )
				return false;

			uint64_t entries [ page_size / 8 ];
			uint32_t count = static_cast < uint32_t > ( ( page_size - list % page_size ) / 8 );
			bool chained = false;

			vm.read ( list, reinterpret_cast < uint8_t* > ( entries ), count * 8 );

			for ( uint32_t i = 0; i < count && done < length; i++ ) {

				uint32_t left = length - done;

				if ( i == count - 1 && left > page_size ) {

					list = entries [ i ];
					chained = true;

					break;

				}

				if ( entries [ i ] % page_size )
					return false;

				copy ( entries [ i ], left < page_size ? left : page_size );

			}

			if ( done < length && !chained )
				return false;

		}

		return true;

	}

	void nvme::describe_controller ( uint8_t* out ) const {

		put16 ( out + 0, 0x8086 );
		put16 ( out + 2, 0x8086 );
		put_text ( out + 4, 20, "CB0002" );
		put_text ( out + 24, 40, "Coldboot NVMe Disk" );
		put_text ( out + 64, 8, "1.0" );
		out [ 72 ] = 6;
		out [ 77 ] = 5;
		put16 ( out + 78, 1 );
		put32 ( out + 80, NVME_VERSION );
		out [ 111 ] = 1;
		out [ 512 ] = 0x66;
		out [ 513 ] = 0x44;
		put16 ( out + 514, queue_entries );
		put32 ( out + 516, 1 );
		out [ 525 ] = 1;

	}

	void nvme::describe_namespace ( uint8_t* out ) const {

		uint64_t blocks = disk && disk->present ( ) ? disk->size / sector_size : 0;

		put64 ( out + 0, blocks );
		put64 ( out + 8, blocks );
		put64 ( out + 16, blocks );
		out [ 25 ] = 0;
		out [ 26 ] = 0;
		out [ 130 ] = 9;

	}

	void nvme::post ( uint32_t queue, uint16_t identifier, const result& done ) {

		submission& work = submissions [ queue ];
		completion& target = completions [ work.completion ];

		if ( !target.used )
			return;

		uint8_t entry [ COMPLETION_SIZE ] = { };

		put32 ( entry, done.value );
		put32 ( entry + 8, work.head | queue << 16 );
		put32 ( entry + 12, identifier | ( target.phase ? 1u << 16 : 0 ) | static_cast < uint32_t > ( done.status ) << 17 );
		vm.write ( target.base + static_cast < uint64_t > ( target.tail ) * COMPLETION_SIZE, entry, COMPLETION_SIZE );
		target.tail = ( target.tail + 1 ) % target.size;

		if ( !target.tail )
			target.phase = !target.phase;

		if ( msi_enabled ( ) && target.interrupts )
			line = false;

		update_line ( );

	}

	void nvme::update_line ( ) {

		bool wanted = false;

		if ( ( control & CONFIG_ENABLE ) && !( masked & 1 ) ) {

			for ( uint32_t i = 0; i < queue_count && !wanted; i++ ) {

				const completion& done = completions [ i ];

				wanted = done.used && done.interrupts && done.head != done.tail;

			}

		}

		signal ( bus, vm, irq, wanted, line );

	}

}