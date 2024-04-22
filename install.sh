#! /bin/sh
apt-get install -y libgnutls28-dev libev-dev libpam0g-dev liblz4-dev libseccomp-dev \
        libreadline-dev libnl-route-3-dev libkrb5-dev libradcli-dev \
        libcurl4-gnutls-dev libcjose-dev libjansson-dev libprotobuf-c-dev \
        libtalloc-dev libhttp-parser-dev protobuf-c-compiler gperf \
        nuttcp lcov libuid-wrapper libpam-wrapper libnss-wrapper \
        libsocket-wrapper gss-ntlmssp haproxy iputils-ping freeradius \
        gawk gnutls-bin iproute2 yajl-tools tcpdump
#sysctl -w net.ipv4.ip_forward =1
#sudo iptables -t nat -o eth0 -A POSTROUTING -j MASQUERADE
#apt-get install iptables-persistent
#netfilter-persistent save
#iptables -I INPUT -p tcp --dport 443 -j ACCEPT
#iptables -I INPUT -p udp --dport 443 -j ACCEPT
