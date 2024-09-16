package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	"github.com/google/go-github/github"

	log "github.com/sirupsen/logrus"
)

func getLowestSupportedModuleVersion() (string, error) {
	client := github.NewClient(nil)

	release, _, err := client.Repositories.GetLatestRelease(context.Background(), "DataDog", "nginx-datadog")
	if err != nil {
		return "", fmt.Errorf("error getting latest release for the NGINX module: %v", err)
	}

	log.Debug("Got latest release for the NGINX module")

	re := regexp.MustCompile(`ngx_http_datadog_module-(?:amd64|arm64)-(\d+\.\d+\.\d+)\.so\.tgz`)

	lowestVersion := ""
	for _, asset := range release.Assets {
		matches := re.FindStringSubmatch(*asset.Name)
		if len(matches) > 1 {
			version := matches[1]
			if lowestVersion == "" || compareVersions(version, lowestVersion) < 0 {
				lowestVersion = version
			}
		}
	}

	if lowestVersion == "" {
		return "", fmt.Errorf("no valid version found for the NGINX module in the release assets")
	}

	return lowestVersion, nil
}

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

	log.Debug("Extracted file '", srcPath, "' to destination '", destPath, "'")

	return nil
}
