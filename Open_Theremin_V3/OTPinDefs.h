/**
 * \file
 * Pin definitions
 */

#ifndef WavePinDefs_h
#define WavePinDefs_h

//------------------------------------------------------------------------------
// DAC pin definitions

// LDAC may be connected to ground to save a pin
/** Set USE_MCP_DAC_LDAC to 0 if LDAC is grounded. */
#define USE_MCP_DAC_LDAC 1

// use arduino pins 2, 3, 4, 5 for DAC

// pin 2 is DAC chip select

/** Data direction register for DAC chip select. */
#define MCP_DAC_CS_DDR DDRB
#define MCP_DAC2_CS_DDR DDRB
/** Port register for DAC chip select. */
#define MCP_DAC_CS_PORT PORTB
/** Port bit number for DAC chip select. */
#define MCP_DAC_CS_BIT 2
#define MCP_DAC2_CS_BIT 1

// pin 3 is DAC serial clock
/** Data direction register for DAC clock. */
#define MCP_DAC_SCK_DDR DDRB
/** Port register for DAC clock. */
#define MCP_DAC_SCK_PORT PORTB
/** Port bit number for DAC clock. */
#define MCP_DAC_SCK_BIT 5

// pin 4 is DAC serial data in

/** Data direction register for DAC serial in. */
#define MCP_DAC_SDI_DDR  DDRB
/** Port register for DAC clock. */
#define MCP_DAC_SDI_PORT PORTB
/** Port bit number for DAC clock. */
#define MCP_DAC_SDI_BIT  3

// pin 5 is LDAC if used
#if USE_MCP_DAC_LDAC
/** Data direction register for Latch DAC Input. */
#define MCP_DAC_LDAC_DDR  DDRD
/** Port register for Latch DAC Input. */
#define MCP_DAC_LDAC_PORT PORTD
/** Port bit number for Latch DAC Input. */
#define MCP_DAC_LDAC_BIT  7
#endif // USE_MCP_DAC_LDAC

#endif // WavePinDefs_h
