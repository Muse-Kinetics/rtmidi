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
//  JACK             32720 bytes per message, in both directions, and RtMidi's
//                   own output ringbuffer is smaller still unless
//                   JACK_RINGBUFFER_SIZE_OVERRIDE is raised.  JACK's MIDI
//                   port buffer is a fixed size regardless of the period, so
//                   a larger period does not help.  Receiving is the case
//                   with no workaround: a device that sends one large SysEx
//                   decides how it sends, and a 66 kB firmware dump measured
//                   here never arrived, with no error reported.  Use ALSA for
//                   large inbound SysEx on Linux.
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
//  ALSA, inbound    No limit: the kernel chunks a device's SysEx at 256 bytes
//                   before it reaches the sequencer, so a 66 kB firmware dump
//                   from real hardware arrives intact.  Measured.
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
#include <string>
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
  std::cout << "       sysexchunked --listen [seconds]\n\n";
  std::cout << "    N         length of the SysEx message to send.  Try a size a\n";
  std::cout << "              single sendMessage() cannot carry, such as 66312,\n";
  std::cout << "              the size of a real firmware dump.\n";
  std::cout << "    --listen  receive only, and report what arrives.  Useful for\n";
  std::cout << "              checking what a device actually sends, and whether\n";
  std::cout << "              a large message survives the transport.\n\n";
  exit( 0 );
}

static std::vector<unsigned char> received;
static bool complete = false;
static unsigned int callbacks = 0;

void mycallback( double /*deltatime*/, std::vector<unsigned char> *message, void * /*userData*/ )
{
  received.insert( received.end(), message->begin(), message->end() );
  callbacks++;
  if ( !received.empty() && received.back() == 0xF7 ) complete = true;
}

int main( int argc, char *argv[] )
{
  if ( argc < 2 ) usage();

  bool listen = ( std::string( argv[1] ) == "--listen" );
  size_t nBytes = 0;
  int seconds = 30;

  if ( listen ) {
    if ( argc > 2 ) seconds = atoi( argv[2] );
  } else {
    if ( argc != 2 ) usage();
    nBytes = (size_t) atoi( argv[1] );
    if ( nBytes < 3 ) usage();
  }

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
      if ( listen )
        std::cout << "Listening on \"" << midiin->getPortName( 0 ) << "\".\n";
      else
        std::cout << "Opening \"" << midiout->getPortName( 0 ) << "\" for output and\n"
                  << "        \"" << midiin->getPortName( 0 ) << "\" for input.\n"
                  << "Connect them externally for the round trip to complete.\n";
      midiout->openPort( 0 );
      midiin->openPort( 0 );
    }
    else {
      midiout->openVirtualPort( "sysexchunked out" );
      midiin->openVirtualPort( "sysexchunked in" );
      if ( listen )
        std::cout << "Opened virtual port \"sysexchunked in\".\n"
                  << "Connect a device or another application to it.\n";
      else
        std::cout << "Opened virtual ports \"sysexchunked out\" and\n"
                  << "\"sysexchunked in\". Connect them now";
    }

    midiin->ignoreTypes( false, true, true );   // do not ignore SysEx

    // Receiving a large SysEx needs room to reassemble it.  The default input
    // buffer is 1 kB, which is ample for ordinary MIDI and far too small for
    // a firmware dump; the message is truncated or lost without it.  Only
    // some backends use this, but setting it is harmless on the others.
    midiin->setBufferSize( listen ? 1048576 : (unsigned int) nBytes + 1024, 4 );

    midiin->setCallback( &mycallback );

    if ( listen ) {
      std::cout << "Listening for " << seconds << " seconds...\n";
      for ( int i = 0; i < seconds * 10 && !complete; i++ ) SLEEP( 100 );

      if ( received.empty() ) {
        std::cout << "Nothing received.\n";
      } else {
        std::cout << "Received " << received.size() << " bytes in "
                  << callbacks << " callback(s).\n";
        // A message split by the transport arrives in several callbacks and is
        // reassembled by RtMidi; one callback means it came through whole.
        std::cout << "First bytes:";
        for ( size_t i = 0; i < received.size() && i < 8; i++ )
          std::cout << " " << std::hex << (int) received[i] << std::dec;
        std::cout << ( complete ? "  (ends with F7)\n" : "  (no F7 seen)\n" );
      }
    }
    else {
      // Give the user a moment to patch the two ports together before sending.
      for ( int i = 0; i < 10; i++ ) { std::cout << "." << std::flush; SLEEP( 500 ); }
      std::cout << "\n";

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
