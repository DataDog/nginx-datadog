# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

FROM mcr.microsoft.com/windows/servercore:ltsc2019-amd64

SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

EXPOSE 80
EXPOSE 8080
EXPOSE 8081

# Pin to an older version of chocolatey because recent versions
# required .NET Framework 4.8.
# More details: <https://stackoverflow.com/questions/76470752/chocolatey-installation-in-docker-started-to-fail-restart-due-to-net-framework>
ENV chocolateyVersion=1.4.0

RUN Set-ExecutionPolicy Bypass -Scope Process -Force; \
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; \
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

RUN Add-WindowsFeature Web-Server; \
    choco install -y urlrewrite iis-arr 

RUN choco install -y python3 neovim
RUN pip install --no-cache-dir -r requirements.txt

ENTRYPOINT powershell
