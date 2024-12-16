#!/usr/bin/env ruby

nginx_version_table = <<-TAB
amazonlinux:2023.3.20240219.0 1.24.0
nginx:1.27.3-alpine 1.27.3
nginx:1.27.3 1.27.3
nginx:1.27.2-alpine 1.27.2
nginx:1.27.2 1.27.2
nginx:1.27.1-alpine 1.27.1
nginx:1.27.1 1.27.1
nginx:1.27.0-alpine 1.27.0
nginx:1.27.0 1.27.0
nginx:1.26.2-alpine 1.26.2
nginx:1.26.2 1.26.2
nginx:1.26.1-alpine 1.26.1
nginx:1.26.1 1.26.1
nginx:1.26.0-alpine 1.26.0
nginx:1.26.0 1.26.0
nginx:1.25.5-alpine 1.25.5
nginx:1.25.5 1.25.5
nginx:1.25.4-alpine 1.25.4
nginx:1.25.4 1.25.4
nginx:1.25.3-alpine 1.25.3
nginx:1.25.3 1.25.3
nginx:1.25.2-alpine 1.25.2
nginx:1.25.2 1.25.2
nginx:1.25.1-alpine 1.25.1
nginx:1.25.1 1.25.1
nginx:1.25.0-alpine 1.25.0
nginx:1.25.0 1.25.0
nginx:1.24.0-alpine 1.24.0
nginx:1.24.0 1.24.0
TAB

resty_version_table = <<-TAB
openresty/openresty:1.27.1.1-alpine 1.27.1.1
openresty/openresty:1.25.3.2-alpine 1.25.3.2
openresty/openresty:1.25.3.1-alpine 1.25.3.1
openresty/openresty:1.21.4.4-alpine 1.21.4.4
openresty/openresty:1.21.4.3-alpine 1.21.4.3
openresty/openresty:1.21.4.2-alpine 1.21.4.2
openresty/openresty:1.21.4.1-alpine 1.21.4.1
TAB

SpecLine = Struct.new(:image, :version)

all_nginx_specs = nginx_version_table.each_line.map do |line|
  image, version, archs = line.split
  SpecLine.new(image, version)
end
all_nginx_versions = all_nginx_specs.map(&:version).uniq.sort

all_resty_specs = resty_version_table.each_line.map do |line|
  image, version, archs = line.split
  SpecLine.new(image, version)
end
all_resty_versions = all_resty_specs.map(&:version).uniq.sort

puts <<-YAML.gsub(/^/, '    ')
- build_amd64:
    filters:
        tags:
          only: /^v.*/
    matrix:
      parameters:
        nginx-version:
        - #{all_nginx_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build << matrix.nginx-version >> on amd64 WAF << matrix.waf >>
- build_arm64:
    filters:
        tags:
          only: /^v.*/
    matrix:
      parameters:
        nginx-version:
        - #{all_nginx_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build << matrix.nginx-version >> on arm64 WAF << matrix.waf >>
YAML

puts <<-YAML.gsub(/^/, '    ')
- build_openresty_amd64:
    matrix:
      parameters:
        resty-version:
        - #{all_resty_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build openresty << matrix.resty-version >> on amd64 WAF << matrix.waf >>
- build_openresty_arm64:
    matrix:
      parameters:
        resty-version:
        - #{all_resty_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build openresty << matrix.resty-version >> on arm64 WAF << matrix.waf >>
YAML

all_nginx_specs.group_by(&:version).each do |version, specs|
    puts <<~YAML.gsub(/^/, '    ')
    - test:
        filters:
            tags:
              only: /^v.*/
        matrix:
          parameters:
            arch:
            - amd64
            - arm64
            waf:
            - 'ON'
            - 'OFF'
            base-image:
            - #{specs.map(&:image).join("\n        - ")}
            nginx-version:
            - #{version}
        name: test << matrix.nginx-version >> on << matrix.base-image >>:<< matrix.arch >> WAF << matrix.waf >>
        requires:
        - build << matrix.nginx-version >> on << matrix.arch >> WAF << matrix.waf >>
    YAML
end

all_resty_specs.group_by(&:version).each do |version, specs|
  puts <<~YAML.gsub(/^/, '    ')
  - test-openresty:
      matrix:
        parameters:
          arch:
          - amd64
          - arm64
          waf:
          - 'ON'
          - 'OFF'
          base-image:
          - #{specs.map(&:image).join("\n        - ")}
          resty-version:
          - #{version}
      name: test openresty << matrix.resty-version >> on << matrix.base-image >>:<< matrix.arch >> WAF << matrix.waf >>
      requires:
      - build openresty << matrix.resty-version >> on << matrix.arch >> WAF << matrix.waf >>
  YAML
end

