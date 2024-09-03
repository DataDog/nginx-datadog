RUM INJECTION INSTALLER
=======================
This folder contains the necessary components to run the nginx module installation.
It consists of an sh script, plus a go binary that performs the actual installation.

Local Testing
-------------
1. Compile the go binary
```bash
env GOOS=linux go build -o nginx-configurator ./installer
```
2. Update the DD_API_KEY env variable in `docker-compose.yml`
3. Start docker compose with the docker-compose file provided. It boots up an NGINX
instance and a Datadog Agent instance with connectivity to each other.
```bash
docker compose up -d
```
4. Run the installer
```bash
docker compose exec nginx bash -c "cd /installer && sh install_script.sh --appId 123 --site datadoghq.com --clientToken abcdef --sessionSampleRate 50 --sessionReplaySampleRate 50 --agentUrl http://datadog-agent:8126"
```

Currently, the latest release doesn't yet support rum injection, so expect a message
showing an error when validating the final NGINX configuration `unknown directive
"datadog_rum" in /etc/nginx/nginx.conf`
