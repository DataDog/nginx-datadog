include(FetchContent)

FetchContent_Declare(
  nghttp2
  URL "https://github.com/nghttp2/nghttp2/releases/download/v1.62.1/nghttp2-1.62.1.tar.gz"
  URL_MD5 "73b43fc2385d2849f289edc3d1cb8df2"
)

FetchContent_MakeAvailable(nghttp2)
