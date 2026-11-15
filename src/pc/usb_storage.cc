#include "pc/usb_storage.hh"
#include "runtime/runtime.hh"

namespace pc {

	enum : uint32_t {

		CBW_SIGNATURE = 0x43425355,
		CSW_SIGNATURE = 0x53425355,
		CBW_SIZE = 31,
		CSW_SIZE = 13,

		SCSI_TEST_UNIT_READY = 0x00,
		SCSI_REQUEST_SENSE = 0x03,
		SCSI_INQUIRY = 0x12,
		SCSI_MODE_SENSE_6 = 0x1A,
		SCSI_START_STOP = 0x1B,
		SCSI_PREVENT_ALLOW = 0x1E,
		SCSI_READ_CAPACITY_10 = 0x25,
		SCSI_READ_10 = 0x28,
		SCSI_WRITE_10 = 0x2A,
		SCSI_VERIFY_10 = 0x2F,
		SCSI_SYNCHRONIZE = 0x35,
		SCSI_MODE_SENSE_10 = 0x5A,
		SCSI_READ_16 = 0x88,
		SCSI_WRITE_16 = 0x8A,
		SCSI_SERVICE_IN = 0x9E,
		SCSI_READ_CAPACITY_16 = 0x10,

		SENSE_NOT_READY = 0x02,
		SENSE_MEDIUM_ERROR = 0x03,
		SENSE_ILLEGAL_REQUEST = 0x05,
		SENSE_DATA_PROTECT = 0x07,
		CODE_NO_MEDIUM = 0x3A,
		CODE_INVALID_COMMAND = 0x20,
		CODE_OUT_OF_RANGE = 0x21,
		CODE_INVALID_FIELD = 0x24,
		CODE_WRITE_PROTECTED = 0x27,
		CODE_READ_ERROR = 0x11,

		STATUS_GOOD = 0,
		STATUS_FAILED = 1,

	};

	static const uint8_t storage_device [ 18 ] = {

		18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 64,
		0x09, 0x12, 0x03, 0xCB, 0x00, 0x01, 1, 2, 3, 1,

	};

	static const uint8_t storage_configuration [ 32 ] = {

		9, 0x02, 32, 0, 1, 1, 0, 0x80, 50,
		9, 0x04, 0, 0, 2, 0x08, 0x06, 0x50, 0,
		7, 0x05, 0x81, 0x02, 64, 0, 0,
		7, 0x05, 0x02, 0x02, 64, 0, 0,

	};

