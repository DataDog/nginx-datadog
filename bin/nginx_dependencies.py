import subprocess
import os

LATEST_VERSION_SUPPORTED = "1.26.0"

def if_version_supported(version):
    return tuple(map(int, version.split('.'))) > tuple(map(int, LATEST_VERSION_SUPPORTED.split('.')))

def update_nginx_version(source_file, destination_file, new_version):
    with open(source_file, 'r') as file:
        file_content = file.read()

    updated_content = file_content.replace("{NGINX_VERSION}", new_version)

    with open(destination_file, 'w') as file:
        file.write(updated_content)

# set the pwd in the bin folder
abspath = os.path.abspath(__file__)
dname = os.path.dirname(abspath)
os.chdir(dname)

get_nginx_release_versions = subprocess.Popen(['bash', "nginx_release_downloads.sh"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
stdout, stderr = get_nginx_release_versions.communicate()

if get_nginx_release_versions.returncode != 0:
    print("Could not get nginx versions:", stderr.decode())
    exit()


for line in stdout.decode().splitlines():
    version, link = line.split(' ', 1)
    
    if if_version_supported(version):
        print("export NGINX_VERSION_TO_TEST="+version)
        update_nginx_version("base_config.yml", "config.yml", version)
        subprocess.run(["mv config.yml ../.circleci/config.yml"], shell=True, check=True)
        break
