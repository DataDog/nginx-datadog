package main

import "fmt"

type ErrorType int

const (
	UnexpectedError ErrorType = iota
	ArgumentError
	InternalError
	NginxError
	TelemetryError
)

func (e ErrorType) String() string {
	switch e {
	case UnexpectedError:
		return "UnexpectedError"
	case ArgumentError:
		return "ArgumentError"
	case InternalError:
		return "InternalError"
	case NginxError:
		return "NginxError"
	case TelemetryError:
		return "TelemetryError"
	default:
		return fmt.Sprintf("%d", int(e))
	}
}

func NewInstallerError(errorType ErrorType, err error) *InstallerError {
	return &InstallerError{
		ErrorType: errorType,
		Err:       err,
	}
}

type InstallerError struct {
	ErrorType ErrorType
	Err       error
}

func (m *InstallerError) Error() string {
	return fmt.Sprintf("%s: %v", m.ErrorType, m.Err)
}