	static uint32_t get32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	static uint32_t big32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	static void put_big32 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value >> 24 );
		at [ 1 ] = static_cast < uint8_t > ( value >> 16 );
		at [ 2 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 3 ] = static_cast < uint8_t > ( value );

	}

	static void put32 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 2 ] = static_cast < uint8_t > ( value >> 16 );
		at [ 3 ] = static_cast < uint8_t > ( value >> 24 );

	}

	static void copy_text ( uint8_t* at, const char* text, int32_t room ) {

		bool ended = false;

		for ( int32_t i = 0; i < room; i++ ) {

			if ( !ended && !text [ i ] )
				ended = true;

			at [ i ] = static_cast < uint8_t > ( ended ? ' ' : text [ i ] );

		}

	}

	usb_storage::usb_storage ( ) {

		data = static_cast < uint8_t* > ( runtime_allocate_zero ( buffer_size ) );

	}

	usb_storage::~usb_storage ( ) {

		runtime_release ( data );

	}

	bool usb_storage::present ( ) const {

		return disk && disk->present ( );

	}

	int32_t usb_storage::descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) {

		if ( type == 0x01 )
			return give ( storage_device, sizeof ( storage_device ), reply, room );

		if ( type == 0x02 )
			return give ( storage_configuration, sizeof ( storage_configuration ), reply, room );

		if ( type == 0x03 ) {

			static const uint8_t languages [ 4 ] = { 4, 0x03, 0x09, 0x04 };

			if ( index == 0 )
				return give ( languages, 4, reply, room );

			if ( index == 1 )
				return text ( "Coldboot", reply, room );

			if ( index == 2 )
				return text ( "Coldboot USB Disk", reply, room );

			if ( index == 3 )
				return text ( "CB0003000001", reply, room );

		}

		return usb_stall;

	}

	int32_t usb_storage::request ( const uint8_t* setup, uint8_t* reply, int32_t room ) {

		uint8_t type = setup [ 0 ];
		uint8_t code = setup [ 1 ];

		if ( type == 0xA1 && code == 0xFE && room >= 1 ) {

			reply [ 0 ] = 0;

			return 1;

		}

		if ( type == 0x21 && code == 0xFF ) {

			stage = WAIT_COMMAND;

			return 0;

		}

		return usb_stall;

	}

	void usb_storage::fail ( uint8_t key, uint8_t code ) {

		sense_key = key;
		sense_code = code;
		result = STATUS_FAILED;
		length = 0;
		cursor = 0;

		if ( !expected )
			stage = SEND_STATUS;
		else
			stage = inbound ? HALTED_IN : HALTED_OUT;

	}

	void usb_storage::reply ( uint32_t size ) {

		length = size < expected ? size : expected;
		cursor = 0;
		result = STATUS_GOOD;
		sense_key = 0;
		sense_code = 0;
		stage = length ? DATA_IN : SEND_STATUS;

	}

	void usb_storage::command ( const uint8_t* block ) {

		const uint8_t* cdb = block + 15;
		uint64_t blocks = present ( ) ? disk->size / block_size : 0;

		for ( int32_t i = 0; i < 4; i++ )
			tag [ i ] = block [ 4 + i ];

		expected = get32 ( block + 8 );
		inbound = block [ 12 ] & 0x80;
		commands_run++;

		switch ( cdb [ 0 ] ) {

			case SCSI_TEST_UNIT_READY:
				if ( !present ( ) )
					fail ( SENSE_NOT_READY, CODE_NO_MEDIUM );
				else
					reply ( 0 );

				break;

			case SCSI_REQUEST_SENSE: {

				uint8_t key = sense_key;
				uint8_t code = sense_code;

				for ( uint32_t i = 0; i < 18; i++ )
					data [ i ] = 0;

				data [ 0 ] = 0x70;
				data [ 2 ] = key;
				data [ 7 ] = 10;
				data [ 12 ] = code;
				reply ( 18 );

				break;

			}

			case SCSI_INQUIRY:
				for ( uint32_t i = 0; i < 36; i++ )
					data [ i ] = 0;

				data [ 1 ] = 0x80;
				data [ 2 ] = 0x04;
				data [ 3 ] = 0x02;
				data [ 4 ] = 31;
				copy_text ( data + 8, "Coldboot", 8 );
				copy_text ( data + 16, "USB Disk", 16 );
				copy_text ( data + 32, "1.0", 4 );
				reply ( 36 );
				break;

			case SCSI_READ_CAPACITY_10:
				if ( !blocks ) {

					fail ( SENSE_NOT_READY, CODE_NO_MEDIUM );

					break;

				}

				put_big32 ( data, static_cast < uint32_t > ( blocks - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFull : blocks - 1 ) );
				put_big32 ( data + 4, block_size );
				reply ( 8 );
				break;

			case SCSI_SERVICE_IN:
				if ( ( cdb [ 1 ] & 0x1F ) != SCSI_READ_CAPACITY_16 || !blocks ) {

					fail ( SENSE_ILLEGAL_REQUEST, CODE_INVALID_COMMAND );

					break;

				}

				for ( uint32_t i = 0; i < 32; i++ )
					data [ i ] = 0;

				put_big32 ( data, static_cast < uint32_t > ( ( blocks - 1 ) >> 32 ) );
				put_big32 ( data + 4, static_cast < uint32_t > ( blocks - 1 ) );
				put_big32 ( data + 8, block_size );
				reply ( 32 );
				break;

			case SCSI_MODE_SENSE_6:
				data [ 0 ] = 3;
				data [ 1 ] = 0;
				data [ 2 ] = static_cast < uint8_t > ( present ( ) && !disk->writable ? 0x80 : 0 );
				data [ 3 ] = 0;
				reply ( 4 );
				break;

			case SCSI_MODE_SENSE_10:
				for ( uint32_t i = 0; i < 8; i++ )
					data [ i ] = 0;

				data [ 1 ] = 6;
				data [ 3 ] = static_cast < uint8_t > ( present ( ) && !disk->writable ? 0x80 : 0 );
				reply ( 8 );
				break;

			case SCSI_PREVENT_ALLOW:
			case SCSI_START_STOP:
			case SCSI_VERIFY_10:
				reply ( 0 );
				break;

			case SCSI_SYNCHRONIZE:
				if ( present ( ) )
					disk->flush ( );

				reply ( 0 );
				break;

			case SCSI_READ_10:
			case SCSI_READ_16:
			case SCSI_WRITE_10:
			case SCSI_WRITE_16: {

				bool wide = cdb [ 0 ] == SCSI_READ_16 || cdb [ 0 ] == SCSI_WRITE_16;
				bool writing = cdb [ 0 ] == SCSI_WRITE_10 || cdb [ 0 ] == SCSI_WRITE_16;
				uint64_t first = wide ? static_cast < uint64_t > ( big32 ( cdb + 2 ) ) << 32 | big32 ( cdb + 6 ) : big32 ( cdb + 2 );
				uint32_t count = wide ? big32 ( cdb + 10 ) : static_cast < uint32_t > ( cdb [ 7 ] << 8 | cdb [ 8 ] );
				uint64_t bytes = static_cast < uint64_t > ( count ) * block_size;

				if ( !present ( ) )
					fail ( SENSE_NOT_READY, CODE_NO_MEDIUM );
				else if ( first + count > blocks )
					fail ( SENSE_ILLEGAL_REQUEST, CODE_OUT_OF_RANGE );
				else if ( bytes > buffer_size || bytes != expected || writing == inbound )
					fail ( SENSE_ILLEGAL_REQUEST, CODE_INVALID_FIELD );
				else if ( writing && !disk->writable )
					fail ( SENSE_DATA_PROTECT, CODE_WRITE_PROTECTED );
				else if ( writing ) {

					write_block = first;
					length = static_cast < uint32_t > ( bytes );
					cursor = 0;
					result = STATUS_GOOD;
					stage = length ? DATA_OUT : SEND_STATUS;

				} else if ( count && !disk->read ( first * block_size, data, static_cast < uint32_t > ( bytes ) ) )
					fail ( SENSE_MEDIUM_ERROR, CODE_READ_ERROR );
				else {

					blocks_moved += count;
					reply ( static_cast < uint32_t > ( bytes ) );

				}

				break;

			}

			default:
				fail ( SENSE_ILLEGAL_REQUEST, CODE_INVALID_COMMAND );
				break;

		}

	}

	int32_t usb_storage::poll ( uint8_t endpoint, uint8_t* out, int32_t room ) {

		if ( endpoint != 1 )
			return usb_stall;

		if ( stage == HALTED_IN )
			return usb_stall;

		if ( stage == DATA_IN ) {

			int32_t piece = static_cast < int32_t > ( length - cursor );

			piece = piece < room ? piece : room;
			piece = piece < packet ? piece : packet;

			for ( int32_t i = 0; i < piece; i++ )
				out [ i ] = data [ cursor + i ];

			cursor += static_cast < uint32_t > ( piece );

			if ( cursor >= length )
				stage = SEND_STATUS;

			return piece;

		}

		if ( stage == SEND_STATUS && room >= static_cast < int32_t > ( CSW_SIZE ) ) {

			uint32_t moved = stage == SEND_STATUS ? ( inbound ? cursor : ( result == STATUS_GOOD ? length : 0 ) ) : 0;

			put32 ( status, CSW_SIGNATURE );

			for ( int32_t i = 0; i < 4; i++ )
				status [ 4 + i ] = tag [ i ];

			put32 ( status + 8, expected > moved ? expected - moved : 0 );
			status [ 12 ] = result;

			for ( uint32_t i = 0; i < CSW_SIZE; i++ )
				out [ i ] = status [ i ];

			stage = WAIT_COMMAND;

			return CSW_SIZE;

		}

		return usb_nak;

	}

	int32_t usb_storage::receive ( uint8_t endpoint, const uint8_t* in, int32_t size ) {

		if ( endpoint != 2 )
			return usb_stall;

		if ( stage == HALTED_OUT )
			return usb_stall;

		if ( stage == WAIT_COMMAND ) {

			if ( size != static_cast < int32_t > ( CBW_SIZE ) || get32 ( in ) != CBW_SIGNATURE )
				return usb_stall;

			command ( in );

			return size;

		}

		if ( stage == DATA_OUT ) {

			int32_t piece = static_cast < int32_t > ( length - cursor );

			piece = piece < size ? piece : size;

			for ( int32_t i = 0; i < piece; i++ )
				data [ cursor + i ] = in [ i ];

			cursor += static_cast < uint32_t > ( piece );

			if ( cursor >= length ) {

				if ( disk->write ( write_block * block_size, data, length ) )
					blocks_moved += length / block_size;
				else {

					result = STATUS_FAILED;
					sense_key = SENSE_MEDIUM_ERROR;
					sense_code = CODE_READ_ERROR;

				}

				stage = SEND_STATUS;

			}

			return size;

		}

		return usb_nak;

	}

	void usb_storage::clear_halt ( uint8_t endpoint ) {

		if ( ( endpoint == 1 && stage == HALTED_IN ) || ( endpoint == 2 && stage == HALTED_OUT ) ) {

			cursor = 0;
			length = 0;
			stage = SEND_STATUS;

		}

	}

	void usb_storage::restart ( ) {

		stage = WAIT_COMMAND;
		sense_key = 0;
		sense_code = 0;

	}

}