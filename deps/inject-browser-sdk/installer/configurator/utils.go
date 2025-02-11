// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	log "github.com/sirupsen/logrus"
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
			return NewInstallerError(InternalError, fmt.Errorf("unsupported file type to extract: %v", header.Typeflag))
		}
	}

	log.Debug("Extracted file '", srcPath, "' to destination '", destPath, "'")

	return nil
}

func unzipFile(srcPath, destPath string) error {
	log.Debug("Unzipping file ", srcPath, " to ", destPath)

	reader, err := zip.OpenReader(srcPath)
	if err != nil {
		return err
	}
	defer reader.Close()

	for _, file := range reader.File {
		if err := copySingleZipFile(file, destPath); err != nil {
			return err
		}
	}
	return nil
}

func copySingleZipFile(file *zip.File, destPath string) error {
	filePath := filepath.Join(destPath, file.Name)
	if file.FileInfo().IsDir() {
		if err := os.MkdirAll(filePath, os.ModePerm); err != nil {
			return err
		}
		return nil
	}

	if err := os.MkdirAll(filepath.Dir(filePath), os.ModePerm); err != nil {
		return err
	}

	destFile, err := os.Create(filePath)
	if err != nil {
		return err
	}
	defer destFile.Close()

	srcFile, err := file.Open()
	if err != nil {
		return err
	}
	defer srcFile.Close()

	if _, err := io.Copy(destFile, srcFile); err != nil {
		return err
	}

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

func ensureProtocol(url string) string {
	if !strings.HasPrefix(url, "http://") && !strings.HasPrefix(url, "https://") {
		return "http://" + url
	}
	return url
}

func commandExists(cmd string) bool {
	_, err := exec.LookPath(cmd)
	return err == nil
}

func verifySignatureGPG(moduleFile, signatureFile, publicKeyFile string) error {
	importCmd := exec.Command("gpg", "--import", publicKeyFile)
	if output, err := importCmd.CombinedOutput(); err != nil {
		return NewInstallerError(InternalError, fmt.Errorf("failed to import public key: %v\nOutput: %s", err, output))
	}

	log.Debug("Imported public key ", publicKeyFile)

	verifyCmd := exec.Command("gpg", "--verify", signatureFile, moduleFile)
	if output, err := verifyCmd.CombinedOutput(); err != nil {
		return NewInstallerError(InternalError, fmt.Errorf("signature verification failed: %v\nOutput: %s", err, output))
	}

	log.Debug("Verified signature ", signatureFile, " for module ", moduleFile)

	return nil
}

func downloadFile(url string) ([]byte, error) {
	log.Debug("Downloading file: ", url)

	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	respBytes, err := io.ReadAll(resp.Body)
	if err != nil {
		return respBytes, err
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 400 {
		return respBytes, NewInstallerError(InternalError, fmt.Errorf("failed to download file with status code %s: %s", resp.Status, string(respBytes)))
	}

	return respBytes, nil
}
