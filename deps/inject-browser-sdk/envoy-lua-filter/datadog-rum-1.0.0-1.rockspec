rockspec_format = "3.0"
package = "datadog-rum"
version = "1.0.0-1"
source = {
   url = "git+https://github.com/DataDog/inject-browser-sdk.git"
}
description = {
   summary = "Shared library that injects Browser SDK snippets in HTML documents.",
   detailed = "Shared library that injects Browser SDK snippets in HTML documents.",
   homepage = "https://www.datadoghq.com/",
   license = "Apache-2.0"
}
dependencies = {
   "lua >= 5.4"
}
test = {
   type = "busted"
}
test_dependencies = {
  "busted >= 2.1.1, < 2.2.0"
}
build = {
   type = "builtin",
   modules = {
      ["datadog-rum"] = "src/datadog_rum.lua"
   }
}
