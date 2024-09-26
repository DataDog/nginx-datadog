import subprocess
import os

LATEST_VERSION_SUPPORTED = "1.27.0"

def if_version_supported(version):
    return tuple(map(int, version.split('.'))) > tuple(map(int, LATEST_VERSION_SUPPORTED.split('.')))

# set the pwd in the bin folder
abspath = os.path.abspath(__file__)
dname = os.path.dirname(abspath)
os.chdir(dname)

get_nginx_release_versions = subprocess.Popen(['bash', "nginx_release_downloads.sh"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
stdout, stderr = get_nginx_release_versions.communicate()

if get_nginx_release_versions.returncode != 0:
    print("Could not get nginx versions:", stderr.decode())
else:
    for line in stdout.decode().splitlines():
        version, link = line.split(' ', 1)
        
        if if_version_supported(version):
            print(version)

            ruby_script = "generate_jobs_yaml.rb"
            new_version = "nginx:{}-alpine {}\nnginx:{} {}\n".format(version, version, version, version)

            with open(ruby_script, 'r') as file:
                content = file.readlines()

            last_amazon_version_line = -1
            for i, line in enumerate(content):
                if "amazonlinux" in line:
                    last_amazon_version_line = i
                
            content.insert(last_amazon_version_line + 1, new_version)

            with open(ruby_script, 'w') as file:
                file.writelines(content)

            get_build_commands = subprocess.Popen(['./' + ruby_script], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = get_build_commands.communicate()
            
            with open("base_config.yml") as file:
                config_content = file.read()

            updated_config_content = config_content + stdout.decode()

            with open("config.yml", 'w') as file:
                file.write(updated_config_content)

            subprocess.run(["mv config.yml ../.circleci/config.yml"], shell=True, check=True)
            break
