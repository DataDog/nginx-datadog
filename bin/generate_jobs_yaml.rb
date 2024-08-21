#!/usr/bin/env ruby

version_table = <<-TAB
amazonlinux:2023.3.20240219.0 1.24.0
amazonlinux:2.0.20230418.0 1.22.1
amazonlinux:2.0.20230320.0 1.22.1
amazonlinux:2.0.20230307.0 1.22.1
amazonlinux:2.0.20230221.0 1.22.1
amazonlinux:2.0.20230207.0 1.22.1
amazonlinux:2.0.20230119.1 1.22.1
amazonlinux:2.0.20221210.0 1.22.1
amazonlinux:2.0.20221103.3 1.22.1
amazonlinux:2.0.20221004.0 1.22.1
amazonlinux:2.0.20220912.1 1.22.1
amazonlinux:2.0.20220805.0 1.22.1
amazonlinux:2.0.20220719.0 1.22.1
amazonlinux:2.0.20220606.1 1.22.1
amazonlinux:2.0.20220426.0 1.22.1
amazonlinux:2.0.20220419.0 1.22.1
amazonlinux:2.0.20220406.1 1.22.1
amazonlinux:2.0.20220316.0 1.22.1
amazonlinux:2.0.20220218.1 1.22.1
amazonlinux:2.0.20220121.0 1.22.1
nginx:1.27.1-alpine 1.27.1
nginx:1.27.1 1.27.1
nginx:1.27.0-alpine 1.27.0
nginx:1.27.0 1.27.0
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
nginx:1.23.4-alpine 1.23.4
nginx:1.23.4 1.23.4
nginx:1.23.3-alpine 1.23.3
nginx:1.23.3 1.23.3
nginx:1.23.2-alpine 1.23.2
nginx:1.23.2 1.23.2
nginx:1.23.1-alpine 1.23.1
nginx:1.23.1 1.23.1
nginx:1.23.0-alpine 1.23.0
nginx:1.23.0 1.23.0
nginx:1.22.1-alpine 1.22.1
nginx:1.22.1 1.22.1
nginx:1.22.0-alpine 1.22.0
nginx:1.22.0 1.22.0
TAB

SpecLine = Struct.new(:image, :version)

all_specs = version_table.each_line.map do |line|
  image, version, archs = line.split
  SpecLine.new(image, version)
end

all_versions = all_specs.map(&:version).uniq.sort

puts <<-YAML.gsub(/^/, '    ')
- build_amd64:
    matrix:
      parameters:
        nginx-version:
        - #{all_versions.join("\n        - ")}
        waf:
        - 'ON'
        - 'OFF'
    name: build << matrix.nginx-version >> on amd64 WAF << matrix.waf >>
- build_arm64:
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

