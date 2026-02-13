; // Define events consumed by the event viewer <https://learn.microsoft.com/en-us/windows/win32/eventlog/reporting-an-event>.

; // Header

LanguageNames=(English=0x409:MSG00409)

; // Categories

MessageIdTypedef=WORD

MessageId=0x1
SymbolicName=INJECTOR_CATEGORY
Language=English
Injection Events
.

MessageId=0x2
SymbolicName=TRACER_CATEGORY
Language=English
Tracer Events
.

; // Messages

MessageIdTypedef=DWORD

MessageId=0x100
Severity=Informational
SymbolicName=MSG_GENERIC_INFO
Language=English
%1
.

MessageId=0x101
Severity=Error
SymbolicName=MSG_GENERIC_ERROR
Language=English
%1
.

MessageId=0x102
Severity=Informational
SymbolicName=MSG_GENERIC_DEBUG
Language=English
debug: %1
.
