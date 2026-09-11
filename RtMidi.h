/**********************************************************************/
/*! \class RtMidi
    \brief An abstract base class for realtime MIDI input/output.

    This class implements some common functionality for the realtime
    MIDI input/output subclasses RtMidiIn and RtMidiOut.

    RtMidi GitHub site: https://github.com/thestk/rtmidi
    RtMidi WWW site: https://caml.music.mcgill.ca/~gary/rtmidi/

    RtMidi: realtime MIDI i/o C++ classes
    Copyright (c) 2003-2023 Gary P. Scavone

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation files
    (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software,
    and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    Any person wishing to distribute modifications to the Software is
    asked to send the modifications to the original developer so that
    they can be incorporated into the canonical version.  This is,
    however, not a binding provision of this license.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/**********************************************************************/

/*!
  \file RtMidi.h
 */

#ifndef RTMIDI_H
#define RTMIDI_H

#if defined _WIN32 || defined __CYGWIN__
  #if defined(RTMIDI_EXPORT)
    #define RTMIDI_DLL_PUBLIC __declspec(dllexport)
  #else
    #define RTMIDI_DLL_PUBLIC
  #endif
#else
  #if __GNUC__ >= 4
    #define RTMIDI_DLL_PUBLIC __attribute__( (visibility( "default" )) )
  #else
    #define RTMIDI_DLL_PUBLIC
  #endif
#endif

#define RTMIDI_VERSION_MAJOR 6
#define RTMIDI_VERSION_MINOR 0
#define RTMIDI_VERSION_PATCH 0
#define RTMIDI_VERSION_BETA  0

#define RTMIDI_TOSTRING2(n) #n
#define RTMIDI_TOSTRING(n) RTMIDI_TOSTRING2(n)

#if RTMIDI_VERSION_BETA > 0
    #define RTMIDI_VERSION RTMIDI_TOSTRING(RTMIDI_VERSION_MAJOR) \
                        "." RTMIDI_TOSTRING(RTMIDI_VERSION_MINOR) \
                        "." RTMIDI_TOSTRING(RTMIDI_VERSION_PATCH) \
                     "beta" RTMIDI_TOSTRING(RTMIDI_VERSION_BETA)
#else
    #define RTMIDI_VERSION RTMIDI_TOSTRING(RTMIDI_VERSION_MAJOR) \
                        "." RTMIDI_TOSTRING(RTMIDI_VERSION_MINOR) \
                        "." RTMIDI_TOSTRING(RTMIDI_VERSION_PATCH)
