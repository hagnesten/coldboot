#include "pc/sealing.hh"

namespace pc::sealing {

	namespace {

		constexpr uint64_t schedule_constants [ 80 ] = {
			0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull,
			0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull,
			0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull, 0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
			0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
			0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull, 0x983e5152ee66dfabull,
			0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
			0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull,
			0x53380d139d95b3dfull, 0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
			0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
			0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull, 0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
			0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull,
			0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
			0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull,
			0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
			0x113f9804bef90daeull, 0x1b710b35131c471bull, 0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
			0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull
		};

		constexpr uint8_t group_order [ 32 ] = {
			0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
		};

		constexpr int64_t prime_limbs [ 16 ] = { 0xffed, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x7fff };

		inline uint64_t spin ( uint64_t value, uint32_t count ) {

			return ( value >> count ) | ( value << ( 64 - count ) );

		}

		struct element {

			int64_t v [ 16 ] = { };

		};

		struct point {

			element x;
			element y;
			element z;
			element t;

		};

		element constant ( int64_t value ) {

			element out;

			out.v [ 0 ] = value & 0xffff;
			out.v [ 1 ] = value >> 16;

			return out;

		}

		void settle ( element& a ) {

			for ( int32_t pass = 0; pass < 3; pass++ ) {

				for ( int32_t i = 0; i < 16; i++ ) {

					int64_t carry = a.v [ i ] >> 16;

					a.v [ i ] -= carry * 65536;

					if ( i < 15 )
						a.v [ i + 1 ] += carry;
					else
						a.v [ 0 ] += 38 * carry;

				}

			}

		}

		element plus ( const element& a, const element& b ) {

			element out;

			for ( int32_t i = 0; i < 16; i++ )
				out.v [ i ] = a.v [ i ] + b.v [ i ];

			settle ( out );

			return out;

		}

		element minus ( const element& a, const element& b ) {

			element out;

			for ( int32_t i = 0; i < 16; i++ )
				out.v [ i ] = a.v [ i ] - b.v [ i ] + 4 * prime_limbs [ i ];

			settle ( out );

			return out;

		}

		element times ( const element& a, const element& b ) {

			int64_t wide [ 31 ] = { };

			for ( int32_t i = 0; i < 16; i++ )
				for ( int32_t j = 0; j < 16; j++ )
					wide [ i + j ] += a.v [ i ] * b.v [ j ];

			for ( int32_t i = 0; i < 15; i++ )
				wide [ i ] += 38 * wide [ i + 16 ];

			element out;

			for ( int32_t i = 0; i < 16; i++ )
				out.v [ i ] = wide [ i ];

			settle ( out );

			return out;

		}

		element square ( const element& a ) {

			return times ( a, a );

		}

		element inverse ( const element& a ) {

			element c = a;

			for ( int32_t bit = 253; bit >= 0; bit-- ) {

				c = square ( c );

				if ( bit != 2 && bit != 4 )
					c = times ( c, a );

			}

			return c;

		}

		element root_power ( const element& a ) {

			element c = a;

			for ( int32_t bit = 250; bit >= 0; bit-- ) {

				c = square ( c );

				if ( bit != 1 )
					c = times ( c, a );

			}

			return c;

		}

		void freeze ( const element& a, uint8_t* out ) {

			element t = a;

			settle ( t );

			for ( int32_t round = 0; round < 2; round++ ) {

				element m;
				int64_t borrow = 0;

				for ( int32_t i = 0; i < 16; i++ ) {

					m.v [ i ] = t.v [ i ] - prime_limbs [ i ] - borrow;
					borrow = m.v [ i ] < 0 ? 1 : 0;

					if ( borrow )
						m.v [ i ] += 65536;

				}

				if ( !borrow )
					t = m;

			}

			for ( int32_t i = 0; i < 16; i++ ) {

				out [ i * 2 ] = static_cast < uint8_t > ( t.v [ i ] & 0xff );
				out [ i * 2 + 1 ] = static_cast < uint8_t > ( t.v [ i ] >> 8 );

			}

		}

		element thaw ( const uint8_t* bytes ) {

			element out;

			for ( int32_t i = 0; i < 16; i++ )
				out.v [ i ] = bytes [ i * 2 ] | static_cast < int64_t > ( bytes [ i * 2 + 1 ] ) << 8;

			out.v [ 15 ] &= 0x7fff;

			return out;

		}

