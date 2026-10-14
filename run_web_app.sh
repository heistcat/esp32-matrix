#!/bin/bash
IP=$(ip route get 1.1.1.1 2>/dev/null | awk '{print $7}')
[ -z "$IP" ] && IP="127.0.0.1"
PORT=8443
URL="https://${IP}:${PORT}/"

echo "=========================================="
echo "  ESP32 Matrix BLE Studio Web Server"
echo "=========================================="
echo "URL для смартфона: $URL"
echo ""
echo "Отсканируйте QR-код камерой смартфона:"
qrencode -t ANSIUTF8 "$URL"
echo ""
echo "• На Android: откройте ссылку в Chrome (нажмите 'Дополнительно' -> 'Перейти')"
echo "• На iPhone/iPad: откройте в приложении Bluefy"
echo "• Нажмите '⚡ Подключить Bluetooth' и выберите ESP32-Matrix-BLE (PIN: 1234)"
echo "• Нажмите Ctrl+C для остановки сервера"
echo "=========================================="

python3 -c "
import http.server, ssl, os
os.chdir('ble_app')
server = http.server.HTTPServer(('0.0.0.0', $PORT), http.server.SimpleHTTPRequestHandler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('cert.pem', 'key.pem')
server.socket = ctx.wrap_socket(server.socket, server_side=True)
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
"