#endif

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace rt {
namespace midi {

/************************************************************************/
/*! \class RtMidiError
    \brief Exception handling class for RtMidi.

    The RtMidiError class is quite simple but it does allow errors to be
    "caught" by RtMidiError::Type. See the RtMidi documentation to know
    which methods can throw an RtMidiError.
*/
/************************************************************************/

class RTMIDI_DLL_PUBLIC RtMidiError : public std::exception
{
 public:
  //! Defined RtMidiError types.
  enum Type {
    WARNING,           /*!< A non-critical error. */
    DEBUG_WARNING,     /*!< A non-critical error which might be useful for debugging. */
    UNSPECIFIED,       /*!< The default, unspecified error type. */
    NO_DEVICES_FOUND,  /*!< No devices found on system. */
    INVALID_DEVICE,    /*!< An invalid device ID was specified. */
    MEMORY_ERROR,      /*!< An error occurred during memory allocation. */
    INVALID_PARAMETER, /*!< An invalid parameter was specified to a function. */
    INVALID_USE,       /*!< The function was called incorrectly. */
    DRIVER_ERROR,        /*!< A system driver error occurred. */
    SYSTEM_ERROR,        /*!< A system error occurred. */
    THREAD_ERROR,        /*!< A thread error occurred. */
    DRIVER_NOT_INSTALLED /*!< A required driver or runtime is not installed on this system. */
  };

  //! The constructor.
  RtMidiError( const std::string& message, Type type = RtMidiError::UNSPECIFIED ) throw()
    : message_(message), type_(type) {}

  //! The destructor.
  virtual ~RtMidiError( void ) throw() {}

  //! Prints thrown error message to stderr.
  virtual void printMessage( void ) const throw() { std::cerr << '\n' << message_ << "\n\n"; }

  //! Returns the thrown error message type.
  virtual const Type& getType( void ) const throw() { return type_; }

  //! Returns the thrown error message string.
  virtual const std::string& getMessage( void ) const throw() { return message_; }

  //! Returns the thrown error message as a c-style string.
  virtual const char* what( void ) const throw() { return message_.c_str(); }

 protected:
  std::string message_;
  Type type_;
};

//! RtMidi error callback function prototype.
/*!
    \param type Type of error.
    \param errorText Error description.

    Note that class behaviour is undefined after a critical error (not
    a warning) is reported.
 */
typedef void (*RtMidiErrorCallback)( RtMidiError::Type type, const std::string &errorText, void *userData );

class MidiApi;

class RTMIDI_DLL_PUBLIC RtMidi
{
 public:

     RtMidi(RtMidi&& other) noexcept;
  //! MIDI API specifier arguments.
  enum Api {
    UNSPECIFIED,    /*!< Search for a working compiled API. */
    MACOSX_CORE,    /*!< Macintosh OS-X CoreMIDI API. */
    LINUX_ALSA,     /*!< The Advanced Linux Sound Architecture API. */
    UNIX_JACK,      /*!< The JACK Low-Latency MIDI Server API. */
    WINDOWS_MM,     /*!< The Microsoft Multimedia MIDI API. */
    RTMIDI_DUMMY,   /*!< A compilable but non-functional API. */
    WEB_MIDI_API,   /*!< W3C Web MIDI API. */
    WINDOWS_UWP,    /*!< The Microsoft Universal Windows Platform MIDI API. */
    ANDROID_AMIDI,  /*!< Native Android MIDI API. */
    WINDOWS_MIDI_SERVICES, /*!< Windows MIDI Services (Windows 11 24H2+). */
    NUM_APIS        /*!< Number of values in this enum. */
  };

  //! Result returned by checkApiAvailability().
  struct RtMidiApiAvailability {
    bool available = true;      /*!< True when the API runtime is ready to use. */
    std::string message;        /*!< Human-readable reason if not available. */
    std::string installUrl;     /*!< Installer download URL when a runtime is missing. */
  };

  //! Query whether the runtime required by a compiled API is installed.
  /*!
    Call this before constructing any RtMidiIn / RtMidiOut objects that use the
    specified API.  When available is false the installUrl field contains the
    URL the user must visit to install the missing runtime (for example the
    Windows MIDI Services Desktop App SDK Runtime).  The check is lightweight
    and does not open any ports.
  */
  static RtMidiApiAvailability checkApiAvailability( RtMidi::Api api );

  //! A static function to determine the current RtMidi version.
  static std::string getVersion( void ) throw();

  //! A static function to determine the available compiled MIDI APIs.
  /*!
    The values returned in the std::vector can be compared against
    the enumerated list values.  Note that there can be more than one
    API compiled for certain operating systems.
  */
  static void getCompiledApi( std::vector<RtMidi::Api> &apis ) throw();

  //! Return the name of a specified compiled MIDI API.
  /*!
    This obtains a short lower-case name used for identification purposes.
    This value is guaranteed to remain identical across library versions.
    If the API is unknown, this function will return the empty string.
  */
  static std::string getApiName( RtMidi::Api api );

  //! Return the display name of a specified compiled MIDI API.
  /*!
    This obtains a long name used for display purposes.
    If the API is unknown, this function will return the empty string.
  */
  static std::string getApiDisplayName( RtMidi::Api api );

  //! Return the compiled MIDI API having the given name.
  /*!
    A case insensitive comparison will check the specified name
    against the list of compiled APIs, and return the one which
    matches. On failure, the function returns UNSPECIFIED.
  */
  static RtMidi::Api getCompiledApiByName( const std::string &name );

#if defined(__WINDOWS_MIDI_SERVICES__)
  //! Returns true if the Windows MIDI Services SDK runtime is installed and the service is running.
  /*!
    Uses the WMS Desktop App SDK COM bootstrapper (the same probe that the WMS backend itself
    uses on first initialisation).  Safe to call before any RtMidi instances are created.
    Always returns false when compiled without __WINDOWS_MIDI_SERVICES__.
  */
  static bool isWindowsMidiServicesAvailable();
#endif

  //! Pure virtual openPort() function.
  virtual void openPort( unsigned int portNumber = 0, const std::string &portName = std::string( "RtMidi" ) ) = 0;

  //! Pure virtual openVirtualPort() function.
  virtual void openVirtualPort( const std::string &portName = std::string( "RtMidi" ) ) = 0;

  //! Pure virtual getPortCount() function.
  virtual unsigned int getPortCount() = 0;

  //! Pure virtual getPortName() function.
  virtual std::string getPortName( unsigned int portNumber = 0 ) = 0;

  //! Pure virtual closePort() function.
  virtual void closePort( void ) = 0;

  void setClientName( const std::string &clientName );
  void setPortName( const std::string &portName );

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  virtual bool isPortOpen( void ) const = 0;

  //! Set an error callback function to be invoked when an error has occurred.
  /*!
    The callback function will be called whenever an error has occurred. It is best
    to set the error callback function before opening a port.
  */
  virtual void setErrorCallback( RtMidiErrorCallback errorCallback = NULL, void *userData = 0 ) = 0;

 protected:
  RtMidi();
  virtual ~RtMidi();
  MidiApi *rtapi_;

  /* Make the class non-copyable */
  RtMidi(RtMidi& other) = delete;
  RtMidi& operator=(RtMidi& other) = delete;
};

/**********************************************************************/
/*! \class RtMidiIn
    \brief A realtime MIDI input class.

    This class provides a common, platform-independent API for
    realtime MIDI input.  It allows access to a single MIDI input
    port.  Incoming MIDI messages are either saved to a queue for
    retrieval using the getMessage() function or immediately passed to
    a user-specified callback function.  Create multiple instances of
    this class to connect to more than one MIDI device at the same
    time.  With the OS-X, Linux ALSA, and JACK MIDI APIs, it is also
    possible to open a virtual input port to which other MIDI software
    clients can connect.
*/
/**********************************************************************/

// **************************************************************** //
//
// RtMidiIn and RtMidiOut class declarations.
//
// RtMidiIn / RtMidiOut are "controllers" used to select an available
// MIDI input or output interface.  They present common APIs for the
// user to call but all functionality is implemented by the classes
// MidiInApi, MidiOutApi and their subclasses.  RtMidiIn and RtMidiOut
// each create an instance of a MidiInApi or MidiOutApi subclass based
// on the user's API choice.  If no choice is made, they attempt to
// make a "logical" API selection.
//
// **************************************************************** //

class RTMIDI_DLL_PUBLIC RtMidiIn : public RtMidi
{
 public:
  //! User callback function type definition.
  typedef void (*RtMidiCallback)( double timeStamp, std::vector<unsigned char> *message, void *userData );

  //! Default constructor that allows an optional api, client name and queue size.
  /*!
    An exception will be thrown if a MIDI system initialization
    error occurs.  The queue size defines the maximum number of
    messages that can be held in the MIDI queue (when not using a
    callback function).  If the queue size limit is reached,
    incoming messages will be ignored.

    If no API argument is specified and multiple API support has been
    compiled, the default order of use is ALSA, JACK (Linux) and CORE,
    JACK (OS-X).

    \param api        An optional API id can be specified.
    \param clientName An optional client name can be specified. This
                      will be used to group the ports that are created
                      by the application.
    \param queueSizeLimit An optional size of the MIDI input queue can be specified.
  */
  RtMidiIn( RtMidi::Api api=UNSPECIFIED,
            const std::string& clientName = "RtMidi Input Client",
            unsigned int queueSizeLimit = 100 );

  RtMidiIn(RtMidiIn&& other) noexcept : RtMidi(std::move(other)) { }

  //! If a MIDI connection is still open, it will be closed by the destructor.
  ~RtMidiIn ( void ) throw();

  //! Returns the MIDI API specifier for the current instance of RtMidiIn.
  RtMidi::Api getCurrentApi( void ) throw();

  //! Open a MIDI input connection given by enumeration number.
  /*!
    \param portNumber An optional port number greater than 0 can be specified.
                      Otherwise, the default or first port found is opened.
    \param portName An optional name for the application port that is used to connect to portId can be specified.
  */
  void openPort( unsigned int portNumber = 0, const std::string &portName = std::string( "RtMidi Input" ) );

  //! Create a virtual input port, with optional name, to allow software connections (OS X, JACK and ALSA only).
  /*!
    This function creates a virtual MIDI input port to which other
    software applications can connect.  This type of functionality
    is currently only supported by the Macintosh OS-X, any JACK,
    and Linux ALSA APIs (the function returns an error for the other APIs).

    \param portName An optional name for the application port that is
                    used to connect to portId can be specified.
  */
  void openVirtualPort( const std::string &portName = std::string( "RtMidi Input" ) );

  //! Set a callback function to be invoked for incoming MIDI messages.
  /*!
    The callback function will be called whenever an incoming MIDI
    message is received.  While not absolutely necessary, it is best
    to set the callback function before opening a MIDI port to avoid
    leaving some messages in the queue.

    \param callback A callback function must be given.
    \param userData Optionally, a pointer to additional data can be
                    passed to the callback function whenever it is called.
  */
  void setCallback( RtMidiCallback callback, void *userData = 0 );

  //! Cancel use of the current callback function (if one exists).
  /*!
    Subsequent incoming MIDI messages will be written to the queue
    and can be retrieved with the \e getMessage function.
  */
  void cancelCallback();

  //! Close an open MIDI connection (if one exists).
  void closePort( void );

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  virtual bool isPortOpen() const;

  //! Return the number of available MIDI input ports.
  /*!
    \return This function returns the number of MIDI ports of the selected API.
  */
  unsigned int getPortCount();

  //! Return a string identifier for the specified MIDI input port number.
  /*!
    \return The name of the port with the given Id is returned.
    \retval An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  std::string getPortName( unsigned int portNumber = 0 );

  //! Specify whether certain MIDI message types should be queued or ignored during input.
  /*!
    By default, MIDI timing and active sensing messages are ignored
    during message input because of their relative high data rates.
    MIDI sysex messages are ignored by default as well.  Variable
    values of "true" imply that the respective message type will be
    ignored.
  */
  void ignoreTypes( bool midiSysex = true, bool midiTime = true, bool midiSense = true );

  //! Fill the user-provided vector with the data bytes for the next available MIDI message in the input queue and return the event delta-time in seconds.
  /*!
    This function returns immediately whether a new message is
    available or not.  A valid message is indicated by a non-zero
    vector size.  An exception is thrown if an error occurs during
    message retrieval or an input connection was not previously
    established.
  */
  double getMessage( std::vector<unsigned char> *message );

  //! Set an error callback function to be invoked when an error has occurred.
  /*!
    The callback function will be called whenever an error has occurred. It is best
    to set the error callback function before opening a port.
  */
  virtual void setErrorCallback( RtMidiErrorCallback errorCallback = NULL, void *userData = 0 );

  //! Set maximum expected incoming message size.
  /*!
    For APIs that require manual buffer management, it can be useful to set the buffer
    size and buffer count when expecting to receive large SysEx messages.  Note that
    currently this function has no effect when called after openPort().  The default
    buffer size is 1024 with a count of 4 buffers, which should be sufficient for most
    cases; as mentioned, this does not affect all API backends, since most either support
    dynamically scalable buffers or take care of buffer handling themselves.  It is
    principally intended for users of the Windows MM backend who must support receiving
    especially large messages.
  */
  virtual void setBufferSize( unsigned int size, unsigned int count );

 protected:
  void openMidiApi( RtMidi::Api api, const std::string &clientName, unsigned int queueSizeLimit );
};

/**********************************************************************/
/*! \class RtMidiOut
    \brief A realtime MIDI output class.

    This class provides a common, platform-independent API for MIDI
    output.  It allows one to probe available MIDI output ports, to
    connect to one such port, and to send MIDI bytes immediately over
    the connection.  Create multiple instances of this class to
    connect to more than one MIDI device at the same time.  With the
    OS-X, Linux ALSA and JACK MIDI APIs, it is also possible to open a
    virtual port to which other MIDI software clients can connect.
*/
/**********************************************************************/

class RTMIDI_DLL_PUBLIC RtMidiOut : public RtMidi
{
 public:
  //! Default constructor that allows an optional client name.
  /*!
    An exception will be thrown if a MIDI system initialization error occurs.

    If no API argument is specified and multiple API support has been
    compiled, the default order of use is ALSA, JACK (Linux) and CORE,
    JACK (OS-X).
  */
  RtMidiOut( RtMidi::Api api=UNSPECIFIED,
             const std::string& clientName = "RtMidi Output Client" );

  RtMidiOut(RtMidiOut&& other) noexcept : RtMidi(std::move(other)) { }

  //! The destructor closes any open MIDI connections.
  ~RtMidiOut( void ) throw();

  //! Returns the MIDI API specifier for the current instance of RtMidiOut.
  RtMidi::Api getCurrentApi( void ) throw();

  //! Open a MIDI output connection.
  /*!
      An optional port number greater than 0 can be specified.
      Otherwise, the default or first port found is opened.  An
      exception is thrown if an error occurs while attempting to make
      the port connection.
  */
  void openPort( unsigned int portNumber = 0, const std::string &portName = std::string( "RtMidi Output" ) );

  //! Close an open MIDI connection (if one exists).
  void closePort( void );

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  virtual bool isPortOpen() const;

  //! Create a virtual output port, with optional name, to allow software connections (OS X, JACK and ALSA only).
  /*!
      This function creates a virtual MIDI output port to which other
      software applications can connect.  This type of functionality
      is currently only supported by the Macintosh OS-X, Linux ALSA
      and JACK APIs (the function does nothing with the other APIs).
      An exception is thrown if an error occurs while attempting to
      create the virtual port.
  */
  void openVirtualPort( const std::string &portName = std::string( "RtMidi Output" ) );

  //! Return the number of available MIDI output ports.
  unsigned int getPortCount( void );

  //! Return a string identifier for the specified MIDI port type and number.
  /*!
    \return The name of the port with the given Id is returned.
    \retval An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  std::string getPortName( unsigned int portNumber = 0 );

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.

      On some backends, Windows MM in particular, a SysEx message is sent
      synchronously: this call does not return until the driver has finished
      with the data, which for a large dump over a slow link can take a
      noticeable amount of time. Avoid calling it from a user-interface thread.
  */
  int sendMessage( const std::vector<unsigned char> *message );

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.

      Returns the number of message bytes accepted (sent or buffered), or a
      negative value on error. Callers may ignore the return value; existing
      code that called this as `void` is unaffected.

      On some backends, Windows MM in particular, a SysEx message is sent
      synchronously: this call does not return until the driver has finished
      with the data, which for a large dump over a slow link can take a
      noticeable amount of time. Avoid calling it from a user-interface thread.

      \param message A pointer to the MIDI message as raw bytes
      \param size    Length of the MIDI message in bytes
  */
  int sendMessage( const unsigned char *message, size_t size );

  //! Block until all queued output for this port has actually been sent.
  /*!
      Useful for paced/chunked SysEx: sendMessage a chunk, drain(), then delay
      before the next, so the delay becomes a real inter-message gap on the wire.
      A no-op on backends whose sendMessage already blocks until sent.
  */
  void drain( void );

  //! Set an error callback function to be invoked when an error has occurred.
  /*!
    The callback function will be called whenever an error has occurred. It is best
    to set the error callback function before opening a port.
  */
  virtual void setErrorCallback( RtMidiErrorCallback errorCallback = NULL, void *userData = 0 );

 protected:
  void openMidiApi( RtMidi::Api api, const std::string &clientName );
};


// **************************************************************** //
//
// MidiInApi / MidiOutApi class declarations.
//
// Subclasses of MidiInApi and MidiOutApi contain all API- and
// OS-specific code necessary to fully implement the RtMidi API.
//
// Note that MidiInApi and MidiOutApi are abstract base classes and
// cannot be explicitly instantiated.  RtMidiIn and RtMidiOut will
// create instances of a MidiInApi or MidiOutApi subclass.
//
// **************************************************************** //

class RTMIDI_DLL_PUBLIC MidiApi
{
 public:

  MidiApi();
  virtual ~MidiApi();
  virtual RtMidi::Api getCurrentApi( void ) = 0;
  virtual void openPort( unsigned int portNumber, const std::string &portName ) = 0;
  virtual void openVirtualPort( const std::string &portName ) = 0;
  virtual void closePort( void ) = 0;
  virtual void setClientName( const std::string &clientName ) = 0;
  virtual void setPortName( const std::string &portName ) = 0;

  virtual unsigned int getPortCount( void ) = 0;
  virtual std::string getPortName( unsigned int portNumber ) = 0;

  inline bool isPortOpen() const { return connected_; }
  void setErrorCallback( RtMidiErrorCallback errorCallback, void *userData );

  //! A basic error reporting function for RtMidi classes.
  void error( RtMidiError::Type type, std::string errorString );

protected:
  virtual void initialize( const std::string& clientName ) = 0;

  void *apiData_;
  bool connected_;
  std::string errorString_;
  RtMidiErrorCallback errorCallback_;
  bool firstErrorOccurred_;
  void *errorCallbackUserData_;

};

class RTMIDI_DLL_PUBLIC MidiInApi : public MidiApi
{
 public:

  MidiInApi( unsigned int queueSizeLimit );
  virtual ~MidiInApi( void );
  void setCallback( RtMidiIn::RtMidiCallback callback, void *userData );
  void cancelCallback( void );
  virtual void ignoreTypes( bool midiSysex, bool midiTime, bool midiSense );
  virtual double getMessage( std::vector<unsigned char> *message );
  virtual void setBufferSize( unsigned int size, unsigned int count );

  // A MIDI structure used internally by the class to store incoming
  // messages.  Each message represents one and only one MIDI message.
  struct MidiMessage {
    std::vector<unsigned char> bytes;

    //! Time in seconds elapsed since the previous message
    double timeStamp;

    // Default constructor.
    MidiMessage()
      : bytes(0), timeStamp(0.0) {}
  };

  struct MidiQueue {
    unsigned int front;
    unsigned int back;
    unsigned int ringSize;
    MidiMessage *ring;

    // Default constructor.
    MidiQueue()
      : front(0), back(0), ringSize(0), ring(0) {}
    bool push( const MidiMessage& );
    bool pop( std::vector<unsigned char>*, double* );
    unsigned int size( unsigned int *back=0, unsigned int *front=0 );
  };

  // The RtMidiInData structure is used to pass private class data to
  // the MIDI input handling function or thread.
  struct RtMidiInData {
    MidiQueue queue;
    MidiMessage message;
    unsigned char ignoreFlags;
    bool doInput;
    bool firstMessage;
    void *apiData;
    bool usingCallback;
    RtMidiIn::RtMidiCallback userCallback;
    void *userData;
    bool continueSysex;
    unsigned int bufferSize;
    unsigned int bufferCount;

    // Default constructor.
    RtMidiInData()
      : ignoreFlags(7), doInput(false), firstMessage(true), apiData(0), usingCallback(false),
        userCallback(0), userData(0), continueSysex(false), bufferSize(1024), bufferCount(4) {}
  };

 protected:
  RtMidiInData inputData_;
};

// ---------------------------------------------------------------------------
//! \class Midi1UmpEncoder
//! \brief Stateful MIDI 1.0 byte-stream to UMP word encoder.
//!
//! Converts raw MIDI 1.0 bytes into a flat std::vector<uint32_t> of UMP words
//! suitable for SendMultipleMessagesWordArray (Windows MIDI Services) or any
//! other UMP-capable send API.  State persists across calls so chunked SysEx
//! (F0 in one buffer, F7 in a later one), running status, and mixed buffers
//! (channel messages followed by SysEx start) all work correctly without any
//! application-level coordination.
//!
//! Supported:
//!   - Channel voice (0x80-0xEF)   Type-2 32-bit UMP words
//!   - Running status
//!   - System Common (0xF1-0xF6)   Type-1 32-bit UMP words
//!   - System Realtime (0xF8-0xFF) Type-1, emitted immediately without
//!     interrupting SysEx accumulation or running status
//!   - SysEx (0xF0...0xF7), including multi-buffer chunked sends
//!     Type-3 64-bit UMP word pairs, flushed at each 0xF7
// ---------------------------------------------------------------------------
class RTMIDI_DLL_PUBLIC Midi1UmpEncoder
{
public:
    Midi1UmpEncoder() = default;

    //! Encode bytes from buf[0..len-1], appending UMP words to |out|.
    //! |group| is the UMP group index (0-15).
    void encode(const uint8_t* buf, size_t len, uint8_t group,
                std::vector<uint32_t>& out)
    {
        for (size_t i = 0; i < len; ++i)
        {
            const uint8_t b = buf[i];

            // System Realtime (0xF8-0xFF): single-byte, emit immediately.
            // Does NOT cancel running status or interrupt SysEx accumulation.
            if (b >= 0xF8)
            {
                emit_type1(b, 0, 0, group, out);
                continue;
            }

            // Status byte (0x80-0xF7)
            if (b >= 0x80)
            {
                if (b == 0xF0) // SYX_START
                {
                    // SysEx start: discard any partial channel message.
                    data_count_      = 0;
                    running_status_  = 0;
                    in_sysex_        = true;
                    sysex_started_   = false;
                    sysex_buf_count_ = 0;
                }
                else if (b == 0xF7) // SYX_END
                {
                    // SysEx end: emit end/complete with whatever bytes remain
                    // (0-6). A 0-byte "end" is valid UMP and maps to just F7
                    // on the wire for MIDI 1.0 devices.
                    if (in_sysex_)
                    {
                        const uint8_t sn = sysex_started_ ? 0x3u : 0x0u;
                        emit_sysex_packet(sn, sysex_buf_count_, group, out);
                    }
                    in_sysex_        = false;
                    sysex_started_   = false;
                    sysex_buf_count_ = 0;
                    running_status_  = 0;
                    data_count_      = 0;
                }
                else if (b == 0xF4 || b == 0xF5)
                {
                    // Undefined System Common: ignore, no state change.
                }
                else if (b >= 0xF1 && b <= 0xF6)
                {
                    // System Common: cancels running status and SysEx.
                    in_sysex_        = false;
                    sysex_started_   = false;
                    sysex_buf_count_ = 0;
                    data_count_      = 0;
                    const int dlen  = sys_common_data_len(b);
                    if (dlen == 0)
                    {
                        emit_type1(b, 0, 0, group, out);
                        running_status_ = 0;
                    }
                    else
                    {
                        running_status_ = b;
                        data_needed_    = dlen;
                    }
                }
                else
                {
                    // Channel status (0x80-0xEF): set/update running status.
                    // Cancels in-progress SysEx per MIDI spec.
                    in_sysex_        = false;
                    sysex_started_   = false;
                    sysex_buf_count_ = 0;
                    running_status_  = b;
                    data_needed_     = channel_data_len(b);
                    data_count_      = 0;
                }
                continue;
            }

            // Data byte (0x00-0x7F)
            if (in_sysex_)
            {
                // Flush a full 6-byte group immediately as start or continue.
                if (sysex_buf_count_ == 6)
                {
                    emit_sysex_packet(sysex_started_ ? 0x2u : 0x1u, 6, group, out);
                    sysex_started_   = true;
                    sysex_buf_count_ = 0;
                }
                sysex_buf_[sysex_buf_count_++] = b;
                continue;
            }

            if (running_status_ == 0)
                continue;  // stray data byte - ignore

            data_buf_[data_count_++] = b;
            if (data_count_ >= data_needed_)
            {
                const uint8_t d1 = data_buf_[0];
                const uint8_t d2 = (data_needed_ >= 2) ? data_buf_[1] : 0;
                if (running_status_ < 0xF0)
                    emit_type2(running_status_, d1, d2, group, out);
                else
                    emit_type1(running_status_, d1, d2, group, out);
                data_count_ = 0;
                // System common does not support running status.
                if (running_status_ >= 0xF0)
                    running_status_ = 0;
            }
        } // end byte loop

        // Flush any pending partial SysEx bytes so that each send_buffer call
        // produces output proportional to its input rather than accumulating
        // silently until F7 arrives in a later chunk.
        if (in_sysex_ && sysex_buf_count_ > 0)
        {
            emit_sysex_packet(sysex_started_ ? 0x2u : 0x1u,
                              sysex_buf_count_, group, out);
            sysex_started_   = true;
            sysex_buf_count_ = 0;
        }
    }

    //! Discard all accumulated state (call on port close / error recovery).
    void reset()
    {
        in_sysex_        = false;
        sysex_started_   = false;
        sysex_buf_count_ = 0;
        running_status_  = 0;
        data_needed_     = 0;
        data_count_      = 0;
    }

private:
    static int channel_data_len(uint8_t status) noexcept
    {
        const uint8_t type = status >> 4;
        return (type == 0xC || type == 0xD) ? 1 : 2;
    }

    static int sys_common_data_len(uint8_t status) noexcept
    {
        switch (status)
        {
        case 0xF1: return 1;  // MTC Quarter Frame
        case 0xF2: return 2;  // Song Position Pointer
        case 0xF3: return 1;  // Song Select
        default:   return 0;  // 0xF4, 0xF5 (undefined), 0xF6 (Tune Request)
        }
    }

    // Type-2: MIDI 1.0 Channel Voice (32-bit UMP)
    static void emit_type2(uint8_t status, uint8_t d1, uint8_t d2,
                            uint8_t group, std::vector<uint32_t>& out)
    {
        out.push_back((0x2u << 28)
                    | (static_cast<uint32_t>(group)  << 24)
                    | (static_cast<uint32_t>(status) << 16)
                    | (static_cast<uint32_t>(d1)     <<  8)
                    |  static_cast<uint32_t>(d2));
    }

    // Type-1: System Common / Realtime (32-bit UMP)
    static void emit_type1(uint8_t status, uint8_t d1, uint8_t d2,
                            uint8_t group, std::vector<uint32_t>& out)
    {
        out.push_back((0x1u << 28)
                    | (static_cast<uint32_t>(group)  << 24)
                    | (static_cast<uint32_t>(status) << 16)
                    | (static_cast<uint32_t>(d1)     <<  8)
                    |  static_cast<uint32_t>(d2));
    }

    // Type-3: SysEx Data (64-bit UMP pair).
    // sn: 0x0=complete  0x1=start  0x2=continue  0x3=end
    // count: number of valid payload bytes (0-6), read from sysex_buf_.
    void emit_sysex_packet(uint8_t sn, int count, uint8_t group,
                           std::vector<uint32_t>& out)
    {
        uint8_t b[6] = {};
        for (int j = 0; j < count; ++j)
            b[j] = sysex_buf_[j];
        out.push_back((0x3u << 28)
                    | (static_cast<uint32_t>(group) << 24)
                    | (static_cast<uint32_t>(sn)    << 20)
                    | (static_cast<uint32_t>(count) << 16)
                    | (static_cast<uint32_t>(b[0])  <<  8)
                    |  static_cast<uint32_t>(b[1]));
        out.push_back((static_cast<uint32_t>(b[2]) << 24)
                    | (static_cast<uint32_t>(b[3]) << 16)
                    | (static_cast<uint32_t>(b[4]) <<  8)
                    |  static_cast<uint32_t>(b[5]));
    }

    bool    in_sysex_        = false;
    bool    sysex_started_   = false;  // emitted at least one start/continue packet
    uint8_t sysex_buf_[6]    = {};     // pending SysEx bytes not yet emitted
    int     sysex_buf_count_ = 0;      // 0-6
    uint8_t running_status_  = 0;
    int     data_needed_     = 0;
    int     data_count_      = 0;
    uint8_t data_buf_[2]     = {};
};

class RTMIDI_DLL_PUBLIC MidiOutApi : public MidiApi
{
 public:

  MidiOutApi( void );
  virtual ~MidiOutApi( void );
  virtual int sendMessage( const unsigned char *message, size_t size ) = 0;

  //! Block until all queued output for this port has actually been sent.
  /*!
      Lets a caller gate real inter-message timing on delivery (e.g. paced/
      chunked SysEx: sendMessage a chunk, drain(), then delay before the next).
      Default is a no-op - correct for backends whose sendMessage already blocks
      until the message is out. Backends with asynchronous output (e.g. CoreMIDI
      via MIDISendSysex) override this to wait for real completion.
  */
  virtual void drain( void ) {}
};

// **************************************************************** //
//
// Inline RtMidiIn and RtMidiOut definitions.
//
// **************************************************************** //

inline RtMidi::Api RtMidiIn :: getCurrentApi( void ) throw() { return rtapi_->getCurrentApi(); }
inline void RtMidiIn :: openPort( unsigned int portNumber, const std::string &portName ) { rtapi_->openPort( portNumber, portName ); }
inline void RtMidiIn :: openVirtualPort( const std::string &portName ) { rtapi_->openVirtualPort( portName ); }
inline void RtMidiIn :: closePort( void ) { rtapi_->closePort(); }
inline bool RtMidiIn :: isPortOpen() const { return rtapi_->isPortOpen(); }
inline void RtMidiIn :: setCallback( RtMidiCallback callback, void *userData ) { static_cast<MidiInApi *>(rtapi_)->setCallback( callback, userData ); }
inline void RtMidiIn :: cancelCallback( void ) { static_cast<MidiInApi *>(rtapi_)->cancelCallback(); }
inline unsigned int RtMidiIn :: getPortCount( void ) { return rtapi_->getPortCount(); }
inline std::string RtMidiIn :: getPortName( unsigned int portNumber ) { return rtapi_->getPortName( portNumber ); }
inline void RtMidiIn :: ignoreTypes( bool midiSysex, bool midiTime, bool midiSense ) { static_cast<MidiInApi *>(rtapi_)->ignoreTypes( midiSysex, midiTime, midiSense ); }
inline double RtMidiIn :: getMessage( std::vector<unsigned char> *message ) { return static_cast<MidiInApi *>(rtapi_)->getMessage( message ); }
inline void RtMidiIn :: setErrorCallback( RtMidiErrorCallback errorCallback, void *userData ) { rtapi_->setErrorCallback(errorCallback, userData); }
inline void RtMidiIn :: setBufferSize( unsigned int size, unsigned int count ) { static_cast<MidiInApi *>(rtapi_)->setBufferSize(size, count); }

inline RtMidi::Api RtMidiOut :: getCurrentApi( void ) throw() { return rtapi_->getCurrentApi(); }
inline void RtMidiOut :: openPort( unsigned int portNumber, const std::string &portName ) { rtapi_->openPort( portNumber, portName ); }
inline void RtMidiOut :: openVirtualPort( const std::string &portName ) { rtapi_->openVirtualPort( portName ); }
inline void RtMidiOut :: closePort( void ) { rtapi_->closePort(); }
inline bool RtMidiOut :: isPortOpen() const { return rtapi_->isPortOpen(); }
inline unsigned int RtMidiOut :: getPortCount( void ) { return rtapi_->getPortCount(); }
inline std::string RtMidiOut :: getPortName( unsigned int portNumber ) { return rtapi_->getPortName( portNumber ); }
inline int RtMidiOut :: sendMessage( const std::vector<unsigned char> *message ) { return static_cast<MidiOutApi *>(rtapi_)->sendMessage( &message->at(0), message->size() ); }
inline int RtMidiOut :: sendMessage( const unsigned char *message, size_t size ) { return static_cast<MidiOutApi *>(rtapi_)->sendMessage( message, size ); }
inline void RtMidiOut :: drain( void ) { static_cast<MidiOutApi *>(rtapi_)->drain(); }
inline void RtMidiOut :: setErrorCallback( RtMidiErrorCallback errorCallback, void *userData ) { rtapi_->setErrorCallback(errorCallback, userData); }

} // namespace midi
} // namespace rt

#endif

#ifndef RTMIDI_USE_NAMESPACE
using namespace rt::midi;
#endif
