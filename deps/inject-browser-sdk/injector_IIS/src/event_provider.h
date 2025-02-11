 // Define events consumed by the event viewer <https://learn.microsoft.com/en-us/windows/win32/eventlog/reporting-an-event>.
 // Header
 // Categories
//
//  Values are 32 bit values laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//


//
// MessageId: INJECTOR_CATEGORY
//
// MessageText:
//
// Injection Events
//
#define INJECTOR_CATEGORY                ((WORD)0x00000001L)

//
// MessageId: TRACER_CATEGORY
//
// MessageText:
//
// Tracer Events
//
#define TRACER_CATEGORY                  ((WORD)0x00000002L)

 // Messages
//
// MessageId: MSG_GENERIC_INFO
//
// MessageText:
//
// %1
//
#define MSG_GENERIC_INFO                 ((DWORD)0x40000100L)

//
// MessageId: MSG_GENERIC_ERROR
//
// MessageText:
//
// %1
//
#define MSG_GENERIC_ERROR                ((DWORD)0xC0000101L)

//
// MessageId: MSG_GENERIC_DEBUG
//
// MessageText:
//
// debug: %1
//
#define MSG_GENERIC_DEBUG                ((DWORD)0x40000102L)

