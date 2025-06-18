#!/bin/bash
set -e
set -x
sudo apt-get update
sudo apt-get -y install curl j2cli
#TODO: adjust kubectl version to the cluster
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
KUBEVIRT_VERSION=$(kubectl get kubevirt.kubevirt.io/kubevirt -n kubevirt -o=jsonpath="{.status.observedKubeVirtVersion}")
curl -L -o virtctl https://github.com/kubevirt/kubevirt/releases/download/${KUBEVIRT_VERSION}/virtctl-${KUBEVIRT_VERSION}-linux-amd64
sudo install -o root -g root -m 0755 virtctl /usr/local/bin

ssh-keygen -b 2048 -t rsa -f ./identity -q -N ""
vm_user=$INPUT_VM_USER
export vm_ssh_authorized_keys=$(cat ./identity.pub | xargs)
export kernel_version=$INPUT_KERNEL_VERSION
export vm_name=$INPUT_VM_NAME
#TODO: change vm source
curl -L -o vm.yml.j2 https://raw.githubusercontent.com/MaisenbacherD/blktests-ci/refs/heads/fix-arc/playbooks/roles/k8s-install-kubevirt-actions-runner-controller/templates/fedora-var-kernel-vm.yaml.j2
j2 vm.yml.j2 -o vm.yml
kubectl create -f vm.yml
ssh_options=(--identity-file=$(realpath ./identity | xargs) --local-ssh --local-ssh-opts="-o StrictHostKeyChecking=no")
while true; do
  echo "Waiting for VM to be up and running"
  virtctl ssh ${vm_user}@${vm_name} "${ssh_options[@]}" --command="ls /vm-ready" && break
  sleep 10
done

run_cmds=$INPUT_RUN_CMDS
virtctl ssh ${vm_user}@${vm_name} "${ssh_options[@]}" --command="${run_cmds}"
