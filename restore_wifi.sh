#!/bin/bash
echo "========================================================"
echo "  Восстановление исходной версии с Wi-Fi и Web-сервером"
echo "========================================================"
PORT="${1:-/dev/ttyACM0}"
if [ ! -f "backup/esp32c3_fastled_wifi_backup.ino" ]; then
  echo "Ошибка: файл резервной копии не найден!"
  exit 1
fi
cp backup/esp32c3_fastled_wifi_backup.ino esp32c3_fastled.ino
cp backup/ble_app_html_backup.h ble_app_html.h
./upload.sh "$PORT"
echo "========================================================"
echo "  Успешно! Версия с Wi-Fi восстановлена и загружена."
echo "========================================================"
