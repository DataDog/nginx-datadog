#!/usr/bin/env ruby

version_table = <<-TAB
amazonlinux:2023.3.20240219.0 1.24.0
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

SpecLine = Struct.new(:image, :version)

all_specs = version_table.each_line.map do |line|
  image, version, archs = line.split
  SpecLine.new(image, version)
end

all_versions = all_specs.map(&:version).uniq.sort

puts <<-YAML.gsub(/^/, '    ')
- build_amd64:
    filters:
        tags:
          only: /^v.*/
    matrix:
      parameters:
        nginx-version:
        - #{all_versions.join("\n        - ")}
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
        - #{all_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build << matrix.nginx-version >> on arm64 WAF << matrix.waf >>
YAML

all_specs.group_by(&:version).each do |version, specs|
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

