//*****************************************//
//  sysexchunked.cpp
//  by Eric Bateman, 2026.
//
//  Send a SysEx message of any size by splitting it across several
//  sendMessage() calls, and receive it reassembled.
//
//  Why this is needed
//  ------------------
//  A single sendMessage() can be bounded by the transport underneath RtMidi,
//  and the bound differs by backend.  Splitting a large SysEx across calls
//  avoids every one of these: only the first piece carries F0 and only the
//  last carries F7, the pieces between are ordinary data bytes, and the
//  receiver concatenates them until F7 arrives.  The application on the other
//  end sees one message however it was divided.
//
//  This is upstream issue #214, open since 2020.  The limits are in the
//  transports, not in RtMidi, and no RtMidi change removes them.
//
//  Per-backend notes
//  -----------------
//  ALSA sequencer   About 5.5 kB per message.  The kernel splits a variable
//                   length event into 28-byte cells from the receiving
//                   client's pool and refuses the message outright when it
//                   needs more cells than the pool holds.  The whole message
//                   is lost.  RtMidi reports this when sending to another
//                   application's port; when a kernel client such as
//                   "Midi Through" is in the path the send succeeds and the
//                   loss happens later, with nothing observable at either end.
//
//  JACK             32720 bytes per message, and RtMidi's own output
//                   ringbuffer is smaller still unless
//                   JACK_RINGBUFFER_SIZE_OVERRIDE is raised.  JACK's MIDI
//                   port buffer is a fixed size regardless of the period, so
//                   a larger period does not help.
//
//  CoreMIDI         No caller-visible limit: sendMessage() already breaks a
//                   large SysEx into 64 kB MIDIPacketLists internally.
//                   Chunking here is harmless.
//
//  Windows MM       No documented size limit, but a SysEx send is
//                   synchronous: sendMessage() does not return until the
//                   driver has finished with the buffer.  Sending in pieces
//                   keeps each call short, which matters on a UI thread.
//
//  Other backends   Windows MIDI Services, Web MIDI and Android AMidi have
//                   not been measured.  If you find a limit on one of them,
//                   the note belongs here.
//
//  Receiving takes two steps: ignoreTypes( false, ... ) so SysEx is not
//  filtered out, and setBufferSize() large enough to reassemble the message.
//  The default input buffer is 1 kB, so a firmware-sized dump is lost without
//  it.
//
//*****************************************//

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "RtMidi.h"

#if defined(_WIN32)
  #include <windows.h>
  #define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds )
#else // Unix variants
  #include <unistd.h>
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

// Small enough for every backend, large enough to be efficient.  This is a
// tuning knob rather than a protocol boundary: only the first piece carries
// F0 and only the last carries F7, and the pieces between are ordinary data
// bytes.
static const size_t kSysExSpan = 512;

void sendLargeSysEx( RtMidiOut &midiout, const std::vector<unsigned char> &message )
{
  for ( size_t offset = 0; offset < message.size(); offset += kSysExSpan ) {
    size_t count = std::min( kSysExSpan, message.size() - offset );
    midiout.sendMessage( &message[offset], count );

    // Pace the pieces.  Sending a whole dump as fast as the loop can run
    // overruns a backend that batches per processing period -- JACK collects
    // everything sent during one period into a single port buffer of about
    // 32 kB, and refuses the rest.  A short pause spreads the pieces across
    // periods.  It also keeps a slow hardware link from being flooded.
    SLEEP( 2 );
  }
}

void usage( void )
{
  std::cout << "\nusage: sysexchunked N\n";
  std::cout << "    where N = length of the SysEx message to send.\n";
  std::cout << "    Try a size that a single sendMessage() cannot carry,\n";
  std::cout << "    such as 66312, the size of a real firmware dump.\n\n";
  exit( 0 );
}

static std::vector<unsigned char> received;
static bool complete = false;

void mycallback( double /*deltatime*/, std::vector<unsigned char> *message, void * /*userData*/ )
{
  received.insert( received.end(), message->begin(), message->end() );
  if ( !received.empty() && received.back() == 0xF7 ) complete = true;
}

int main( int argc, char *argv[] )
{
  if ( argc != 2 ) usage();
  size_t nBytes = (size_t) atoi( argv[1] );
  if ( nBytes < 3 ) usage();

  RtMidiOut *midiout = 0;
  RtMidiIn *midiin = 0;

  try {
    midiout = new RtMidiOut();
    midiin = new RtMidiIn();

    // Open a virtual port where the API supports one, so the example needs no
    // hardware and no loopback driver.  Windows has no virtual ports, so fall
    // back to the first real port there.
    if ( midiin->getCurrentApi() == RtMidi::WINDOWS_MM ||
         midiin->getCurrentApi() == RtMidi::WINDOWS_UWP ) {
      if ( midiin->getPortCount() == 0 || midiout->getPortCount() == 0 ) {
        std::cout << "No MIDI ports available.\n";
        goto cleanup;
      }
      std::cout << "Opening \"" << midiout->getPortName( 0 ) << "\" for output and\n"
                << "        \"" << midiin->getPortName( 0 ) << "\" for input.\n"
                << "Connect them externally for the round trip to complete.\n";
      midiout->openPort( 0 );
      midiin->openPort( 0 );
    }
    else {
      midiout->openVirtualPort( "sysexchunked out" );
      midiin->openVirtualPort( "sysexchunked in" );
      std::cout << "Opened virtual ports \"sysexchunked out\" and\n"
                << "\"sysexchunked in\". Connect them now";
    }

    midiin->ignoreTypes( false, true, true );   // do not ignore SysEx

    // Receiving a large SysEx needs room to reassemble it.  The default input
    // buffer is 1 kB, which is ample for ordinary MIDI and far too small for
    // a firmware dump; the message is truncated or lost without it.  Only
    // some backends use this, but setting it is harmless on the others.
    midiin->setBufferSize( (unsigned int) nBytes + 1024, 4 );

    midiin->setCallback( &mycallback );

    // Give the user a moment to patch the two ports together before sending.
    for ( int i = 0; i < 10; i++ ) { std::cout << "." << std::flush; SLEEP( 500 ); }
    std::cout << "\n";

    {
      // F0 7D <data...> F7.  0x7D is the non-commercial manufacturer id.
      std::vector<unsigned char> message;
      message.push_back( 0xF0 );
      message.push_back( 0x7D );
      while ( message.size() < nBytes - 1 )
        message.push_back( (unsigned char) ( message.size() & 0x7F ) );
      message.push_back( 0xF7 );

      std::cout << "Sending " << message.size() << " bytes in "
                << kSysExSpan << "-byte pieces...\n";
      sendLargeSysEx( *midiout, message );

      // Allow time for the message to arrive; a slow link needs longer.
      for ( int i = 0; i < 2000 && !complete; i++ ) SLEEP( 5 );

      if ( received.empty() )
        std::cout << "Nothing received. Are the two ports connected?\n";
      else
        std::cout << "Received " << received.size() << " bytes: "
                  << ( received == message ? "identical." : "MISMATCH." ) << "\n";
    }
  }
  catch ( RtMidiError &error ) {
    error.printMessage();
  }

 cleanup:
  delete midiout;
  delete midiin;
  return 0;
}
