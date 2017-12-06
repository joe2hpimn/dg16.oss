# Guide to Setting Up Dev Environment on Ubuntu 16.04


## Editor

```bash
  sudo apt install emacs24
  sudo apt-get install fonts-inconsolata
```

## Browser

```bash
  wget -q -O - https://dl-ssl.google.com/linux/linux_signing_key.pub | sudo apt-key add - 
  sudo sh -c 'echo "deb [arch=amd64] http://dl.google.com/linux/chrome/deb/ stable main" >> /etc/apt/sources.list.d/google-chrome.list'
  sudo apt-get update 
  sudo apt-get install google-chrome-stable
  google-chrome
```

## Tools and Libraries

```bash
  sudo apt install git
  sudo apt install curl
  sudo apt install m4
  sudo apt install autoconf
  sudo apt install libtool-bin
  sudo apt install zlib1g-dev
  sudo apt install libssl-dev
  sudo apt install libcurl4-openssl-dev
  sudo apt install libreadline-dev
  sudo apt install libbz2-dev
  sudo apt install bison
  sudo apt install flex
  sudo apt install python-dev
  sudo apt install openssh-server
  sudo apt install make 
  sudo apt install unzip 
  sudo snap install --classic go
```

# Logout and Login again

## LLVM

```bash
  cd ~/
  wget http://releases.llvm.org/3.5.2/llvm-3.5.2.src.tar.xz
  tar xvfJ llvm-3.5.2.src.tar.xz
  ln -s llvm-3.5.2.src llvm
  mkdir build
  cd build
  ../llvm/configure --prefix=/opt/llvm-release+assert --enable-targets=x86_64 --enable-optimized=YES
  make clean && make -j8 && sudo make install
  ../llvm/configure --prefix=/opt/llvm-release --enable-targets=x86_64 --enable-optimized=YES --enable-assertions=NO
  make clean && make -j8 && sudo make install
```

## Download Source

```bash
  mkdir ~/p
  cd ~/p
  git clone git@github.com:vitessedata/deepgreen.git
  git clone git@github.com:vitessedata/monona.git
  git clone git@github.com:vitessedata/mendota.git
  git clone git@github.com:vitessedata/toolchain.git
  git clone git@github.com:vitessedata/madlib.git

  mkdir ~/go
```

## Shell Environment

Edit `.profile` and add these lines:

```bash
ulimit -c unlimited
#echo "core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern

export TOOLCHAIN_DIR="$HOME/p/toolchain"
export PATH=".:$TOOLCHAIN_DIR/installed/bin:/usr/local/go/bin:$PATH"
export GOPATH=~/go:~/p/deepgreen/dg:~/p/mendota/phi/go

export EDITOR=vi
```

Also run this so that core files show up in current working directory:

```bash
echo "core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern
```


## System Environment

Greenplum needs specific system parameters to function properly.

Run `sudo vi /etc/sysctl.conf` and add these lines:

```
kernel.shmmax = 500000000
kernel.shmmni = 4096
kernel.shmall = 4000000000
kernel.sem = 250 512000 100 2048
kernel.sysrq = 1
kernel.core_uses_pid = 1
kernel.msgmnb = 65536
kernel.msgmax = 65536
kernel.msgmni = 2048
net.ipv4.tcp_syncookies = 1
net.ipv4.ip_forward = 0
net.ipv4.conf.default.accept_source_route = 0
net.ipv4.tcp_tw_recycle = 1
net.ipv4.tcp_max_syn_backlog = 4096
net.ipv4.conf.all.arp_filter = 1
net.ipv4.ip_local_port_range = 1025 65535
net.core.netdev_max_backlog = 10000
net.core.rmem_max = 2097152
net.core.wmem_max = 2097152
vm.overcommit_memory = 2
```

Run `sudo vi /etc/security/limits.conf` and add these lines:

```
* soft nofile 65536
* hard nofile 65536
* soft nproc 131072
* hard nproc 131072
```

# Logout and Login again 

## Make Toolchain

```bash
  cd toolchain/
  ln -s ~/p/mendota/
  bash build.sh
```

## Make Orca

```bash
  cd ~/p/deepgreen/orca
  bash build.sh   # it will ask for root password. supply it.
```

## Make Deepgreen

```bash
  cd ~/p/deepgreen
  
  ln -s ~/p/mendota
  ln -s ~/p/mendota/vdbtools
  ln -s ~/p/toolchain
  ln -s ~/p/madlib
  ln -s ~/p/mendota/phi

  make config
  make -j8
  make install
```

## Ready for gpinitsystem

```bash
  cd ~/p/deepgreen/run
  source env.sh
  gpssh-exkeys -h localhost  # ignore 'unable to copy authentication files to localhost -- lost connection'
  ssh localhost ls    # may need to trim ~/.ssh/known_hosts
  bash init2.sh
```

