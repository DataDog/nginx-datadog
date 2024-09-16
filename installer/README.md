NGINX INSTALLER
===============
This folder contains the necessary components to install the NGINX module.
It consists of an sh script, plus a go binary (configurator) that performs the actual installation.
Running this installer results in the NGINX module being installed locally.

Shell script
------------
Verifies connectivity to the Datadog Agent. Detects the architecture, downloads 
the Configurator for the specific architecture and invokes it forwarding all the 
parameters plus the architecture.

Configurator
------------
Does the bulk of the installation.
1. Validates parameters
2. Validates NGINX installation and version
3. Downloads the NGINX module
4. Makes neccessary config modifications
5. Validates the modified configurations.

Local Testing
-------------
1. Compile the go binary
```bash
env GOOS=linux go -C configurator build -o ../nginx-configurator
```
2. Start docker compose with the docker-compose file provided. It boots up an NGINX
instance and a Datadog Agent instance with connectivity to each other.
```bash
DD_API_KEY=<YOUR_API_KEY> docker compose -f test/docker-compose.yml up -d
```
3. Run the installer
```bash
docker compose -f test/docker-compose.yml exec nginx bash -c "cd /installer && sh install-nginx-datadog.sh --appId 123 --site datadoghq.com --clientToken abcdef --sessionSampleRate 50 --sessionReplaySampleRate 50 --agentUri http://datadog-agent:8126"
```

Currently, the latest release doesn't yet support rum injection, so expect a message
showing an error when validating the final NGINX configuration `unknown directive
"datadog_rum" in /etc/nginx/nginx.conf`
