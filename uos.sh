# 系统诊断脚本
#!/bin/bash
echo "=== UOS SSL Diagnosis ==="
echo "System Info:"
cat /etc/os-release | grep -E "NAME|VERSION"

echo "\nSSL Certificates:"
ls -la /etc/ssl/certs/ca-certificates.crt
ls -la /etc/pki/tls/certs/ca-bundle.crt

echo "\nSSL Environment:"
echo "SSL_CERT_FILE: $SSL_CERT_FILE"
echo "SSL_CERT_DIR: $SSL_CERT_DIR"

echo "\nNetwork Test:"
curl -v https://m.crealitycloud.cn 2>&1 | head -20

echo "\nOpenSSL Test:"
openssl s_client -connect m.crealitycloud.cn:443 -servername m.crealitycloud.com < /dev/null
