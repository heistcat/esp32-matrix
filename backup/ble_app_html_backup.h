#pragma once
#include <Arduino.h>

const char BLE_APP_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>ESP32 Matrix BLE Studio</title>
<style>
  :root {
    --bg: #070a12;
    --card: #101726;
    --card-hover: #162035;
    --accent: #00f0ff;
    --accent2: #ff007b;
    --accent-grad: linear-gradient(135deg, #00f0ff, #ff007b);
    --text: #f0f4fc;
    --text-dim: #7f94b4;
    --border: #1b263d;
    --led-off: #141c2e;
    --success: #00ff66;
    --danger: #ff3366;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; -webkit-tap-highlight-color: transparent; }
  body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 12px; display: flex; justify-content: center; }
  .container { width: 100%; max-width: 580px; display: flex; flex-direction: column; gap: 14px; }

  /* BLE Connection Header */
  .ble-banner { background: #0c1322; border: 1px solid var(--border); border-radius: 16px; padding: 14px; display: flex; flex-direction: column; gap: 10px; box-shadow: 0 4px 20px rgba(0,0,0,0.5); }
  .ble-status-row { display: flex; justify-content: space-between; align-items: center; }
  .ble-status-badge { display: inline-flex; align-items: center; gap: 8px; font-size: 13px; font-weight: 700; padding: 4px 10px; border-radius: 20px; background: #162035; color: var(--text-dim); }
  .ble-dot { width: 10px; height: 10px; border-radius: 50%; background: #666; }
  .ble-dot.connected { background: var(--success); box-shadow: 0 0 10px var(--success); }
  .ble-dot.connecting { background: #ffbb00; animation: pulse 1s infinite; }
  @keyframes pulse { 0% { opacity: 0.3; } 50% { opacity: 1; } 100% { opacity: 0.3; } }

  /* Navigation Tabs */
  .nav-tabs { display: grid; grid-template-columns: repeat(5, 1fr); gap: 4px; background: #0b111e; padding: 5px; border-radius: 14px; border: 1px solid var(--border); }
  .nav-btn { background: transparent; border: none; color: var(--text-dim); font-size: 11px; font-weight: 600; padding: 10px 2px; border-radius: 10px; cursor: pointer; text-align: center; transition: all 0.2s; }
  .nav-btn.active { background: var(--card); color: var(--accent); box-shadow: 0 4px 12px rgba(0,0,0,0.4); border: 1px solid var(--border); }

  .tab-content { display: none; flex-direction: column; gap: 14px; }
  .tab-content.active { display: flex; }

  .card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 16px; display: flex; flex-direction: column; gap: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.3); }
  .card-title { font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-dim); display: flex; justify-content: space-between; align-items: center; }

  /* Buttons & Inputs */
  .input-row { display: flex; gap: 8px; }
  input[type="text"], input[type="password"] { flex: 1; background: #080d18; border: 1px solid var(--border); border-radius: 10px; color: #fff; font-size: 15px; padding: 10px 14px; outline: none; }
  input[type="text"]:focus, input[type="password"]:focus { border-color: var(--accent); }
  button { background: var(--accent-grad); color: #fff; border: none; font-size: 13px; font-weight: 600; padding: 10px 16px; border-radius: 10px; cursor: pointer; transition: transform 0.1s, opacity 0.2s; }
  button:active { transform: scale(0.97); }
  button.sec { background: #162035; color: var(--text); border: 1px solid var(--border); }
  button.danger { background: var(--danger); color: #fff; }
  button.success { background: #00b050; color: #fff; }

  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; }
  .grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }
  .grid-5 { display: grid; grid-template-columns: repeat(5, 1fr); gap: 6px; }

  /* Sliders */
  .slider-group { display: flex; flex-direction: column; gap: 6px; }
  .slider-head { display: flex; justify-content: space-between; font-size: 13px; }
  .slider-val { font-weight: 600; color: var(--accent); }
  input[type="range"] { -webkit-appearance: none; width: 100%; height: 6px; border-radius: 4px; background: #1a253c; outline: none; }
  input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; box-shadow: 0 0 6px rgba(0,240,255,0.6); }

  /* Presets */
  .presets { display: flex; flex-wrap: wrap; gap: 6px; }
  .preset-btn { background: #141d30; border: 1px solid var(--border); color: #b9cced; font-size: 12px; padding: 6px 10px; border-radius: 8px; cursor: pointer; transition: all 0.2s; }
  .preset-btn:hover { border-color: var(--accent); background: #1b2844; }

  /* Mode Button */
  .btn-select { background: #121a2d; border: 1px solid var(--border); color: var(--text); padding: 9px 5px; border-radius: 10px; font-size: 12px; font-weight: 600; text-align: center; cursor: pointer; transition: all 0.2s; }
  .btn-select.active { border-color: var(--accent); background: rgba(0,240,255,0.12); color: var(--accent); box-shadow: 0 0 10px rgba(0,240,255,0.2); }

  /* Canvas 20x5 */
  .matrix-canvas-wrap { display: flex; justify-content: center; padding: 10px; background: #060912; border-radius: 14px; border: 1px solid var(--border); touch-action: none; }
  .pixel-matrix { display: grid; grid-template-columns: repeat(20, 1fr); gap: 4px; width: 100%; max-width: 520px; }
  .led-pixel { aspect-ratio: 1; border-radius: 50%; background: var(--led-off); cursor: pointer; transition: background 0.05s; border: 1px solid rgba(255,255,255,0.04); }

  /* Color Palette */
  .palette-bar { display: flex; gap: 6px; align-items: center; overflow-x: auto; padding-bottom: 4px; }
  .pal-color { width: 32px; height: 32px; border-radius: 50%; cursor: pointer; border: 2px solid transparent; flex-shrink: 0; }
  .pal-color.active { border-color: #fff; transform: scale(1.1); box-shadow: 0 0 8px rgba(255,255,255,0.5); }
  input[type="color"] { -webkit-appearance: none; border: none; width: 34px; height: 34px; border-radius: 50%; cursor: pointer; background: transparent; flex-shrink: 0; }
  input[type="color"]::-webkit-color-swatch-wrapper { padding: 0; }
  input[type="color"]::-webkit-color-swatch { border: 2px solid var(--border); border-radius: 50%; }

  /* Timeline */
  .timeline { display: flex; gap: 8px; overflow-x: auto; padding: 6px 0; align-items: center; }
  .frame-thumb { background: #141d30; border: 2px solid var(--border); border-radius: 8px; min-width: 52px; height: 36px; display: flex; align-items: center; justify-content: center; font-size: 12px; font-weight: 700; cursor: pointer; color: var(--text-dim); }
  .frame-thumb.active { border-color: var(--accent); color: var(--accent); background: rgba(0,240,255,0.15); }

  /* Effects cards */
  .effect-card { background: #121a2d; border: 1px solid var(--border); border-radius: 14px; padding: 14px; display: flex; flex-direction: column; gap: 10px; transition: all 0.2s; }
  .effect-card:hover { border-color: #2b3a5c; }
  .effect-card.active { border-color: var(--accent); background: rgba(0,240,255,0.08); box-shadow: 0 0 16px rgba(0,240,255,0.15); }
  .eff-head { display: flex; align-items: center; justify-content: space-between; }
  .eff-info h4 { font-size: 15px; font-weight: 700; }
  .eff-info p { font-size: 12px; color: var(--text-dim); margin-top: 2px; }
  .eff-actions { display: flex; gap: 8px; }
  .eff-actions button { flex: 1; padding: 8px 12px; font-size: 12px; }

  /* Live Stream Visualizer */
  .vu-meter-bar { display: flex; gap: 2px; height: 60px; align-items: flex-end; background: #060912; padding: 6px; border-radius: 10px; border: 1px solid var(--border); }
  .vu-col { flex: 1; background: var(--accent-grad); border-radius: 2px; transition: height 0.05s ease; min-height: 4px; }

  /* Modal */
  .modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.75); z-index: 200; align-items: center; justify-content: center; padding: 20px; }
  .modal-overlay.open { display: flex; }
  .modal-box { background: var(--card); border: 1px solid var(--accent); border-radius: 16px; width: 100%; max-width: 380px; padding: 20px; display: flex; flex-direction: column; gap: 14px; }

  #toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #18233a; border: 1px solid var(--accent); color: #fff; padding: 10px 22px; border-radius: 30px; font-size: 13px; font-weight: 600; opacity: 0; pointer-events: none; transition: opacity 0.3s; box-shadow: 0 8px 24px rgba(0,0,0,0.6); z-index: 300; }
  #toast.show { opacity: 1; }
</style>
</head>
<body>

<div class="container">
  <!-- BLE Header -->
  <div class="ble-banner">
    <div class="ble-status-row">
      <div class="ble-status-badge">
        <div class="ble-dot" id="bleStatusDot"></div>
        <span id="bleStatusText">BLE: Отключено</span>
      </div>
      <button class="sec" style="padding:6px 12px; font-size:12px;" onclick="openPinModal()">🔑 PIN-код</button>
    </div>
    <div class="grid-2">
      <button id="bleConnectBtn" onclick="toggleBleConnection()">⚡ Подключить Bluetooth</button>
      <button class="sec" id="syncStatusBtn" onclick="requestStatus()" disabled>🔄 Статус ESP32</button>
    </div>
  </div>

  <!-- Navigation Tabs -->
  <div class="nav-tabs">
    <button class="nav-btn active" id="btn-tabSmart" onclick="switchTab('tabSmart')">🕒 Виджеты</button>
    <button class="nav-btn" id="btn-tabText" onclick="switchTab('tabText')">🔤 Текст</button>
    <button class="nav-btn" id="btn-tabDraw" onclick="switchTab('tabDraw')">🎨 Рисование</button>
    <button class="nav-btn" id="btn-tabEffects" onclick="switchTab('tabEffects')">🎬 Эффекты</button>
    <button class="nav-btn" id="btn-tabSettings" onclick="switchTab('tabSettings')">⚙️ Инфо</button>
  </div>

  <!-- ТАБ 1: СМАРТ-ВИДЖЕТЫ (ТЕЛЕФОН -> МАТРИЦА) -->
  <div id="tabSmart" class="tab-content active">
    <!-- ЧАСЫ СО СМАРТФОНА -->
    <div class="card">
      <div class="card-title">
        <span>🕒 Точные часы смартфона</span>
        <span id="clockPreview" style="color:var(--accent); font-weight:700;">12:00</span>
      </div>
      <p style="font-size:12px; color:var(--text-dim);">Транслирует точное время вашего телефона прямо на матрицу без интернета на ESP32.</p>
      <div class="grid-2">
        <div class="btn-select active" id="clockModeCenter" onclick="setClockFormat(5)">🎯 Статичные (по центру)</div>
        <div class="btn-select" id="clockModeScroll" onclick="setClockFormat(0)">➡️ Бегущая строка</div>
      </div>
      <div class="grid-2">
        <button id="clockStreamBtn" onclick="toggleClockStream()">▶ Включить трансляцию часов</button>
        <button class="sec" onclick="sendClockOnce()">Отправить 1 раз</button>
      </div>
    </div>

    <!-- КРИПТОВАЛЮТЫ И КУРСЫ -->
    <div class="card">
      <div class="card-title">
        <span>💰 Курсы криптовалют (CoinGecko Live)</span>
        <span id="cryptoStatus" style="font-size:11px; color:var(--text-dim);">Готово</span>
      </div>
      <div class="grid-4">
        <div class="btn-select active" id="cCoinBtc" onclick="toggleCryptoCoin('BTC')">BTC</div>
        <div class="btn-select active" id="cCoinEth" onclick="toggleCryptoCoin('ETH')">ETH</div>
        <div class="btn-select active" id="cCoinTon" onclick="toggleCryptoCoin('TON')">TON</div>
        <div class="btn-select" id="cCoinSol" onclick="toggleCryptoCoin('SOL')">SOL</div>
      </div>
      <div class="grid-2">
        <button id="cryptoStreamBtn" onclick="toggleCryptoStream()">▶ Транслировать курсы</button>
        <button class="sec" onclick="fetchAndSendCryptoOnce()">Обновить и отправить</button>
      </div>
    </div>

    <!-- ПОГОДА (OPEN-METEO) -->
    <div class="card">
      <div class="card-title">
        <span>⛅ Погода со смартфона</span>
        <span id="weatherPreview" style="color:var(--accent); font-weight:700;">-</span>
      </div>
      <div class="input-row">
        <input type="text" id="cityInput" placeholder="Город (например, Moscow, Almaty...)" value="Moscow">
        <button class="sec" onclick="fetchWeatherGps()">📍 GPS</button>
      </div>
      <div class="grid-2">
        <button id="weatherStreamBtn" onclick="toggleWeatherStream()">▶ Транслировать погоду</button>
        <button class="sec" onclick="fetchAndSendWeather()">Отправить 1 раз</button>
      </div>
    </div>

    <!-- ЦВЕТОМУЗЫКА С МИКРОФОНА -->
    <div class="card">
      <div class="card-title">
        <span>🎵 Цветомузыка с микрофона смартфона</span>
        <span id="audioStatus" style="font-size:11px; color:var(--accent);">FFT 20 полос</span>
      </div>
      <p style="font-size:12px; color:var(--text-dim);">Использует микрофон телефона как спектроанализатор для матрицы в такт музыке!</p>
      <div class="vu-meter-bar" id="vuMeter">
        <!-- 20 полос -->
      </div>
      <div class="slider-group">
        <div class="slider-head"><span>Чувствительность микрофона</span><span class="slider-val" id="micGainVal">1.5x</span></div>
        <input type="range" id="micGainSlider" min="5" max="40" value="15" oninput="updateMicGain(this.value)">
      </div>
      <button id="micStreamBtn" onclick="toggleMicStream()">🎙 Включить цветомузыку с микрофона</button>
    </div>
  </div>

  <!-- ТАБ 2: БЕГУЩАЯ СТРОКА И ДВИЖОК V2 -->
  <div id="tabText" class="tab-content">
    <div class="card">
      <div class="card-title">Текст строки</div>
      <div class="input-row">
        <input type="text" id="textInput" maxlength="120" placeholder="Введите текст...">
        <button onclick="applyTextBle()">Пуск</button>
      </div>
      <div class="card-title">Быстрые фразы</div>
      <div class="presets">
        <span class="preset-btn" onclick="setPresetDash()">--- Бесшовная линия</span>
        <span class="preset-btn" onclick="setPreset('>>> >>> >>>')">&gt;&gt;&gt; Стрелы</span>
        <span class="preset-btn" onclick="setPreset('ЖИЗНЬ ПРЕКРАСНА 🙂')">ЖИЗНЬ ПРЕКРАСНА 🙂</span>
        <span class="preset-btn" onclick="setPreset('МИР И ЛЮБОВЬ ❤️')">МИР И ЛЮБОВЬ ❤️</span>
        <span class="preset-btn" onclick="setPreset('$100 / ДЕНЬ')">$100 / ДЕНЬ</span>
        <span class="preset-btn" onclick="setPreset('STATUS: OK ✅')">STATUS: OK ✅</span>
        <span class="preset-btn" onclick="setPreset('НОЧЬ 🌙 ТИШИНА')">НОЧЬ 🌙 ТИШИНА</span>
      </div>
      <div class="card-title" style="margin-top:4px;">Спецсимволы (5x5)</div>
      <div class="presets">
        <span class="preset-btn" onclick="insertEmoji('🙂')">🙂 Улыбка</span>
        <span class="preset-btn" onclick="insertEmoji('❤️')">❤️ Сердце</span>
        <span class="preset-btn" onclick="insertEmoji('🌙')">🌙 Луна</span>
        <span class="preset-btn" onclick="insertEmoji('✅')">✅ Галочка</span>
        <span class="preset-btn" onclick="insertEmoji('❌')">❌ Крестик</span>
        <span class="preset-btn" onclick="insertEmoji('←')">←</span>
        <span class="preset-btn" onclick="insertEmoji('→')">→</span>
        <span class="preset-btn" onclick="insertEmoji('↑')">↑</span>
        <span class="preset-btn" onclick="insertEmoji('↓')">↓</span>
        <span class="preset-btn" onclick="insertEmoji('↔')">↔</span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Зацикливание</div>
      <div class="grid-2">
        <div class="btn-select active" id="loopModeSeamless" onclick="setSeamless(true)">🔄 Бесшовно (сразу)</div>
        <div class="btn-select" id="loopModeClassic" onclick="setSeamless(false)">⏹ С паузой (в край)</div>
      </div>
      <div class="slider-group" id="loopGapGroup">
        <div class="slider-head"><span>Промежуток между повторами</span><span class="slider-val" id="loopGapVal">3 пикс</span></div>
        <input type="range" id="loopGapSlider" min="0" max="20" value="3" oninput="updateLoopGap(this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Цвет текста (10 палитр)</div>
      <div class="grid-5">
        <div class="btn-select active" id="tm0" onclick="setTextMode(0)">🌈 Радуга</div>
        <div class="btn-select" id="tm1" onclick="setTextMode(1)">🎨 Моно</div>
        <div class="btn-select" id="tm2" onclick="setTextMode(2)">🌌 2X Град</div>
        <div class="btn-select" id="tm3" onclick="setTextMode(3)">🌅 3X Град</div>
        <div class="btn-select" id="tm4" onclick="setTextMode(4)">🔥 Огонь</div>
        <div class="btn-select" id="tm5" onclick="setTextMode(5)">⚡ Неон</div>
        <div class="btn-select" id="tm6" onclick="setTextMode(6)">🟢 Матрица</div>
        <div class="btn-select" id="tm7" onclick="setTextMode(7)">🔮 Плазма</div>
        <div class="btn-select" id="tm8" onclick="setTextMode(8)">✨ Звезды</div>
        <div class="btn-select" id="tm9" onclick="setTextMode(9)">🍬 Пастель</div>
      </div>
      <div id="textColorPickers" style="display:none; gap:10px; align-items:center; flex-wrap:wrap; margin-top:4px;">
        <span>Цвет 1: <input type="color" id="tc1" value="#00ffff" onchange="sendTextParamsBle()"></span>
        <span id="tc2Wrap" style="display:none;">Цвет 2: <input type="color" id="tc2" value="#ff0080" onchange="sendTextParamsBle()"></span>
        <span id="tc3Wrap" style="display:none;">Цвет 3: <input type="color" id="tc3" value="#ffe600" onchange="sendTextParamsBle()"></span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Стиль движения</div>
      <div class="grid-3">
        <div class="btn-select active" id="ts0" onclick="setTextStyle(0)">➡️ Скролл</div>
        <div class="btn-select" id="ts1" onclick="setTextStyle(1)">⌨️ Машинка</div>
        <div class="btn-select" id="ts2" onclick="setTextStyle(2)">🌊 Волна Y</div>
        <div class="btn-select" id="ts3" onclick="setTextStyle(3)">💓 Дыхание</div>
        <div class="btn-select" id="ts4" onclick="setTextStyle(4)">⚡ Глитч</div>
        <div class="btn-select" id="ts5" onclick="setTextStyle(5)">🎯 Центр</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Фоновый спецэффект</div>
      <div class="grid-4">
        <div class="btn-select active" id="tbg0" onclick="setTextBg(0)">⬛ Черный</div>
        <div class="btn-select" id="tbg1" onclick="setTextBg(1)">🌌 Звезды</div>
        <div class="btn-select" id="tbg2" onclick="setTextBg(2)">🔥 Угли</div>
        <div class="btn-select" id="tbg3" onclick="setTextBg(3)">🌀 Плазма</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Скорость текста</div>
      <div class="slider-group">
        <div class="slider-head"><span>Задержка шага</span><span class="slider-val" id="textSpeedVal">65 мс</span></div>
        <input type="range" id="textSpeed" min="15" max="200" value="65" oninput="updateTextSpeed(this.value)">
      </div>
      <div class="grid-2">
        <button class="sec" id="textPauseBtn" onclick="toggleTextPauseBle()">⏸ Пауза</button>
        <button onclick="saveTextBle()">💾 Сохранить в ESP32</button>
      </div>
    </div>
  </div>

  <!-- ТАБ 3: РИСОВАНИЕ 20x5 И АНИМАЦИИ (ХРАНЕНИЕ В ПАМЯТИ ТЕЛЕФОНА) -->
  <div id="tabDraw" class="tab-content">
    <div class="card">
      <div class="card-title">Шаблоны в редактор</div>
      <div class="presets">
        <span class="preset-btn" onclick="loadTemplate('nyan')">🐱 Нян-Кэт (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('pacman')">🟡 Пакман (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('heart')">💓 Сердце (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('invader')">👾 Пришелец (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('car')">🏎️ Спорткар (4 кадра)</span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">
        <span>Холст 20×5</span>
        <span id="curFrameLabel" style="color:var(--accent);">Кадр 1/1</span>
      </div>
      <div class="matrix-canvas-wrap" id="canvasWrap">
        <div class="pixel-matrix" id="pixelGrid"></div>
      </div>
      <div class="palette-bar">
        <div class="pal-color active" style="background:#ff0055" onclick="selectColor('#ff0055', this)"></div>
        <div class="pal-color" style="background:#ff5500" onclick="selectColor('#ff5500', this)"></div>
        <div class="pal-color" style="background:#ffcc00" onclick="selectColor('#ffcc00', this)"></div>
        <div class="pal-color" style="background:#00ff66" onclick="selectColor('#00ff66', this)"></div>
        <div class="pal-color" style="background:#00f0ff" onclick="selectColor('#00f0ff', this)"></div>
        <div class="pal-color" style="background:#0066ff" onclick="selectColor('#0066ff', this)"></div>
        <div class="pal-color" style="background:#aa00ff" onclick="selectColor('#aa00ff', this)"></div>
        <div class="pal-color" style="background:#ffffff" onclick="selectColor('#ffffff', this)"></div>
        <div class="pal-color" style="background:#000000; border:1px solid #444;" title="Ластик" onclick="selectColor('#000000', this)"></div>
        <input type="color" id="customPen" value="#ff0055" onchange="selectColor(this.value, null)">
      </div>
      <div class="grid-3">
        <button class="sec" onclick="fillCanvas()">🪣 Залить</button>
        <button class="sec" onclick="clearCanvas()">🗑️ Очистить</button>
        <button class="sec" onclick="duplicateFrame()">📋 Дубликат</button>
      </div>
    </div>

    <!-- Таймлайн кадров -->
    <div class="card">
      <div class="card-title">
        <span>Кадры (Память смартфона)</span>
        <button class="sec" style="padding:4px 10px; font-size:12px;" onclick="addFrame()">+ Новый кадр</button>
      </div>
      <div class="timeline" id="timelineBar"></div>

      <div class="slider-group">
        <div class="slider-head"><span>Скорость анимации</span><span class="slider-val" id="fpsVal">6 FPS</span></div>
        <input type="range" id="fpsSlider" min="1" max="25" value="6" oninput="updateFps(this.value)">
      </div>

      <div class="grid-2">
        <button id="streamAnimBtn" onclick="toggleStreamCustomAnim()">▶ Запустить на матрице</button>
        <button class="danger" onclick="deleteFrame()">Удалить кадр</button>
      </div>

      <div class="grid-2">
        <button class="sec" onclick="saveProjectToPhone()">💾 Сохранить в телефон</button>
        <button class="sec" onclick="openSavedProjects()">📁 Проекты в телефоне</button>
      </div>
      <div class="grid-2">
        <button class="sec" onclick="exportAnimJson()">📥 Экспорт JSON</button>
        <button class="sec" onclick="importAnimJson()">📤 Импорт JSON</button>
      </div>
    </div>
  </div>

  <!-- ТАБ 4: ВСТРОЕННЫЕ ЭФФЕКТЫ -->
  <div id="tabEffects" class="tab-content">
    <div class="card">
      <div class="card-title">Скорость эффектов</div>
      <div class="slider-group">
        <div class="slider-head"><span>Скорость анимации</span><span class="slider-val" id="effSpeedVal">55 мс</span></div>
        <input type="range" id="effSpeedSlider" min="15" max="200" value="55" oninput="updateEffectSpeed(this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Галерея эффектов</div>

      <div class="effect-card" id="eff0">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🐱 Нян-Кэт (Nyan Cat)</h4>
            <p>Радужный шлейф волнами и летящий кот</p>
          </div>
          <span style="font-size:24px;">🌈</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(0)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('nyan')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff4">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🟡 Пакман (Pac-Man)</h4>
            <p>Пакман поедает точки, убегая от призрака</p>
          </div>
          <span style="font-size:24px;">👻</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(4)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('pacman')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff5">
        <div class="eff-head">
          <div class="eff-info">
            <h4>💓 Сердцебиение (Heartbeat)</h4>
            <p>Пульс пиксельного сердца и линия ЭКГ</p>
          </div>
          <span style="font-size:24px;">❤️</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(5)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('heart')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff1">
        <div class="eff-head">
          <div class="eff-info">
            <h4>💻 Матрица (Matrix Rain)</h4>
            <p>Цифровой зеленый дождь с белыми каплями</p>
          </div>
          <span style="font-size:24px;">🟩</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(1)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff2">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🔥 Огонь (Fire Flame)</h4>
            <p>Плавная физика языков пламени</p>
          </div>
          <span style="font-size:24px;">🔥</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(2)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff3">
        <div class="eff-head">
          <div class="eff-info">
            <h4>📊 Эквалайзер (VU Meter)</h4>
            <p>20 скачущих частотных полос</p>
          </div>
          <span style="font-size:24px;">🎵</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(3)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff6">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🌀 Плазма (Plasma)</h4>
            <p>Психоделические 2D волны цвета</p>
          </div>
          <span style="font-size:24px;">🔮</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffectBle(6)">▶ Запустить</button>
        </div>
      </div>
    </div>
  </div>

  <!-- ТАБ 5: НАСТРОЙКИ И ЯРКОСТЬ -->
  <div id="tabSettings" class="tab-content">
    <div class="card">
      <div class="card-title">Глобальная яркость</div>
      <div class="slider-group">
        <div class="slider-head"><span>Яркость всех диодов</span><span class="slider-val" id="globalBrightVal">40</span></div>
        <input type="range" id="globalBright" min="5" max="255" value="40" oninput="updateBrightBle(this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Безопасность BLE</div>
      <p style="font-size:12px; color:var(--text-dim);">Изменение PIN-кода устройства (сохраняется во Flash памяти ESP32):</p>
      <div class="input-row">
        <input type="password" id="newPassInput" placeholder="Новый PIN...">
        <button onclick="changeBlePassword()">Сменить</button>
      </div>
    </div>

    <div class="card">
      <div class="card-title">О системе</div>
      <p style="font-size:13px; line-height:1.6; color:var(--text-dim);">
        • Связь: <b>Bluetooth Low Energy (Nordic UART)</b><br>
        • Матрица: <b>20×5 (100 WS2812B LEDs, Pin 8)</b><br>
        • Хранилище пресетов: <b>Память браузера смартфона (LocalStorage)</b><br>
        • Web Bluetooth: <b>Chrome (Android/PC) / Bluefy (iOS)</b>
      </p>
    </div>
  </div>
</div>

<!-- Модальное окно PIN-кода -->
<div class="modal-overlay" id="pinModal">
  <div class="modal-box">
    <div class="card-title">Авторизация по PIN</div>
    <p style="font-size:13px; color:var(--text-dim);">Введите PIN-код для управления матрицей по Bluetooth (по умолчанию: 1234):</p>
    <input type="password" id="modalPinInput" value="1234" maxlength="20">
    <div class="grid-2">
      <button class="sec" onclick="closePinModal()">Отмена</button>
      <button onclick="savePinFromModal()">Сохранить и подключиться</button>
    </div>
  </div>
</div>

<!-- Модальное окно сохраненных проектов на телефоне -->
<div class="modal-overlay" id="projectsModal">
  <div class="modal-box">
    <div class="card-title">Проекты в памяти телефона</div>
    <div id="projectsList" style="display:flex; flex-direction:column; gap:8px; max-height:240px; overflow-y:auto;"></div>
    <button class="sec" onclick="closeProjectsModal()">Закрыть</button>
  </div>
</div>

<div id="toast">Готово</div>

<script>
  // ==========================================
  // --- КОНФИГУРАЦИЯ BLE NORDIC UART ---
  // ==========================================
  const BLE_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  const BLE_CHAR_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // write to ESP32
  const BLE_CHAR_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // notify from ESP32

  let bleDevice = null;
  let bleServer = null;
  let bleRxChar = null;
  let bleTxChar = null;
  let isConnected = false;
  let blePin = localStorage.getItem('esp32_matrix_pin') || '1234';

  function showToast(msg) {
    const t = document.getElementById('toast');
    t.innerText = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 2200);
  }

  function setBleStatus(state, msg) {
    const dot = document.getElementById('bleStatusDot');
    const txt = document.getElementById('bleStatusText');
    const btn = document.getElementById('bleConnectBtn');
    const syncBtn = document.getElementById('syncStatusBtn');

    dot.className = 'ble-dot ' + state;
    txt.innerText = msg;

    if (state === 'connected') {
      isConnected = true;
      btn.innerText = '🔌 Отключить BLE';
      btn.classList.add('danger');
      syncBtn.disabled = false;
    } else {
      isConnected = false;
      btn.innerText = '⚡ Подключить Bluetooth';
      btn.classList.remove('danger');
      syncBtn.disabled = true;
    }
  }

  async function toggleBleConnection() {
    if (isConnected && bleDevice) {
      if (bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
      }
      return;
    }

    if (!navigator.bluetooth) {
      if (location.protocol !== 'https:' && location.hostname !== 'localhost') {
        alert("⚠️ Браузер Chrome на телефоне требует HTTPS для Bluetooth!\n\n1. Откройте приложение через HTTPS-ссылку.\n2. Либо в Chrome на телефоне откройте:\nchrome://flags/#unsafely-treat-insecure-origin-as-secure\nи добавьте туда " + location.origin + "\n3. Либо используйте бесплатное приложение 'Serial Bluetooth Terminal' (Android)!");
      } else {
        alert("Ваш браузер не поддерживает Web Bluetooth API.\n\n• На Android: используйте Google Chrome\n• На iPhone/iPad: установите бесплатный браузер Bluefy или WebBLE\n• На ПК: Google Chrome, Opera или Edge");
      }
      return;
    }

    try {
      setBleStatus('connecting', 'BLE: Поиск устройства...');
      
      let dev = null;
      try {
        dev = await navigator.bluetooth.requestDevice({
          filters: [
            { namePrefix: 'ESP32-Matrix' },
            { namePrefix: 'ESP32' }
          ],
          optionalServices: [BLE_SERVICE_UUID]
        });
      } catch (e1) {
        // Если пользователь не нашел в фильтре или отменил - предлагаем полный поиск всех устройств
        console.warn("Retrying with acceptAllDevices...", e1);
        dev = await navigator.bluetooth.requestDevice({
          acceptAllDevices: true,
          optionalServices: [BLE_SERVICE_UUID]
        });
      }

      bleDevice = dev;
      bleDevice.addEventListener('gattserverdisconnected', onBleDisconnected);

      setBleStatus('connecting', 'BLE: Подключение GATT...');
      bleServer = await bleDevice.gatt.connect();

      const service = await bleServer.getPrimaryService(BLE_SERVICE_UUID);
      bleRxChar = await service.getCharacteristic(BLE_CHAR_RX_UUID);
      bleTxChar = await service.getCharacteristic(BLE_CHAR_TX_UUID);

      await bleTxChar.startNotifications();
      bleTxChar.addEventListener('characteristicvaluechanged', handleBleNotification);

      // Отправляем AUTH пароль
      setBleStatus('connecting', 'BLE: Авторизация...');
      await sendBleRaw("AUTH:" + blePin + "\n");

    } catch (err) {
      console.error(err);
      setBleStatus('', 'BLE: Ошибка подключения');
      showToast("Подключение отменено");
    }
  }

  let bleKeepAliveTimer = null;

  function startBleKeepAlive() {
    stopBleKeepAlive();
    bleKeepAliveTimer = setInterval(async () => {
      if (isConnected && bleRxChar) {
        try {
          await sendBleRaw("PING\n");
        } catch (e) {
          console.warn("BLE ping error:", e);
        }
      }
    }, 4000);
  }

  function stopBleKeepAlive() {
    if (bleKeepAliveTimer) {
      clearInterval(bleKeepAliveTimer);
      bleKeepAliveTimer = null;
    }
  }

  function onBleDisconnected() {
    stopBleKeepAlive();
    setBleStatus('', 'BLE: Отключено');
    showToast("Bluetooth отключен");
    stopAllStreams();
  }

  function handleBleNotification(event) {
    const val = new TextDecoder().decode(event.target.value);
    console.log("BLE Notify:", val);

    if (val.includes("PONG") || val.includes("OK_BEAT")) {
      return;
    }

    if (val.includes("AUTH_OK")) {
      setBleStatus('connected', 'BLE: Подключено (ESP32)');
      showToast("🟢 Авторизовано по Bluetooth!");
      startBleKeepAlive();
      requestStatus();
    } else if (val.includes("AUTH_FAIL") || val.includes("ERR_AUTH_REQUIRED")) {
      setBleStatus('', 'BLE: Неверный PIN!');
      showToast("❌ Неверный PIN-код!");
      openPinModal();
    } else if (val.startsWith("STATUS:")) {
      try {
        const json = JSON.parse(val.substring(7));
        applyEspStatus(json);
      } catch(e) {}
    } else if (val.startsWith("OK_PASS")) {
      showToast("🔑 PIN-код успешно изменен!");
    }
  }

  async function sendBleRaw(str) {
    if (!bleRxChar) return;
    try {
      const data = new TextEncoder().encode(str);
      const CHUNK_SIZE = 20; // Безопасный размер для BLE ATT MTU 23
      for (let i = 0; i < data.length; i += CHUNK_SIZE) {
        const chunk = data.slice(i, i + CHUNK_SIZE);
        if (bleRxChar.writeValueWithoutResponse) {
          await bleRxChar.writeValueWithoutResponse(chunk);
        } else {
          await bleRxChar.writeValue(chunk);
        }
      }
    } catch (e) {
      console.error("BLE Write error:", e);
    }
  }

  function requestStatus() {
    sendBleRaw("STATUS?\n");
  }

  function applyEspStatus(data) {
    if (data.text) document.getElementById('textInput').value = data.text;
    if (data.bright !== undefined) {
      document.getElementById('globalBright').value = data.bright;
      document.getElementById('globalBrightVal').innerText = data.bright;
    }
    if (data.speed) {
      document.getElementById('textSpeed').value = data.speed;
      document.getElementById('textSpeedVal').innerText = data.speed + ' мс';
    }
    if (data.mode !== undefined) setTextMode(data.mode);
    if (data.style !== undefined) setTextStyle(data.style);
    if (data.bg !== undefined) setTextBg(data.bg);
    if (data.c1) document.getElementById('tc1').value = '#' + data.c1;
    if (data.c2) document.getElementById('tc2').value = '#' + data.c2;
    if (data.c3) document.getElementById('tc3').value = '#' + data.c3;
    if (data.seamless !== undefined) setSeamless(data.seamless == 1);
    if (data.gap !== undefined) {
      loopGap = data.gap;
      document.getElementById('loopGapSlider').value = loopGap;
      document.getElementById('loopGapVal').innerText = loopGap + ' пикс' + (loopGap == 0 ? ' (встык)' : '');
    }
    showToast("Статус матрицы обновлен");
  }

  // PIN модалка
  function openPinModal() {
    document.getElementById('modalPinInput').value = blePin;
    document.getElementById('pinModal').classList.add('open');
  }
  function closePinModal() {
    document.getElementById('pinModal').classList.remove('open');
  }
  function savePinFromModal() {
    blePin = document.getElementById('modalPinInput').value.trim();
    localStorage.setItem('esp32_matrix_pin', blePin);
    closePinModal();
    if (isConnected) {
      sendBleRaw("AUTH:" + blePin + "\n");
    } else {
      toggleBleConnection();
    }
  }
  function changeBlePassword() {
    const np = document.getElementById('newPassInput').value.trim();
    if (!np) return showToast("Введите новый пароль");
    blePin = np;
    localStorage.setItem('esp32_matrix_pin', blePin);
    sendBleRaw("SETPASS:" + np + "\n");
    document.getElementById('newPassInput').value = "";
  }

  // Навигация
  function switchTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    document.getElementById('btn-' + tabId).classList.add('active');
  }

  // ==========================================
  // --- ТАБ 1: СМАРТ-ВИДЖЕТЫ ---
  // ==========================================
  let clockStreamTimer = null;
  let clockStyle = 5; // static center

  function setClockFormat(style) {
    clockStyle = style;
    document.getElementById('clockModeCenter').classList.toggle('active', style === 5);
    document.getElementById('clockModeScroll').classList.toggle('active', style === 0);
  }

  function getFormattedTime() {
    const now = new Date();
    const hh = String(now.getHours()).padStart(2, '0');
    const mm = String(now.getMinutes()).padStart(2, '0');
    return `${hh}:${mm}`;
  }

  function sendClockOnce() {
    const timeStr = getFormattedTime();
    document.getElementById('clockPreview').innerText = timeStr;
    sendBleRaw(`TP:${textState.mode},${getColorHex('tc1')},${getColorHex('tc2')},${getColorHex('tc3')},${textState.speed},${clockStyle},${currentBg},1,3\n`);
    sendBleRaw(`T:${timeStr}\n`);
    showToast("Время отправлено: " + timeStr);
  }

  function toggleClockStream() {
    if (clockStreamTimer) {
      clearInterval(clockStreamTimer);
      clockStreamTimer = null;
      document.getElementById('clockStreamBtn').innerText = "▶ Включить трансляцию часов";
      document.getElementById('clockStreamBtn').classList.remove('danger');
      showToast("Трансляция часов остановлена");
    } else {
      stopAllStreams();
      sendClockOnce();
      clockStreamTimer = setInterval(sendClockOnce, 20000); // каждые 20 сек
      document.getElementById('clockStreamBtn').innerText = "⏹ Остановить трансляцию";
      document.getElementById('clockStreamBtn').classList.add('danger');
      showToast("Трансляция часов запущена!");
    }
  }

  // КРИПТОВАЛЮТЫ
  let activeCoins = { BTC: true, ETH: true, TON: true, SOL: false };
  let cryptoTimer = null;

  function toggleCryptoCoin(c) {
    activeCoins[c] = !activeCoins[c];
    document.getElementById('cCoin' + c.charAt(0) + c.slice(1).toLowerCase()).classList.toggle('active', activeCoins[c]);
  }

  async function fetchCryptoString() {
    try {
      document.getElementById('cryptoStatus').innerText = "Запрос курсов...";
      const res = await fetch('https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,solana,the-open-network&vs_currencies=usd');
      const data = await res.json();
      let parts = [];
      if (activeCoins.BTC && data.bitcoin) parts.push(`BTC $${data.bitcoin.usd.toLocaleString()}`);
      if (activeCoins.ETH && data.ethereum) parts.push(`ETH $${data.ethereum.usd.toLocaleString()}`);
      if (activeCoins.TON && data['the-open-network']) parts.push(`TON $${data['the-open-network'].usd}`);
      if (activeCoins.SOL && data.solana) parts.push(`SOL $${data.solana.usd}`);
      document.getElementById('cryptoStatus').innerText = "Курсы получены";
      return parts.join('   ') + "   ";
    } catch(e) {
      document.getElementById('cryptoStatus').innerText = "Ошибка API";
      return null;
    }
  }

  async function fetchAndSendCryptoOnce() {
    const str = await fetchCryptoString();
    if (str) {
      sendBleRaw(`TP:2,${getColorHex('tc1')},${getColorHex('tc2')},${getColorHex('tc3')},60,0,1,1,4\n`);
      sendBleRaw(`T:${str}\n`);
      showToast("Курсы отправлены на матрицу");
    }
  }

  function toggleCryptoStream() {
    if (cryptoTimer) {
      clearInterval(cryptoTimer);
      cryptoTimer = null;
      document.getElementById('cryptoStreamBtn').innerText = "▶ Транслировать курсы";
      document.getElementById('cryptoStreamBtn').classList.remove('danger');
    } else {
      stopAllStreams();
      fetchAndSendCryptoOnce();
      cryptoTimer = setInterval(fetchAndSendCryptoOnce, 60000); // каждую минуту
      document.getElementById('cryptoStreamBtn').innerText = "⏹ Остановить курсы";
      document.getElementById('cryptoStreamBtn').classList.add('danger');
      showToast("Автообновление курсов запущено!");
    }
  }

  // ПОГОДА
  let weatherTimer = null;
  async function fetchWeatherString(city) {
    try {
      // Геокодинг города через Open-Meteo
      const geoRes = await fetch(`https://geocoding-api.open-meteo.com/v1/search?name=${encodeURIComponent(city)}&count=1&language=ru`);
      const geoData = await geoRes.json();
      if (!geoData.results || geoData.results.length === 0) return null;
      const lat = geoData.results[0].latitude;
      const lon = geoData.results[0].longitude;

      const wRes = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current_weather=true`);
      const wData = await wRes.json();
      const temp = Math.round(wData.current_weather.temperature);
      const wind = Math.round(wData.current_weather.windspeed);
      const sign = temp > 0 ? '+' : '';
      const str = `${city.toUpperCase()} ${sign}${temp}°C ВЕТЕР ${wind}KM/H   `;
      document.getElementById('weatherPreview').innerText = `${sign}${temp}°C (${city})`;
      return str;
    } catch(e) {
      return null;
    }
  }

  async function fetchAndSendWeather() {
    const city = document.getElementById('cityInput').value.trim() || "Moscow";
    const str = await fetchWeatherString(city);
    if (str) {
      sendBleRaw(`TP:0,00f0ff,ff0080,ffe600,60,0,0,1,3\n`);
      sendBleRaw(`T:${str}\n`);
      showToast("Погода отправлена!");
    } else {
      showToast("Не удалось получить погоду");
    }
  }

  function fetchWeatherGps() {
    if (!navigator.geolocation) return showToast("GPS недоступен");
    navigator.geolocation.getCurrentPosition(async (pos) => {
      try {
        const lat = pos.coords.latitude;
        const lon = pos.coords.longitude;
        const wRes = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current_weather=true`);
        const wData = await wRes.json();
        const temp = Math.round(wData.current_weather.temperature);
        const sign = temp > 0 ? '+' : '';
        const str = `ПОГОДА ${sign}${temp}°C   `;
        document.getElementById('weatherPreview').innerText = `${sign}${temp}°C`;
        sendBleRaw(`TP:0,00f0ff,ff0080,ffe600,60,0,0,1,3\n`);
        sendBleRaw(`T:${str}\n`);
        showToast("Погода по GPS отправлена!");
      } catch(e) {
        showToast("Ошибка GPS погоды");
      }
    });
  }

  function toggleWeatherStream() {
    if (weatherTimer) {
      clearInterval(weatherTimer);
      weatherTimer = null;
      document.getElementById('weatherStreamBtn').innerText = "▶ Транслировать погоду";
      document.getElementById('weatherStreamBtn').classList.remove('danger');
    } else {
      stopAllStreams();
      fetchAndSendWeather();
      weatherTimer = setInterval(fetchAndSendWeather, 180000); // каждые 3 мин
      document.getElementById('weatherStreamBtn').innerText = "⏹ Остановить погоду";
      document.getElementById('weatherStreamBtn').classList.add('danger');
    }
  }

  // ЦВЕТОМУЗЫКА С МИКРОФОНА ТЕЛЕФОНА (WEB AUDIO FFT)
  let audioCtx = null;
  let audioAnalyser = null;
  let audioStream = null;
  let micTimer = null;
  let micGain = 1.5;

  const vuMeter = document.getElementById('vuMeter');
  for (let i = 0; i < 20; i++) {
    const col = document.createElement('div');
    col.className = 'vu-col';
    vuMeter.appendChild(col);
  }

  function updateMicGain(v) {
    micGain = v / 10;
    document.getElementById('micGainVal').innerText = micGain.toFixed(1) + 'x';
  }

  async function toggleMicStream() {
    if (micTimer) {
      stopMicStream();
      return;
    }
    stopAllStreams();

    try {
      audioStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      const src = audioCtx.createMediaStreamSource(audioStream);
      audioAnalyser = audioCtx.createAnalyser();
      audioAnalyser.fftSize = 64;
      src.connect(audioAnalyser);

      document.getElementById('micStreamBtn').innerText = "⏹ Выключить цветомузыку";
      document.getElementById('micStreamBtn').classList.add('danger');
      showToast("Микрофон подключен! Играйте музыку");

      const bufferLength = audioAnalyser.frequencyBinCount;
      const dataArray = new Uint8Array(bufferLength);

      let lastSend = 0;
      micTimer = setInterval(() => {
        audioAnalyser.getByteFrequencyData(dataArray);

        // Масштабируем 20 полос
        let colHeights = [];
        for (let i = 0; i < 20; i++) {
          let binIdx = Math.min(Math.floor((i / 20) * (bufferLength * 0.8)), bufferLength - 1);
          let val = Math.min(5, Math.floor((dataArray[binIdx] / 255) * 5 * micGain));
          colHeights.push(val);
          vuMeter.children[i].style.height = (val * 20) + '%';
        }

        // Шлем RAW фрейм каждые 65 мс
        const now = Date.now();
        if (now - lastSend >= 65 && isConnected) {
          lastSend = now;
          let hexFrame = "";
          const vuColors = ["000000", "00ff66", "00f0ff", "ffea00", "ff5500", "ff0055"];
          for (let y = 0; y < 5; y++) {
            for (let x = 0; x < 20; x++) {
              let h = colHeights[x];
              // снизу вверх: y=4 это низ, y=0 это верх
              let rowFromBottom = 4 - y;
              let col = (rowFromBottom < h) ? vuColors[rowFromBottom + 1] : "000000";
              hexFrame += col;
            }
          }
          sendBleRaw("RAW:" + hexFrame + "\n");
        }
      }, 50);

    } catch (e) {
      console.error(e);
      showToast("Ошибка доступа к микрофону");
    }
  }

  function stopMicStream() {
    if (micTimer) clearInterval(micTimer);
    micTimer = null;
    if (audioStream) {
      audioStream.getTracks().forEach(t => t.stop());
      audioStream = null;
    }
    if (audioCtx) {
      audioCtx.close();
      audioCtx = null;
    }
    for (let c of vuMeter.children) c.style.height = '4px';
    document.getElementById('micStreamBtn').innerText = "🎙 Включить цветомузыку с микрофона";
    document.getElementById('micStreamBtn').classList.remove('danger');
    showToast("Цветомузыка выключена");
  }

  function stopAllStreams() {
    if (clockStreamTimer) { clearInterval(clockStreamTimer); clockStreamTimer = null; }
    if (cryptoTimer) { clearInterval(cryptoTimer); cryptoTimer = null; }
    if (weatherTimer) { clearInterval(weatherTimer); weatherTimer = null; }
    if (micTimer) stopMicStream();
    if (animStreamTimer) stopStreamCustomAnim();
    document.getElementById('clockStreamBtn').innerText = "▶ Включить трансляцию часов";
    document.getElementById('clockStreamBtn').classList.remove('danger');
    document.getElementById('cryptoStreamBtn').innerText = "▶ Транслировать курсы";
    document.getElementById('cryptoStreamBtn').classList.remove('danger');
    document.getElementById('weatherStreamBtn').innerText = "▶ Транслировать погоду";
    document.getElementById('weatherStreamBtn').classList.remove('danger');
  }

  // ==========================================
  // --- ТАБ 2: БЕГУЩАЯ СТРОКА BLE ---
  // ==========================================
  let textState = { mode: 0, speed: 65, running: true };
  let currentStyle = 0;
  let currentBg = 0;
  let seamlessLoop = true;
  let loopGap = 3;

  function getColorHex(id) {
    const v = document.getElementById(id).value;
    return v.startsWith('#') ? v.substring(1) : v;
  }

  function applyTextBle() {
    let txt = document.getElementById('textInput').value.trim();
    if (!txt) txt = " ";
    sendBleRaw(`TP:${textState.mode},${getColorHex('tc1')},${getColorHex('tc2')},${getColorHex('tc3')},${textState.speed},${currentStyle},${currentBg},${seamlessLoop ? 1 : 0},${loopGap}\n`);
    sendBleRaw(`T:${txt}\n`);
    showToast("Текст отправлен!");
  }

  function setPreset(txt) {
    document.getElementById('textInput').value = txt;
    applyTextBle();
  }

  function setPresetDash() {
    document.getElementById('textInput').value = '-';
    setSeamless(true);
    updateLoopGap(0);
    applyTextBle();
  }

  function insertEmoji(em) {
    const inp = document.getElementById('textInput');
    inp.value += em;
    inp.focus();
  }

  function setSeamless(val) {
    seamlessLoop = val;
    document.getElementById('loopModeSeamless').classList.toggle('active', seamlessLoop);
    document.getElementById('loopModeClassic').classList.toggle('active', !seamlessLoop);
    document.getElementById('loopGapGroup').style.display = seamlessLoop ? 'flex' : 'none';
    sendTextParamsBle();
  }

  function updateLoopGap(v) {
    loopGap = parseInt(v);
    document.getElementById('loopGapSlider').value = loopGap;
    document.getElementById('loopGapVal').innerText = v + ' пикс' + (v == 0 ? ' (встык)' : '');
    sendTextParamsBle();
  }

  function setTextMode(m) {
    textState.mode = m;
    for (let i = 0; i < 10; i++) {
      const el = document.getElementById('tm' + i);
      if (el) el.classList.toggle('active', i === m);
    }
    const cp = document.getElementById('textColorPickers');
    const c2 = document.getElementById('tc2Wrap');
    const c3 = document.getElementById('tc3Wrap');
    if (m === 1) { cp.style.display = 'flex'; c2.style.display = 'none'; c3.style.display = 'none'; }
    else if (m === 2) { cp.style.display = 'flex'; c2.style.display = 'inline'; c3.style.display = 'none'; }
    else if (m === 3) { cp.style.display = 'flex'; c2.style.display = 'inline'; c3.style.display = 'inline'; }
    else if (m === 8) { cp.style.display = 'flex'; c2.style.display = 'none'; c3.style.display = 'none'; }
    else { cp.style.display = 'none'; }
    sendTextParamsBle();
  }

  function setTextStyle(s) {
    currentStyle = s;
    for (let i = 0; i < 6; i++) {
      const el = document.getElementById('ts' + i);
      if (el) el.classList.toggle('active', i === s);
    }
    sendTextParamsBle();
  }

  function setTextBg(b) {
    currentBg = b;
    for (let i = 0; i < 4; i++) {
      const el = document.getElementById('tbg' + i);
      if (el) el.classList.toggle('active', i === b);
    }
    sendTextParamsBle();
  }

  function sendTextParamsBle() {
    sendBleRaw(`TP:${textState.mode},${getColorHex('tc1')},${getColorHex('tc2')},${getColorHex('tc3')},${textState.speed},${currentStyle},${currentBg},${seamlessLoop ? 1 : 0},${loopGap}\n`);
  }

  function updateTextSpeed(v) {
    textState.speed = parseInt(v);
    document.getElementById('textSpeedVal').innerText = v + ' мс';
    sendTextParamsBle();
  }

  function toggleTextPauseBle() {
    textState.running = !textState.running;
    document.getElementById('textPauseBtn').innerText = textState.running ? "⏸ Пауза" : "▶ Пуск";
    sendBleRaw(`PAUSE:${textState.running ? 1 : 0}\n`);
  }

  function saveTextBle() {
    // Вызывает сохранение во Flash на ESP32
    applyTextBle();
    showToast("💾 Настройки отправлены в ESP32");
  }

  // ==========================================
  // --- ТАБ 3: РИСОВАНИЕ 20x5 И АНИМАЦИИ ---
  // ==========================================
  let activeColor = '#ff0055';
  let isDrawing = false;
  let curFrameIdx = 0;
  let animFrames = [ Array(100).fill('#000000') ];
  let fps = 6;
  let animStreamTimer = null;

  const grid = document.getElementById('pixelGrid');
  for (let i = 0; i < 100; i++) {
    const p = document.createElement('div');
    p.className = 'led-pixel';
    p.dataset.idx = i;
    p.addEventListener('pointerdown', (e) => {
      isDrawing = true;
      paintPixel(i);
      e.preventDefault();
    });
    p.addEventListener('pointerenter', () => {
      if (isDrawing) paintPixel(i);
    });
    grid.appendChild(p);
  }

  window.addEventListener('pointerup', () => { isDrawing = false; });
  window.addEventListener('pointercancel', () => { isDrawing = false; });

  function selectColor(c, el) {
    activeColor = c;
    document.querySelectorAll('.pal-color').forEach(p => p.classList.remove('active'));
    if (el) el.classList.add('active');
    document.getElementById('customPen').value = c;
  }

  function paintPixel(idx) {
    animFrames[curFrameIdx][idx] = activeColor;
    updatePixelDOM(idx, activeColor);
    let x = idx % 20;
    let y = Math.floor(idx / 20);
    let hex = activeColor.startsWith('#') ? activeColor.substring(1) : activeColor;
    if (isConnected) {
      sendBleRaw(`LP:${x},${y},${hex}\n`);
    }
  }

  function updatePixelDOM(idx, color) {
    const p = grid.children[idx];
    if (color === '#000000' || color === '#000') {
      p.style.background = 'var(--led-off)';
      p.style.boxShadow = 'none';
    } else {
      p.style.background = color;
      p.style.boxShadow = '0 0 6px ' + color;
    }
  }

  function renderCurrentFrame() {
    const frame = animFrames[curFrameIdx];
    for (let i = 0; i < 100; i++) {
      updatePixelDOM(i, frame[i]);
    }
    document.getElementById('curFrameLabel').innerText = `Кадр ${curFrameIdx + 1}/${animFrames.length}`;
    renderTimeline();
  }

  function fillCanvas() {
    animFrames[curFrameIdx].fill(activeColor);
    renderCurrentFrame();
    sendLiveFrameBle();
  }

  function clearCanvas() {
    animFrames[curFrameIdx].fill('#000000');
    renderCurrentFrame();
    sendLiveFrameBle();
  }

  function addFrame() {
    animFrames.push(Array(100).fill('#000000'));
    curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrameBle();
    showToast("Добавлен кадр " + (curFrameIdx + 1));
  }

  function duplicateFrame() {
    animFrames.push([...animFrames[curFrameIdx]]);
    curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrameBle();
    showToast("Кадр продублирован");
  }

  function deleteFrame() {
    if (animFrames.length <= 1) {
      clearCanvas();
      return;
    }
    animFrames.splice(curFrameIdx, 1);
    if (curFrameIdx >= animFrames.length) curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrameBle();
    showToast("Кадр удален");
  }

  function renderTimeline() {
    const bar = document.getElementById('timelineBar');
    bar.innerHTML = '';
    animFrames.forEach((_, idx) => {
      const btn = document.createElement('div');
      btn.className = 'frame-thumb' + (idx === curFrameIdx ? ' active' : '');
      btn.innerText = '#' + (idx + 1);
      btn.onclick = () => {
        curFrameIdx = idx;
        renderCurrentFrame();
        sendLiveFrameBle();
      };
      bar.appendChild(btn);
    });
  }

  function updateFps(v) {
    fps = parseInt(v);
    document.getElementById('fpsVal').innerText = v + ' FPS';
    if (animStreamTimer) {
      stopStreamCustomAnim();
      toggleStreamCustomAnim();
    }
  }

  function sendLiveFrameBle() {
    if (!isConnected) return;
    const frame = animFrames[curFrameIdx];
    let hex = '';
    for (let c of frame) {
      hex += (c.startsWith('#') ? c.substring(1) : c).padStart(6, '0');
    }
    sendBleRaw("RAW:" + hex + "\n");
  }

  function toggleStreamCustomAnim() {
    if (animStreamTimer) {
      stopStreamCustomAnim();
      return;
    }
    stopAllStreams();
    let streamIdx = 0;
    const interval = Math.round(1000 / fps);
    animStreamTimer = setInterval(() => {
      const frame = animFrames[streamIdx];
      let hex = '';
      for (let c of frame) {
        hex += (c.startsWith('#') ? c.substring(1) : c).padStart(6, '0');
      }
      sendBleRaw("RAW:" + hex + "\n");
      curFrameIdx = streamIdx;
      renderCurrentFrame();
      streamIdx = (streamIdx + 1) % animFrames.length;
    }, interval);

    document.getElementById('streamAnimBtn').innerText = "⏹ Остановить показ";
    document.getElementById('streamAnimBtn').classList.add('danger');
    showToast("Анимация транслируется на матрицу!");
  }

  function stopStreamCustomAnim() {
    if (animStreamTimer) clearInterval(animStreamTimer);
    animStreamTimer = null;
    document.getElementById('streamAnimBtn').innerText = "▶ Запустить на матрице";
    document.getElementById('streamAnimBtn').classList.remove('danger');
  }

  // СОХРАНЕНИЕ ПРОЕКТОВ В ПАМЯТЬ ТЕЛЕФОНА
  function saveProjectToPhone() {
    const name = prompt("Название анимации:", "Проект " + new Date().toLocaleTimeString());
    if (!name) return;
    let list = JSON.parse(localStorage.getItem('esp32_saved_projects') || '[]');
    list.unshift({ name, fps, frames: animFrames, date: Date.now() });
    localStorage.setItem('esp32_saved_projects', JSON.stringify(list));
    showToast("💾 Сохранено в память телефона!");
  }

  function openSavedProjects() {
    const list = JSON.parse(localStorage.getItem('esp32_saved_projects') || '[]');
    const container = document.getElementById('projectsList');
    container.innerHTML = '';
    if (list.length === 0) {
      container.innerHTML = '<div style="color:var(--text-dim); text-align:center; padding:10px;">Нет сохраненных проектов</div>';
    } else {
      list.forEach((p, idx) => {
        const row = document.createElement('div');
        row.style.cssText = 'display:flex; justify-content:space-between; align-items:center; background:#0c1322; padding:8px 12px; border-radius:10px; border:1px solid var(--border);';
        row.innerHTML = `
          <div>
            <div style="font-weight:700; font-size:13px;">${p.name}</div>
            <div style="font-size:11px; color:var(--text-dim);">${p.frames.length} кадров • ${p.fps || 6} FPS</div>
          </div>
          <div style="display:flex; gap:6px;">
            <button class="sec" style="padding:6px 10px; font-size:11px;" onclick="loadProject(${idx})">Открыть</button>
            <button class="danger" style="padding:6px 10px; font-size:11px;" onclick="deleteProject(${idx})">✕</button>
          </div>
        `;
        container.appendChild(row);
      });
    }
    document.getElementById('projectsModal').classList.add('open');
  }

  function closeProjectsModal() {
    document.getElementById('projectsModal').classList.remove('open');
  }

  function loadProject(idx) {
    const list = JSON.parse(localStorage.getItem('esp32_saved_projects') || '[]');
    if (list[idx]) {
      animFrames = list[idx].frames;
      fps = list[idx].fps || 6;
      document.getElementById('fpsSlider').value = fps;
      document.getElementById('fpsVal').innerText = fps + ' FPS';
      curFrameIdx = 0;
      renderCurrentFrame();
      closeProjectsModal();
      sendLiveFrameBle();
      showToast("Проект загружен!");
    }
  }

  function deleteProject(idx) {
    let list = JSON.parse(localStorage.getItem('esp32_saved_projects') || '[]');
    list.splice(idx, 1);
    localStorage.setItem('esp32_saved_projects', JSON.stringify(list));
    openSavedProjects();
  }

  function exportAnimJson() {
    const data = JSON.stringify({ fps, frames: animFrames }, null, 2);
    const blob = new Blob([data], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'matrix_animation.json';
    a.click();
    showToast("JSON экспортирован");
  }

  function importAnimJson() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = (e) => {
      const file = e.target.files[0];
      const reader = new FileReader();
      reader.onload = (ev) => {
        try {
          const parsed = JSON.parse(ev.target.result);
          if (parsed.frames && Array.isArray(parsed.frames)) {
            animFrames = parsed.frames;
            if (parsed.fps) {
              fps = parsed.fps;
              document.getElementById('fpsSlider').value = fps;
              document.getElementById('fpsVal').innerText = fps + ' FPS';
            }
            curFrameIdx = 0;
            renderCurrentFrame();
            showToast("Анимация импортирована!");
          }
        } catch (err) {
          showToast("Ошибка в JSON файле");
        }
      };
      reader.readAsText(file);
    };
    input.click();
  }

  // ШАБЛОНЫ
  function loadTemplate(type) {
    let newFrames = [];
    if (type === 'nyan') {
      const rainbow = ['#ff0000', '#ff7700', '#ffee00', '#00ff00', '#0077ff'];
      const crust = '#dca064';
      const frost = '#ff66b2';
      const cat = '#9696a0';
      const cheek = '#ff3399';
      const eye = '#111111';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let bob = step % 2;

        for (let x = 0; x < 13; x++) {
          let wave = ((Math.floor(x / 2) + step) % 2) - bob;
          for (let y = 0; y < 5; y++) {
            let sy = y - wave;
            if (sy >= 0 && sy < 5) frame[y * 20 + x] = rainbow[sy];
          }
        }
        if ((step + 1) % 2 === 0) frame[1 * 20 + 2] = '#ffffff';
        if ((step + 2) % 2 === 0) frame[3 * 20 + 8] = '#ffffff';

        for (let cx = 13; cx <= 16; cx++) {
          for (let cy = 1; cy <= 3; cy++) {
            let y = cy + bob - 1;
            if (y >= 0 && y < 5) {
              let idx = y * 20 + cx;
              frame[idx] = (cx === 13 || cx === 16 || cy === 1 || cy === 3) ? crust : (((cx + cy + step) % 2 === 0) ? '#ff0044' : frost);
            }
          }
        }
        for (let cx = 17; cx <= 19; cx++) {
          for (let cy = 1; cy <= 3; cy++) {
            let y = cy + bob - 1;
            if (y >= 0 && y < 5) {
              let idx = y * 20 + cx;
              if (cx === 17 && cy === 2) frame[idx] = cheek;
              else if (cx === 19 && cy === 2) frame[idx] = eye;
              else frame[idx] = cat;
            }
          }
        }
        let earY = bob;
        if (earY >= 0 && earY < 5) {
          frame[earY * 20 + 17] = cat;
          frame[earY * 20 + 19] = cat;
        }
        if (bob === 0) {
          frame[4 * 20 + 13] = cat;
          frame[4 * 20 + 16] = cat;
        }
        newFrames.push(frame);
      }
    } else if (type === 'pacman') {
      const yellow = '#ffdd00';
      const red = '#ff1133';
      const dot = '#ffcc99';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let pacX = 4 + step * 3;
        let ghostX = pacX - 7;
        let mouthOpen = (step % 2 === 0);

        for (let x = 0; x < 20; x += 3) {
          if (x > pacX) frame[2 * 20 + x] = dot;
        }
        for (let dx = -1; dx <= 1; dx++) {
          for (let dy = -1; dy <= 1; dy++) {
            let px = pacX + dx;
            let py = 2 + dy;
            if (px >= 0 && px < 20 && py >= 0 && py < 5) {
              if (mouthOpen && dx === 1 && dy === 0) continue;
              frame[py * 20 + px] = yellow;
            }
          }
        }
        for (let dx = -1; dx <= 1; dx++) {
          for (let dy = -1; dy <= 1; dy++) {
            let gx = ghostX + dx;
            let gy = 2 + dy;
            if (gx >= 0 && gx < 20 && gy >= 0 && gy < 5) {
              if (dy === 1 && dx === 0) continue;
              frame[gy * 20 + gx] = red;
            }
          }
        }
        newFrames.push(frame);
      }
    } else if (type === 'heart') {
      const red = '#ff0044';
      const pink = '#ff4488';
      const cyan = '#00ffcc';
      const mapBig = [ 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 ];
      const mapSmall = [ 0b00000, 0b01010, 0b01110, 0b00100, 0b00000 ];

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let isBig = (step === 1 || step === 2);
        let m = isBig ? mapBig : mapSmall;
        let col = isBig ? pink : red;

        for (let y = 0; y < 5; y++) {
          for (let bx = 0; bx < 5; bx++) {
            if ((m[y] >> (4 - bx)) & 1) {
              frame[y * 20 + 8 + bx] = col;
            }
          }
        }
        frame[2 * 20 + ((step * 5) % 20)] = cyan;
        newFrames.push(frame);
      }
    } else if (type === 'invader') {
      const green = '#00ff66';
      const lime = '#88ff00';
      const m1 = [0b0010100, 0b0101010, 0b1111111, 0b1010101, 0b1000001];
      const m2 = [0b0010100, 0b0101010, 0b1111111, 0b0010100, 0b0100010];

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let shiftX = (step < 2) ? 4 : 9;
        let m = (step % 2 === 0) ? m1 : m2;
        let col = (step % 2 === 0) ? green : lime;

        for (let y = 0; y < 5; y++) {
          for (let bx = 0; bx < 7; bx++) {
            if ((m[y] >> (6 - bx)) & 1) {
              frame[y * 20 + shiftX + bx] = col;
            }
          }
        }
        newFrames.push(frame);
      }
    } else if (type === 'car') {
      const red = '#ff1100';
      const cyan = '#00f0ff';
      const yellow = '#ffea00';
      const grey = '#445566';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        for (let x = 0; x < 20; x += 4) {
          let rx = (x + step) % 20;
          frame[4 * 20 + rx] = '#555555';
        }
        frame[1 * 20 + 9] = cyan; frame[1 * 20 + 10] = cyan; frame[1 * 20 + 11] = cyan;
        for (let x = 7; x <= 14; x++) frame[2 * 20 + x] = red;
        frame[2 * 20 + 14] = yellow;
        frame[3 * 20 + 8] = grey; frame[3 * 20 + 13] = grey;
        newFrames.push(frame);
      }
    }

    if (newFrames.length > 0) {
      animFrames = newFrames;
      curFrameIdx = 0;
      switchTab('tabDraw');
      renderCurrentFrame();
      sendLiveFrameBle();
      showToast("Шаблон загружен в редактор!");
    }
  }

  // ==========================================
  // --- ТАБ 4: ЭФФЕКТЫ ---
  // ==========================================
  let effectSpeedMs = 55;

  function startEffectBle(id) {
    document.querySelectorAll('.effect-card').forEach((c, i) => {
      c.classList.toggle('active', i === id);
    });
    sendBleRaw(`EFF:${id},${effectSpeedMs}\n`);
    showToast("Эффект запущен!");
  }

  function updateEffectSpeed(v) {
    effectSpeedMs = parseInt(v);
    document.getElementById('effSpeedVal').innerText = v + ' мс';
  }

  // ==========================================
  // --- ТАБ 5: ЯРКОСТЬ ---
  // ==========================================
  let brightTimer = null;
  function updateBrightBle(v) {
    document.getElementById('globalBrightVal').innerText = v;
    clearTimeout(brightTimer);
    brightTimer = setTimeout(() => {
      sendBleRaw(`BRIGHT:${v}\n`);
    }, 70);
  }

  // Начальная отрисовка холста
  renderCurrentFrame();
</script>

</body>
</html>
)rawliteral";
