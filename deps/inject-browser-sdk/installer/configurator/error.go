// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import "fmt"

type ErrorType int

const (
	UnexpectedError ErrorType = iota
	ArgumentError
	InternalError
	NginxError
	HttpdError
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
	case HttpdError:
		return "HttpdError"
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