		bool equal ( const element& a, const element& b ) {

			uint8_t left [ 32 ];
			uint8_t right [ 32 ];

			freeze ( a, left );
			freeze ( b, right );

			for ( int32_t i = 0; i < 32; i++ )
				if ( left [ i ] != right [ i ] )
					return false;

			return true;

		}

		int32_t parity ( const element& a ) {

			uint8_t bytes [ 32 ];

			freeze ( a, bytes );

			return bytes [ 0 ] & 1;

		}

		struct curve {

			element d;
			element d2;
			element unit_root;
			point base;
			bool ready = false;

		};

		curve shape;

		bool decode ( const uint8_t* bytes, bool negated, point& out ) {

			element y = thaw ( bytes );
			uint8_t again [ 32 ];

			freeze ( y, again );

			for ( int32_t i = 0; i < 31; i++ )
				if ( again [ i ] != bytes [ i ] )
					return false;

			if ( again [ 31 ] != ( bytes [ 31 ] & 0x7f ) )
				return false;

			element one = constant ( 1 );
			element yy = square ( y );
			element top = minus ( yy, one );
			element bottom = plus ( times ( shape.d, yy ), one );
			element bottom3 = times ( square ( bottom ), bottom );
			element x = times ( times ( top, bottom3 ), root_power ( times ( top, times ( square ( bottom3 ), bottom ) ) ) );
			element check = times ( bottom, square ( x ) );

			if ( !equal ( check, top ) ) {

				if ( !equal ( check, minus ( constant ( 0 ), top ) ) )
					return false;

				x = times ( x, shape.unit_root );

			}

			int32_t sign = bytes [ 31 ] >> 7;

			if ( equal ( x, constant ( 0 ) ) && sign )
				return false;

			if ( ( parity ( x ) == sign ) == negated )
				x = minus ( constant ( 0 ), x );

			out.x = x;
			out.y = y;
			out.z = one;
			out.t = times ( x, y );

			return true;

		}

		point join ( const point& p, const point& q ) {

			element a = times ( minus ( p.y, p.x ), minus ( q.y, q.x ) );
			element b = times ( plus ( p.y, p.x ), plus ( q.y, q.x ) );
			element c = times ( times ( p.t, q.t ), shape.d2 );
			element d = times ( p.z, plus ( q.z, q.z ) );
			element e = minus ( b, a );
			element f = minus ( d, c );
			element g = plus ( d, c );
			element h = plus ( b, a );
			point out;

			out.x = times ( e, f );
			out.y = times ( h, g );
			out.z = times ( g, f );
			out.t = times ( e, h );

			return out;

		}

		point multiply ( const point& p, const uint8_t* scalar ) {

			point out;

			out.x = constant ( 0 );
			out.y = constant ( 1 );
			out.z = constant ( 1 );
			out.t = constant ( 0 );

			for ( int32_t bit = 255; bit >= 0; bit-- ) {

				out = join ( out, out );

				if ( scalar [ bit / 8 ] >> ( bit % 8 ) & 1 )
					out = join ( out, p );

			}

			return out;

		}

		void encode ( const point& p, uint8_t* out ) {

			element flip = inverse ( p.z );

			freeze ( times ( p.y, flip ), out );
			out [ 31 ] ^= static_cast < uint8_t > ( parity ( times ( p.x, flip ) ) << 7 );

		}

		void ready_curve ( ) {

			if ( shape.ready )
				return;

			shape.d = times ( minus ( constant ( 0 ), constant ( 121665 ) ), inverse ( constant ( 121666 ) ) );
			shape.d2 = plus ( shape.d, shape.d );

			element two = constant ( 2 );
			element root = two;

			for ( int32_t bit = 251; bit >= 0; bit-- ) {

				root = square ( root );

				if ( bit != 2 )
					root = times ( root, two );

			}

			shape.unit_root = root;

			uint8_t base_y [ 32 ];

			freeze ( times ( constant ( 4 ), inverse ( constant ( 5 ) ) ), base_y );
			decode ( base_y, false, shape.base );
			shape.ready = true;

		}

