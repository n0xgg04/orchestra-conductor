require('dotenv').config();
const mqtt = require('mqtt');

const client = mqtt.connect({
  protocol: 'mqtts',
  host: process.env.MQTT_HOST,
  port: parseInt(process.env.MQTT_PORT || '8883'),
  username: process.env.MQTT_USER,
  password: process.env.MQTT_PASS,
  clientId: 'virtual-esp-' + Math.random().toString(16).substring(2, 8),
  clean: true,
  connectTimeout: 10000,
  reconnectPeriod: 5000,
});

const scores = {};
let isPlaying = false;
let playbackPosition = 0;
let playStartLocalTime = 0;
let globalPosition = 0;
let lastSyncLocalTime = 0;
let activeTimers = [];

function log(msg) {
  const ts = new Date().toISOString().split('T')[1].split('.')[0];
  console.log(`[${ts}] ${msg}`);
}

client.on('connect', () => {
  log('[VirtualESP] Connected to HiveMQ Cloud');
  client.subscribe([
    'orchestra/conductor/command',
    'orchestra/conductor/sync',
    'orchestra/inst/+/score'
  ], { qos: 1 }, (err) => {
    if (err) log('[VirtualESP] Subscribe error: ' + err.message);
    else log('[VirtualESP] Subscribed to command, sync, and score topics');
  });

  setInterval(publishStatus, 5000);
});

client.on('message', (topic, message) => {
  try {
    const payload = JSON.parse(message.toString());
    if (topic === 'orchestra/conductor/command') {
      handleCommand(payload);
    } else if (topic === 'orchestra/conductor/sync') {
      handleSync(payload);
    } else if (topic.startsWith('orchestra/inst/') && topic.endsWith('/score')) {
      handleScore(topic, payload);
    }
  } catch (e) {
    log('[VirtualESP] Parse error: ' + e.message);
  }
});

function handleScore(topic, data) {
  const id = data.id;
  scores[id] = data;
  log(`[VirtualESP] Score received: Inst#${id} (${data.name}) - ${data.notes.length} notes, pin=${data.pin}`);
  client.publish(`orchestra/inst/${id}/ack`, JSON.stringify({ instrumentId: id, status: 'ok' }));
}

function handleCommand(cmd) {
  const now = Date.now();
  log(`[VirtualESP] COMMAND: ${cmd.type.toUpperCase()}` + (cmd.position !== undefined ? ` @ ${cmd.position}ms` : ''));

  clearAllTimers();

  switch (cmd.type) {
    case 'play': {
      globalPosition = cmd.position || 0;
      lastSyncLocalTime = now;
      playStartLocalTime = now;
      playbackPosition = globalPosition;
      isPlaying = true;
      scheduleNotes();
      break;
    }
    case 'pause': {
      if (isPlaying) {
        playbackPosition += (now - playStartLocalTime);
        isPlaying = false;
      }
      break;
    }
    case 'seek': {
      globalPosition = cmd.position || 0;
      lastSyncLocalTime = now;
      playStartLocalTime = now;
      playbackPosition = globalPosition;
      if (isPlaying) scheduleNotes();
      break;
    }
    case 'stop': {
      isPlaying = false;
      playbackPosition = 0;
      globalPosition = 0;
      break;
    }
  }
}

function handleSync(sync) {
  globalPosition = sync.position;
  lastSyncLocalTime = Date.now();
  if (!sync.isPlaying && isPlaying) {
    isPlaying = false;
    clearAllTimers();
  }
}

function scheduleNotes() {
  clearAllTimers();
  const now = Date.now();
  const currentPos = playbackPosition;

  for (const id in scores) {
    const inst = scores[id];
    for (const note of inst.notes) {
      if (note.t < currentPos) continue;
      const delay = note.t - currentPos;
      const timer = setTimeout(() => {
        if (!isPlaying) return;
        if (inst.id === 4) {
          log(`[PLAY] >>> DRUMS click @ ${note.t}ms (dur=${note.d}ms)`);
        } else {
          log(`[PLAY] >>> ${inst.name.toUpperCase()} note ${note.f}Hz @ ${note.t}ms (dur=${note.d}ms)`);
        }
      }, delay);
      activeTimers.push(timer);
    }
  }
}

function clearAllTimers() {
  for (const t of activeTimers) clearTimeout(t);
  activeTimers = [];
}

function publishStatus() {
  client.publish('orchestra/esp/status', JSON.stringify({
    clientId: client.options.clientId,
    status: 'online',
    instruments: Object.keys(scores).length,
    isPlaying,
  }));
}

client.on('error', (err) => {
  log('[VirtualESP] MQTT error: ' + err.message);
});

client.on('close', () => {
  log('[VirtualESP] Connection closed');
});
