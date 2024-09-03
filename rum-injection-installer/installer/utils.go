package main

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

func compareVersions(v1, v2 string) int {
	v1Segments := strings.Split(v1, ".")
	v2Segments := strings.Split(v2, ".")

	for i := 0; i < len(v1Segments) && i < len(v2Segments); i++ {
		v1digit, _ := strconv.Atoi(v1Segments[i])
		v2digit, _ := strconv.Atoi(v2Segments[i])
		if v1digit < v2digit {
			return -1
		}
		if v1digit > v2digit {
			return 1
		}
	}

	if len(v1Segments) < len(v2Segments) {
		return -1
	}
	if len(v1Segments) > len(v2Segments) {
		return 1
	}

	return 0
}

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
			return fmt.Errorf("unsupported file type to extract: %v", header.Typeflag)
		}
	}

	return nil
}