		void reduce_wide ( const uint8_t* wide, uint8_t* out ) {

			int64_t x [ 64 ];

			for ( int32_t i = 0; i < 64; i++ )
				x [ i ] = wide [ i ];

			for ( int32_t i = 63; i >= 32; i-- ) {

				int64_t carry = 0;
				int32_t j = i - 32;

				for ( ; j < i - 12; j++ ) {

					x [ j ] += carry - 16 * x [ i ] * group_order [ j - ( i - 32 ) ];
					carry = ( x [ j ] + 128 ) >> 8;
					x [ j ] -= carry * 256;

				}

				x [ j ] += carry;
				x [ i ] = 0;

			}

			int64_t carry = 0;

			for ( int32_t j = 0; j < 32; j++ ) {

				x [ j ] += carry - ( x [ 31 ] >> 4 ) * group_order [ j ];
				carry = x [ j ] >> 8;
				x [ j ] &= 255;

			}

			for ( int32_t j = 0; j < 32; j++ )
				x [ j ] -= carry * group_order [ j ];

			for ( int32_t i = 0; i < 32; i++ ) {

				x [ i + 1 ] += x [ i ] >> 8;
				out [ i ] = static_cast < uint8_t > ( x [ i ] & 255 );

			}

		}

		bool below_order ( const uint8_t* scalar ) {

			for ( int32_t i = 31; i >= 0; i-- ) {

				if ( scalar [ i ] != group_order [ i ] )
					return scalar [ i ] < group_order [ i ];

			}

			return false;

		}

		uint64_t read64 ( const uint8_t* at ) {

			uint64_t value = 0;

			for ( int32_t i = 7; i >= 0; i-- )
				value = value << 8 | at [ i ];

			return value;

		}

		uint32_t read32 ( const uint8_t* at ) {

			return static_cast < uint32_t > ( at [ 0 ] | at [ 1 ] << 8 | at [ 2 ] << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24 );

		}

		uint8_t hex_value ( char c ) {

			return static_cast < uint8_t > ( c <= '9' ? c - '0' : c - 'a' + 10 );

		}

		void from_hex ( const char* text, uint8_t* out, int32_t count ) {

			for ( int32_t i = 0; i < count; i++ )
				out [ i ] = static_cast < uint8_t > ( hex_value ( text [ i * 2 ] ) << 4 | hex_value ( text [ i * 2 + 1 ] ) );

		}

	}

	sha512::sha512 ( ) {

		h [ 0 ] = 0x6a09e667f3bcc908ull;
		h [ 1 ] = 0xbb67ae8584caa73bull;
		h [ 2 ] = 0x3c6ef372fe94f82bull;
		h [ 3 ] = 0xa54ff53a5f1d36f1ull;
		h [ 4 ] = 0x510e527fade682d1ull;
		h [ 5 ] = 0x9b05688c2b3e6c1full;
		h [ 6 ] = 0x1f83d9abfb41bd6bull;
		h [ 7 ] = 0x5be0cd19137e2179ull;

	}

	void sha512::block ( const uint8_t* data ) {

		uint64_t ring [ 16 ];
		uint64_t s [ 8 ];

		for ( int32_t i = 0; i < 16; i++ ) {

			ring [ i ] = 0;

			for ( int32_t b = 0; b < 8; b++ )
				ring [ i ] = ring [ i ] << 8 | data [ i * 8 + b ];

		}

		for ( int32_t i = 0; i < 8; i++ )
			s [ i ] = h [ i ];

		for ( int32_t round = 0; round < 80; round++ ) {

			if ( round >= 16 ) {

				uint64_t early = ring [ ( round + 1 ) & 15 ];
				uint64_t late = ring [ ( round + 14 ) & 15 ];

				ring [ round & 15 ] += ( spin ( early, 1 ) ^ spin ( early, 8 ) ^ ( early >> 7 ) ) + ring [ ( round + 9 ) & 15 ] + ( spin ( late, 19 ) ^ spin ( late, 61 ) ^ ( late >> 6 ) );

			}

			uint64_t mixed = s [ 7 ] + ( spin ( s [ 4 ], 14 ) ^ spin ( s [ 4 ], 18 ) ^ spin ( s [ 4 ], 41 ) ) + ( s [ 6 ] ^ ( s [ 4 ] & ( s [ 5 ] ^ s [ 6 ] ) ) ) + schedule_constants [ round ] + ring [ round & 15 ];
			uint64_t major = ( spin ( s [ 0 ], 28 ) ^ spin ( s [ 0 ], 34 ) ^ spin ( s [ 0 ], 39 ) ) + ( ( s [ 0 ] & s [ 1 ] ) | ( s [ 2 ] & ( s [ 0 ] | s [ 1 ] ) ) );

			for ( int32_t i = 7; i > 0; i-- )
				s [ i ] = s [ i - 1 ];

			s [ 4 ] += mixed;
			s [ 0 ] = mixed + major;

		}

		for ( int32_t i = 0; i < 8; i++ )
			h [ i ] += s [ i ];

	}

