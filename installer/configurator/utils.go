package main

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	log "github.com/sirupsen/logrus"
)

func extractTarGzFile(srcPath, destPath string) error {
	file, err := os.Open(srcPath)
	if err != nil {
		return err
	}
	defer file.Close()

	gzReader, err := gzip.NewReader(file)
	if err != nil {
		return err
	}
	defer gzReader.Close()

	tarReader := tar.NewReader(gzReader)

	for {
		header, err := tarReader.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}

		dest := filepath.Join(destPath, header.Name)

		switch header.Typeflag {
		case tar.TypeReg:
			f, err := os.OpenFile(dest, os.O_CREATE|os.O_RDWR, os.FileMode(header.Mode))
			if err != nil {
				return err
			}
			if _, err := io.Copy(f, tarReader); err != nil {
				f.Close()
				return err
			}
			f.Close()
		default:
			return NewInstallerError(InternalError, fmt.Errorf("unsupported file type to extract: %v", header.Typeflag))
		}
	}

	log.Debug("Extracted file '", srcPath, "' to destination '", destPath, "'")

	return nil
}

func keyValuePairsAsTags(pairs map[string]string) string {
	result := make([]string, 0, len(pairs))
	for key, value := range pairs {
		result = append(result, fmt.Sprintf("%s:%s", key, value))
	}
	return strings.Join(result, ", ")
}

func envBool(key string) bool {
	value := os.Getenv(key)
	value = strings.TrimSpace(strings.ToLower(value))
	return value != "false" && value != "0"
}
