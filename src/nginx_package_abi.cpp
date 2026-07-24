#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "nginx_package_abi.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#ifndef DD_NGINX_DEBIAN_UBUNTU_BUILD
#define DD_NGINX_DEBIAN_UBUNTU_BUILD 0
#endif

namespace datadog::nginx::package_abi {
namespace {

using NginxMain = int (*)(int, char *const *);

constexpr std::string_view kUbuntuVersionMarker = "(Ubuntu)";
constexpr std::string_view kUbuntuConfigureMarker = "--build=Ubuntu";
constexpr std::string_view kDebianBuildPath =
    "-ffile-prefix-map=/build/reproducible-path/nginx-";
constexpr std::array<std::string_view, 3> kDebianPackagingLayout = {
    "--prefix=/usr/share/nginx",
    "--modules-path=/usr/lib/nginx/modules",
    "--http-client-body-temp-path=/var/lib/nginx/body",
};

bool contains(std::string_view text, std::string_view marker) noexcept {
  return text.find(marker) != std::string_view::npos;
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int value) : value_{value} {}
  ~FileDescriptor() {
    if (value_ >= 0) {
      close(value_);
    }
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  int get() const { return value_; }

 private:
  int value_;
};

std::optional<std::string> running_nginx_version_info() {
  const auto nginx_main =
      reinterpret_cast<NginxMain>(dlsym(RTLD_DEFAULT, "main"));
  auto *const test_config = reinterpret_cast<std::uintptr_t *>(
      dlsym(RTLD_DEFAULT, "ngx_test_config"));
  if (nginx_main == nullptr || test_config == nullptr) {
    return std::nullopt;
  }

  FileDescriptor output{memfd_create("nginx-version", MFD_CLOEXEC)};
  if (output.get() == -1) {
    return std::nullopt;
  }

  const pid_t child = fork();
  if (child == -1) {
    return std::nullopt;
  }

  if (child == 0) {
    // The parent may be processing `nginx -t`. Without clearing this flag,
    // nginx's main() continues into a second configuration after printing
    // its version instead of returning immediately.
    *test_config = 0;

    if (dup2(output.get(), STDERR_FILENO) == -1) {
      _exit(1);
    }

    char executable[] = "nginx";
    char version[] = "-V";
    char *arguments[] = {executable, version, nullptr};
    _exit(nginx_main(2, arguments) == 0 ? 0 : 1);
  }

  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);

  if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
      lseek(output.get(), 0, SEEK_SET) == -1) {
    return std::nullopt;
  }

  std::string result;
  std::array<char, 4096> buffer;
  for (;;) {
    const ssize_t count = read(output.get(), buffer.data(), buffer.size());
    if (count > 0) {
      result.append(buffer.data(), static_cast<size_t>(count));
    } else if (count == -1 && errno == EINTR) {
      continue;
    } else if (count == 0) {
      return result;
    } else {
      return std::nullopt;
    }
  }
}

}  // namespace

bool is_debian_or_ubuntu_build(std::string_view version_info) noexcept {
  if (contains(version_info, kUbuntuVersionMarker) ||
      contains(version_info, kUbuntuConfigureMarker)) {
    return true;
  }

  if (!contains(version_info, kDebianBuildPath)) {
    return false;
  }

  for (const auto marker : kDebianPackagingLayout) {
    if (!contains(version_info, marker)) {
      return false;
    }
  }
  return true;
}

bool running_nginx_is_debian_or_ubuntu() noexcept {
  try {
    const auto version_info = running_nginx_version_info();
    return version_info && is_debian_or_ubuntu_build(*version_info);
  } catch (...) {
    return false;
  }
}

bool module_was_built_for_debian_or_ubuntu() noexcept {
  return DD_NGINX_DEBIAN_UBUNTU_BUILD;
}

}  // namespace datadog::nginx::package_abi
