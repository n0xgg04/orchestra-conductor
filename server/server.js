require('dotenv').config();
const express = require('express');
const http = require('http');
const path = require('path');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const cors = require('cors');

// ===== ENV VALIDATION =====
const MQTT_HOST = process.env.MQTT_HOST;
const MQTT_PORT = parseInt(process.env.MQTT_PORT || '8883');
const MQTT_USER = process.env.MQTT_USER;
const MQTT_PASS = process.env.MQTT_PASS;
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '3000');

if (!MQTT_HOST || !MQTT_USER || !MQTT_PASS) {
  console.error('[ENV] Missing required environment variables: MQTT_HOST, MQTT_USER, MQTT_PASS');
  process.exit(1);
}

// ===== EXPRESS + SOCKET.IO =====
const app = express();
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: '*' } });

app.use(cors());
app.use(express.json({ limit: '2mb' }));
app.use(express.static(path.join(__dirname, '../frontend')));

// ===== CONDUCTOR STATE =====
let scoreData = null;
let isPlaying = false;
let playbackPosition = 0;
let playStartTime = 0;
let syncInterval = null;
let instrumentStatus = new Map();

// ===== MQTT CLIENT (Conductor) =====
const mqttClient = mqtt.connect({
  protocol: 'mqtts',
  host: MQTT_HOST,
  port: MQTT_PORT,
  username: MQTT_USER,
  password: MQTT_PASS,
  clientId: 'conductor-server-' + Math.random().toString(16).substring(2, 8),
  clean: true,
  connectTimeout: 10000,
  reconnectPeriod: 5000,
});

console.log(`[MQTT] Connecting to ${MQTT_HOST}:${MQTT_PORT}...`);

mqttClient.on('connect', () => {
  console.log('[MQTT] Conductor connected to broker');
  mqttClient.subscribe('orchestra/inst/+/ack', { qos: 1 });
  mqttClient.subscribe('orchestra/esp/status', { qos: 1 });
});

mqttClient.on('message', (topic, message) => {
  try {
    const payload = JSON.parse(message.toString());
    if (topic.startsWith('orchestra/inst/')) {
      const idMatch = topic.match(/orchestra\/inst\/(\d+)\/ack/);
      if (idMatch) {
        const id = parseInt(idMatch[1]);
        instrumentStatus.set(id, {
          online: true,
          ack: payload.status === 'ok',
          lastSeen: Date.now(),
        });
        io.emit('status', { instrumentId: id, ...payload });
      }
    } else if (topic === 'orchestra/esp/status') {
      io.emit('espStatus', payload);
    }
  } catch (e) {
    console.error('[MQTT] Parse error:', e.message);
  }
});

// ===== PLAYBACK CONTROL =====
function getCurrentPosition() {
  if (!isPlaying) return playbackPosition;
  return playbackPosition + (Date.now() - playStartTime);
}

function startSyncBroadcast() {
  if (syncInterval) clearInterval(syncInterval);
  syncInterval = setInterval(() => {
    if (!isPlaying) return;
    const pos = getCurrentPosition();
    mqttClient.publish('orchestra/conductor/sync', JSON.stringify({
      position: pos,
      isPlaying: true,
      sentAt: Date.now(),
    }), { qos: 0 });
  }, 200);
}

function stopSyncBroadcast() {
  if (syncInterval) {
    clearInterval(syncInterval);
    syncInterval = null;
  }
}

function broadcastCommand(type, extra = {}) {
  const cmd = { type, serverTime: Date.now(), ...extra };
  mqttClient.publish('orchestra/conductor/command', JSON.stringify(cmd), { qos: 1 });
  console.log('[Command]', cmd);
}

// ===== HTTP API =====
app.post('/api/score', (req, res) => {
  scoreData = req.body;
  if (!scoreData || !Array.isArray(scoreData.instruments)) {
    return res.status(400).json({ error: 'Invalid score format. Expected { instruments: [...] }' });
  }

  instrumentStatus.clear();
  for (const inst of scoreData.instruments) {
    const topic = `orchestra/inst/${inst.id}/score`;
    mqttClient.publish(topic, JSON.stringify(inst), { qos: 1 });
    instrumentStatus.set(inst.id, { online: false, ack: false, lastSeen: 0 });
  }

  isPlaying = false;
  playbackPosition = 0;
  stopSyncBroadcast();

  io.emit('scoreUpdated', scoreData);
  res.json({ success: true, instrumentCount: scoreData.instruments.length });
});

app.post('/api/control', (req, res) => {
  const { action, position } = req.body;
  if (!['play', 'pause', 'seek', 'stop'].includes(action)) {
    return res.status(400).json({ error: 'Invalid action. Use play, pause, seek, stop' });
  }

  switch (action) {
    case 'play': {
      playbackPosition = position || 0;
      playStartTime = Date.now();
      isPlaying = true;
      startSyncBroadcast();
      broadcastCommand('play', { position: playbackPosition });
      break;
    }
    case 'pause': {
      if (isPlaying) {
        playbackPosition += Date.now() - playStartTime;
        isPlaying = false;
      }
      stopSyncBroadcast();
      broadcastCommand('pause');
      break;
    }
    case 'seek': {
      playbackPosition = position || 0;
      if (isPlaying) {
        playStartTime = Date.now();
      }
      broadcastCommand('seek', { position: playbackPosition });
      break;
    }
    case 'stop': {
      isPlaying = false;
      playbackPosition = 0;
      stopSyncBroadcast();
      broadcastCommand('stop');
      break;
    }
  }

  const state = { isPlaying, position: getCurrentPosition(), action };
  io.emit('playbackState', state);
  res.json({ success: true, ...state });
});

app.get('/api/status', (req, res) => {
  const status = [];
  for (const [id, st] of instrumentStatus.entries()) {
    status.push({ id, ...st });
  }
  res.json({
    isPlaying,
    position: getCurrentPosition(),
    instruments: status,
  });
});

// Socket.IO
io.on('connection', (socket) => {
  console.log('[Socket] Client connected');
  socket.emit('playbackState', { isPlaying, position: getCurrentPosition() });
  if (scoreData) socket.emit('scoreUpdated', scoreData);
});

// Start
server.listen(HTTP_PORT, () => {
  console.log(`[HTTP] Server listening on http://localhost:${HTTP_PORT}`);
});
