#include "pc/usb_hub.hh"

namespace pc {

	enum : uint16_t {

		PORT_CONNECTION = 0x0001,
		PORT_ENABLE = 0x0002,
		PORT_SUSPEND = 0x0004,
		PORT_RESET = 0x0010,
		PORT_POWER = 0x0100,

		FEATURE_ENABLE = 1,
		FEATURE_SUSPEND = 2,
		FEATURE_RESET = 4,
		FEATURE_POWER = 8,
		FEATURE_CHANGE_CONNECTION = 16,
		FEATURE_CHANGE_ENABLE = 17,
		FEATURE_CHANGE_SUSPEND = 18,
		FEATURE_CHANGE_OVER_CURRENT = 19,
		FEATURE_CHANGE_RESET = 20,

	};

	static const uint8_t hub_device [ 18 ] = {

		18, 0x01, 0x10, 0x01, 0x09, 0x00, 0x00, 64,
		0x09, 0x12, 0x02, 0xCB, 0x00, 0x01, 1, 2, 0, 1,

	};

	static const uint8_t hub_configuration [ 25 ] = {

		9, 0x02, 25, 0, 1, 1, 0, 0xE0, 0,
		9, 0x04, 0, 0, 1, 0x09, 0x00, 0x00, 0,
		7, 0x05, 0x81, 0x03, 1, 0, 255,

	};

	void usb_hub::plug ( int32_t port, usb_device* device ) {

		if ( port < 0 || port >= port_count )
			return;

		children [ port ] = device;

		if ( status [ port ] & PORT_POWER ) {

			status [ port ] |= PORT_CONNECTION;
			change [ port ] |= PORT_CONNECTION;

		}

	}

	usb_device* usb_hub::route ( uint8_t target ) {

		if ( target == address )
			return this;

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( !children [ i ] || !( status [ i ] & PORT_ENABLE ) || ( status [ i ] & ( PORT_RESET | PORT_SUSPEND ) ) )
				continue;

			usb_device* found = children [ i ]->route ( target );

			if ( found )
				return found;

		}

		return nullptr;

	}

	int32_t usb_hub::descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) {

		if ( type == 0x01 )
			return give ( hub_device, sizeof ( hub_device ), reply, room );

		if ( type == 0x02 )
			return give ( hub_configuration, sizeof ( hub_configuration ), reply, room );

		if ( type == 0x03 ) {

			static const uint8_t languages [ 4 ] = { 4, 0x03, 0x09, 0x04 };

			if ( index == 0 )
				return give ( languages, 4, reply, room );

			if ( index == 1 )
				return text ( "Coldboot", reply, room );

			if ( index == 2 )
				return text ( "Coldboot USB Hub", reply, room );

		}

		return usb_stall;

	}

	void usb_hub::set_feature ( int32_t port, uint16_t feature ) {

		switch ( feature ) {

			case FEATURE_POWER:
				status [ port ] |= PORT_POWER;

				if ( children [ port ] && !( status [ port ] & PORT_CONNECTION ) ) {

					status [ port ] |= PORT_CONNECTION;
					change [ port ] |= PORT_CONNECTION;

				}

				break;

			case FEATURE_RESET:
				if ( !( status [ port ] & PORT_CONNECTION ) )
					break;

				children [ port ]->reset ( );
				status [ port ] = static_cast < uint16_t > ( ( status [ port ] | PORT_ENABLE ) & ~( PORT_RESET | PORT_SUSPEND ) );
				change [ port ] |= PORT_RESET;
				break;

			case FEATURE_SUSPEND:
				status [ port ] |= PORT_SUSPEND;
				break;

			default:
				break;

		}

	}

	void usb_hub::clear_feature ( int32_t port, uint16_t feature ) {

		switch ( feature ) {

			case FEATURE_ENABLE:
				status [ port ] &= static_cast < uint16_t > ( ~PORT_ENABLE );
				break;

			case FEATURE_SUSPEND:
				status [ port ] &= static_cast < uint16_t > ( ~PORT_SUSPEND );
				break;

			case FEATURE_POWER:
				status [ port ] = 0;
				break;

			case FEATURE_CHANGE_CONNECTION:
				change [ port ] &= static_cast < uint16_t > ( ~0x0001 );
				break;

			case FEATURE_CHANGE_ENABLE:
				change [ port ] &= static_cast < uint16_t > ( ~0x0002 );
				break;

			case FEATURE_CHANGE_SUSPEND:
				change [ port ] &= static_cast < uint16_t > ( ~0x0004 );
				break;

			case FEATURE_CHANGE_OVER_CURRENT:
				change [ port ] &= static_cast < uint16_t > ( ~0x0008 );
				break;

			case FEATURE_CHANGE_RESET:
				change [ port ] &= static_cast < uint16_t > ( ~0x0010 );
				break;

			default:
				break;

		}

	}

	int32_t usb_hub::request ( const uint8_t* setup, uint8_t* reply, int32_t room ) {

		uint8_t type = setup [ 0 ];
		uint8_t code = setup [ 1 ];
		uint16_t value = static_cast < uint16_t > ( setup [ 2 ] | setup [ 3 ] << 8 );
		int32_t port = ( setup [ 4 ] | setup [ 5 ] << 8 ) - 1;

		if ( type == 0xA0 && code == 0x06 && value >> 8 == 0x29 ) {

			uint8_t hub [ 9 ] = { 9, 0x29, port_count, 0x09, 0x00, 10, 100, 0x00, 0xFF };

			return give ( hub, sizeof ( hub ), reply, room );

		}

		if ( type == 0xA0 && code == 0x00 ) {

			uint8_t state [ 4 ] = { };

			return give ( state, 4, reply, room );

		}

		if ( type == 0x20 && ( code == 0x01 || code == 0x03 ) )
			return 0;

		if ( port < 0 || port >= port_count )
			return usb_stall;

		if ( type == 0xA3 && code == 0x00 ) {

			uint8_t state [ 4 ] = { static_cast < uint8_t > ( status [ port ] ), static_cast < uint8_t > ( status [ port ] >> 8 ), static_cast < uint8_t > ( change [ port ] ), static_cast < uint8_t > ( change [ port ] >> 8 ) };

			return give ( state, 4, reply, room );

		}

		if ( type == 0x23 && code == 0x03 ) {

			set_feature ( port, value );

			return 0;

		}

		if ( type == 0x23 && code == 0x01 ) {

			clear_feature ( port, value );

			return 0;

		}

		return usb_stall;

	}

	int32_t usb_hub::poll ( uint8_t endpoint, uint8_t* data, int32_t room ) {

		if ( endpoint != 1 )
			return usb_stall;

		uint8_t bitmap = 0;

		for ( int32_t i = 0; i < port_count; i++ ) {

			if ( change [ i ] )
				bitmap = static_cast < uint8_t > ( bitmap | 1 << ( i + 1 ) );

		}

		if ( !bitmap || room < 1 )
			return usb_nak;

		data [ 0 ] = bitmap;

		return 1;

	}

	void usb_hub::restart ( ) {

		for ( int32_t i = 0; i < port_count; i++ ) {

			status [ i ] = 0;
			change [ i ] = 0;

		}

	}

}