#!/bin/bash

# script to automate setup of cs538 CloudLab VM with Kathara, our github repo, and other tools as needed.
# Run script from local machine.. uses provided keypair to execute commands remotely over SSH. 

# input validation
if [[ "$#" -ne 2 || -z "$1" || -z "$2" ]]; then
  echo "Usage: $0 <hostname> <ssh-key-path>"
  exit 1
fi

HOSTNAME=$1
SSH_KEY=$2

# SSH into VM using key and import repos 
echo "Setting up $HOSTNAME..."

ssh -t -i "$SSH_KEY" ymangel2@"$HOSTNAME" << 'EOF'
  set -e
  echo "Running remote setup on $(hostname)"
  sudo add-apt-repository ppa:katharaframework/kathara
  sudo DEBIAN_FRONTEND=noninteractive apt update && sudo DEBIAN_FRONTEND=noninteractive apt install -y wireshark && sudo apt install -y git kathara
  git clone https://github.com/jeffbyju/optimized_vr_streaming.git
EOF


