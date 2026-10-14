#!/bin/bash
set -e

REPO_NAME="${1:-esp32-matrix}"
REMOTE_URL="git@github.com:heistcat/${REPO_NAME}.git"

echo "=== Настройка удаленного репозитория GitHub ==="
echo "Репозиторий: $REMOTE_URL"

if git remote | grep -q "^origin$"; then
    git remote set-url origin "$REMOTE_URL"
else
    git remote add origin "$REMOTE_URL"
fi

echo "=== Отправка ветки master на GitHub ==="
git push -u origin master

echo "=== Отправка ветки gh-pages на GitHub ==="
git push -u origin gh-pages

echo ""
echo "============================================================"
echo "🎉 Готово! Проект успешно загружен на GitHub."
echo "Ваше веб-приложение будет доступно по ссылке:"
echo "https://heistcat.github.io/${REPO_NAME}/"
echo "============================================================"
