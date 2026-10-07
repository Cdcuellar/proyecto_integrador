Vagrant.configure("2") do |config|
  config.vm.box = "ubuntu/jammy64"
  config.vm.hostname = "servidor-iot"

  # Puerto del servidor Node, solo accesible desde tu PC (ngrok lo publica)
  config.vm.network "forwarded_port", guest: 3000, host: 3000, host_ip: "127.0.0.1"

  # Puerto de MongoDB (para cuando lo instales), solo accesible desde tu PC
  config.vm.network "forwarded_port", guest: 27017, host: 27017, host_ip: "127.0.0.1"

  # La carpeta "server" de tu PC aparece dentro de la VM
  config.vm.synced_folder "./server", "/home/vagrant/server"

  config.vm.provider "virtualbox" do |vb|
    vb.memory = 2048
    vb.cpus = 2
  end

  # Instala Node.js 22 (LTS) la primera vez
  config.vm.provision "shell", inline: <<-SHELL
    curl -fsSL https://deb.nodesource.com/setup_22.x | bash -
    apt-get install -y nodejs
  SHELL
end
