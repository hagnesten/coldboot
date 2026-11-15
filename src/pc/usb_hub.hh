#pragma once

#include "pc/usb.hh"

namespace pc {

	class usb_hub : public usb_device {

	public:

		static constexpr int32_t port_count = 4;

		void plug ( int32_t port, usb_device* device );

		usb_device* route ( uint8_t target ) override;

	protected:

		int32_t descriptor ( uint8_t type, uint8_t index, uint8_t* reply, int32_t room ) override;

		int32_t request ( const uint8_t* setup, uint8_t* reply, int32_t room ) override;

		int32_t poll ( uint8_t endpoint, uint8_t* data, int32_t room ) override;

		void restart ( ) override;

	private:

		void set_feature ( int32_t port, uint16_t feature );

		void clear_feature ( int32_t port, uint16_t feature );

		usb_device* children [ port_count ] = { };
		uint16_t status [ port_count ] = { };
		uint16_t change [ port_count ] = { };

	};

}