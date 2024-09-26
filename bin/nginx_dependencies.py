import subprocess

LATEST_VERSION_SUPPORTED = "1.26.0"

def if_version_supported(version):
    return tuple(map(int, version.split('.'))) > tuple(map(int, LATEST_VERSION_SUPPORTED.split('.')))

def update_nginx_version(source_file, destination_file, new_version):
    with open(source_file, 'r') as file:
        file_content = file.read()

    updated_content = file_content.replace("{NGINX_VERSION}", new_version)

    with open(destination_file, 'w') as file:
        file.write(updated_content)

    print(f"La version de NGINX a été remplacée par {new_version} et enregistrée dans {destination_file}.")

get_nginx_release_versions = subprocess.Popen(['bash', "nginx_release_downloads.sh"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
stdout, stderr = get_nginx_release_versions.communicate()

if get_nginx_release_versions.returncode != 0:
    print("Could not get nginx versions:", stderr.decode())
    exit()


for line in stdout.decode().splitlines():
    version, link = line.split(' ', 1)
    
    if if_version_supported(version):
        print("Version ", version, " not supported") 
        update_nginx_version("base_config.yml", "config.yml", version)