	void sha512::feed ( const uint8_t* data, uint64_t size ) {

		length += size;

		for ( uint64_t i = 0; i < size; i++ ) {

			buffer [ filled++ ] = data [ i ];

			if ( filled == 128 ) {

				block ( buffer );
				filled = 0;

			}

		}

	}

	digest sha512::done ( ) {

		uint64_t bits = length * 8;
		uint8_t tail [ 144 ] = { 0x80 };
		uint64_t pad = ( filled < 112 ? 112 : 240 ) - filled;

		for ( int32_t i = 0; i < 8; i++ )
			tail [ pad + 8 + i ] = static_cast < uint8_t > ( bits >> ( 56 - i * 8 ) );

		feed ( tail, pad + 16 );

		digest out;

		for ( int32_t i = 0; i < 64; i++ )
			out.bytes [ i ] = static_cast < uint8_t > ( h [ i / 8 ] >> ( 56 - ( i % 8 ) * 8 ) );

		return out;

	}

	bool verify ( const uint8_t* key, const uint8_t* first, uint64_t first_size, const uint8_t* second, uint64_t second_size, const uint8_t* signature ) {

		ready_curve ( );

		point negative_key;

		if ( !below_order ( signature + 32 ) || !decode ( key, true, negative_key ) )
			return false;

		sha512 hash;

		hash.feed ( signature, 32 );
		hash.feed ( key, 32 );
		hash.feed ( first, first_size );
		hash.feed ( second, second_size );

		digest wide = hash.done ( );
		uint8_t challenge [ 32 ];
		uint8_t found [ 32 ];

		reduce_wide ( wide.bytes, challenge );
		encode ( join ( multiply ( shape.base, signature + 32 ), multiply ( negative_key, challenge ) ), found );

		for ( int32_t i = 0; i < 32; i++ )
			if ( found [ i ] != signature [ i ] )
				return false;

		return true;

	}

	outcome check ( const uint8_t* file, uint64_t size, const uint8_t* keys, uint64_t keys_size ) {

		if ( size < trailer_size )
			return outcome::unsealed;

		const uint8_t* tail = file + size - trailer_size;

		if ( read64 ( tail ) != magic )
			return outcome::unsealed;

		if ( read32 ( tail + 8 ) != 1 || read32 ( tail + 12 ) || read64 ( tail + 16 ) != size - trailer_size )
			return outcome::broken;

		const uint8_t* key = tail + 24;
		bool known = false;

		for ( uint64_t at = 0; at + key_size <= keys_size && !known; at += key_size ) {

			bool same = true;

			for ( uint32_t i = 0; i < key_size && same; i++ )
				same = keys [ at + i ] == key [ i ];

			known = same;

		}

		if ( !known )
			return outcome::stranger;

		return verify ( key, file, size - trailer_size, tail, signed_head, tail + 56 ) ? outcome::sealed : outcome::broken;

	}

	const char* describe ( outcome found ) {

		switch ( found ) {

			case outcome::sealed:
				return "sealed by an enrolled key";

			case outcome::unsealed:
				return "not sealed";

			case outcome::stranger:
				return "sealed by a key that isn't enrolled";

			default:
				return "carrying a broken seal";

		}

	}

	bool self_test ( ) {

		sha512 hash;
		uint8_t expected [ 64 ];

		hash.feed ( reinterpret_cast < const uint8_t* > ( "abc" ), 3 );
		from_hex ( "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", expected, 64 );

		digest found = hash.done ( );

		for ( int32_t i = 0; i < 64; i++ )
			if ( found.bytes [ i ] != expected [ i ] )
				return false;

		uint8_t key [ 32 ];
		uint8_t signature [ 64 ];

		from_hex ( "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key, 32 );
		from_hex ( "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", signature, 64 );

		if ( !verify ( key, nullptr, 0, nullptr, 0, signature ) )
			return false;

		signature [ 7 ] ^= 1;

		return !verify ( key, nullptr, 0, nullptr, 0, signature );

	}

}