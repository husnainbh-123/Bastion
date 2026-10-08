// app.js: game flow for the "Play" section. The engine (in a Web Worker)
// supplies legal moves, notation and game results; this file keeps the move
// list, talks to the board and fills in the side panel.
import { Board, parseFen, squareIndex } from './board.js';

const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
const MIN_REPLY_MS = 450; // so instant replies at low levels don't feel abrupt

const LEVELS = [
  { name: 'Beginner', skill: 0, depth: 1, about: 'Looks only at its next move and makes frequent mistakes.' },
  { name: 'Novice', skill: 3, depth: 2, about: 'Looks one move ahead for each side, with frequent mistakes.' },
  { name: 'Casual', skill: 6, depth: 4, about: 'Looks two moves ahead for each side, with some mistakes.' },
  { name: 'Club player', skill: 10, depth: 6, about: 'Looks three moves ahead for each side and rarely blunders.' },
  { name: 'Strong club player', skill: 14, depth: 8, about: 'Looks four moves ahead for each side.' },
  { name: 'Expert', skill: 17, depth: 10, about: 'Looks five moves ahead for each side, with small inaccuracies.' },
  { name: 'Master', skill: 20, movetime: 1000, about: 'The full engine, thinking for one second per move.' },
  { name: 'Full strength', skill: 20, movetime: 3000, about: 'The full engine, thinking for three seconds per move.' },
];

const $ = (sel) => document.querySelector(sel);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------------------
// Settings remembered between visits (best effort: storage can be unavailable)
// ---------------------------------------------------------------------------
const settings = { level: 6, color: 'w', showThinking: true };
try {
  Object.assign(settings, JSON.parse(localStorage.getItem('bastion-settings') || '{}'));
} catch { /* private mode or blocked storage */ }
const saveSettings = () => {
  try { localStorage.setItem('bastion-settings', JSON.stringify(settings)); } catch { /* ignore */ }
};

// ---------------------------------------------------------------------------
// Engine client: wraps the worker and lets a running search be abandoned
// ---------------------------------------------------------------------------
class EngineClient {
  constructor(onEvent) {
    this.onEvent = onEvent;
    this.spawn();
  }

  spawn() {
    if (this.worker) this.worker.terminate();
    this.pending = new Map();
    this.nextId = 1;
    this.ready = new Promise((resolve, reject) => { this.resolveReady = resolve; this.rejectReady = reject; });
    this.worker = new Worker(new URL('./engine-worker.js', import.meta.url), { type: 'module' });
    this.worker.onmessage = ({ data }) => {
      if (data.kind === 'ready') this.resolveReady();
      else if (data.kind === 'error') this.rejectReady(new Error(data.message));
      else if (data.kind === 'event') this.onEvent(data.event);
      else if (data.kind === 'reply') {
        const resolve = this.pending.get(data.id);
        this.pending.delete(data.id);
        if (resolve) resolve(data.result);
      }
    };
    this.worker.onerror = (e) => this.rejectReady(new Error(e.message || 'the engine failed to start'));
  }

  call(type, payload = {}) {
    const id = this.nextId++;
    return new Promise((resolve) => {
      this.pending.set(id, resolve);
      this.worker.postMessage({ id, type, ...payload });
    });
  }

  // A search can't be interrupted from outside, so abandon the worker and start a fresh one.
  restart() {
    for (const resolve of this.pending.values()) resolve(null);
    this.spawn();
    return this.ready;
  }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const game = { startFen: START_FEN, moves: [], sans: [], human: settings.color };
let current = null;      // engine's view of the current position
let thinking = false;
let searchToken = 0;     // bumps whenever a pending search should be ignored
let syncToken = 0;       // only the latest position update is applied
let lastInfo = null;
let lastBest = null;
let engineReady = false;

const statusEl = $('#status');
const announcer = $('#announcer');
const announce = (text) => { announcer.textContent = ''; requestAnimationFrame(() => { announcer.textContent = text; }); };

const board = new Board($('#board'), { onMove: humanMove, announce });
const engine = new EngineClient(onEngineEvent);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const turnAt = (plies) => {
  const first = game.startFen.split(' ')[1];
  return plies % 2 === 0 ? first : first === 'w' ? 'b' : 'w';
};
const engineColor = () => (game.human === 'w' ? 'b' : 'w');
const humanMovesPlayed = () => game.moves.filter((_, i) => turnAt(i) === game.human).length;

function kingSquare(fen, color) {
  const pieces = parseFen(fen);
  const king = color === 'w' ? 'K' : 'k';
  return Number(Object.keys(pieces).find((sq) => pieces[sq] === king));
}

// White-point-of-view evaluation from an info event (scores are from the engine's side).
function whiteEval(info) {
  const sign = engineColor() === 'w' ? 1 : -1;
  return { cp: info.score * sign, mate: info.mate * sign };
}

function formatEval({ cp, mate }) {
  if (mate) return `${mate > 0 ? '' : '−'}M${Math.abs(mate)}`;
  const pawns = cp / 100;
  return `${pawns > 0 ? '+' : pawns < 0 ? '−' : ''}${Math.abs(pawns).toFixed(2)}`;
}

function verdict({ cp, mate }) {
  if (mate) return `${mate > 0 ? 'White' : 'Black'} can force mate in ${Math.abs(mate)}`;
  const side = cp > 0 ? 'White' : 'Black', a = Math.abs(cp);
  if (a < 30) return 'The position is roughly equal';
  if (a < 100) return `${side} is slightly better`;
  if (a < 250) return `${side} is better`;
  return `${side} is winning`;
}

const roundTo3 = (n) => { const p = Math.pow(10, Math.max(0, Math.floor(Math.log10(n)) - 2)); return Math.round(n / p) * p; };

function formatCount(n) {
  if (n >= 1e6) return `${(n / 1e6).toFixed(n >= 1e7 ? 1 : 2)} million`;
  return Math.round(n).toLocaleString('en-GB');
}

function moveNumberPrefix(plyIndex, force) {
  const startFull = Number(game.startFen.split(' ')[5] || 1);
  const startBlack = game.startFen.split(' ')[1] === 'b';
  const ply = plyIndex + (startBlack ? 1 : 0);
  const number = startFull + Math.floor(ply / 2);
  if (ply % 2 === 0) return `${number}. `;
  return force ? `${number}… ` : '';
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
function setStatus(text) { statusEl.textContent = text; }

function renderMoves() {
  const list = $('#moves');
  const startBlack = game.startFen.split(' ')[1] === 'b';
  const rows = [];
  const sans = startBlack ? ['', ...game.sans] : game.sans;
  for (let i = 0; i < sans.length; i += 2) rows.push([sans[i], sans[i + 1] ?? '']);
  const startFull = Number(game.startFen.split(' ')[5] || 1);
  const last = game.sans.length - 1 + (startBlack ? 1 : 0);
  list.innerHTML = rows.map(([w, b], r) =>
    `<li><span class="num">${startFull + r}.</span>` +
    `<span class="mv${last === 2 * r ? ' latest' : ''}">${w || '…'}</span>` +
    `<span class="mv${last === 2 * r + 1 ? ' latest' : ''}">${b}</span></li>`).join('');
  $('#moves-empty').hidden = game.sans.length > 0;
  list.scrollTop = list.scrollHeight;
}

function renderGauge(ev) {
  // Share of the gauge given to White, from the evaluation (logistic, as on Lichess).
  let share = 50;
  if (ev) share = ev.mate ? (ev.mate > 0 ? 100 : 0) : 50 + 50 * (2 / (1 + Math.exp(-0.00368208 * ev.cp)) - 1);
  const fill = $('#gauge-fill');
  fill.style.height = `${share}%`;
  fill.classList.toggle('from-top', board.orientation === 'b');
  $('#gauge').setAttribute('aria-valuenow', Math.round(share));
  $('#gauge').setAttribute('aria-valuetext', ev ? `${verdict(ev)} (${formatEval(ev)})` : 'No evaluation yet');
}

function renderReadout(info) {
  const box = $('#readout');
  if (!info) {
    box.dataset.empty = 'true';
    $('#r-eval').textContent = '–';
    $('#r-verdict').textContent = 'Bastion’s analysis appears here while it thinks.';
    $('#r-depth').textContent = '–';
    $('#r-nodes').textContent = '–';
    $('#r-speed').textContent = '–';
    $('#pv').textContent = '';
    renderGauge(null);
    return;
  }
  box.dataset.empty = 'false';
  const ev = whiteEval(info);
  $('#r-eval').textContent = formatEval(ev);
  $('#r-verdict').textContent = `${verdict(ev)}.`;
  $('#r-depth').textContent = `${info.depth} half-moves`;
  $('#r-nodes').textContent = formatCount(info.nodes);
  $('#r-speed').textContent = info.time > 0 ? `${formatCount(roundTo3((info.nodes * 1000) / info.time))} a second` : '–';
  const base = game.moves.length;
  $('#pv').textContent = info.pv.length
    ? 'Expected line: ' + info.pv.map((san, i) => moveNumberPrefix(base + i, i === 0) + san).join(' ')
    : '';
  renderGauge(ev);
}

function updateControls() {
  $('#undo').disabled = !engineReady || humanMovesPlayed() === 0;
  $('#copy-pgn').disabled = game.sans.length === 0;
  $('#new-game').disabled = !engineReady;
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------
async function sync(animateMove = null, animate = true) {
  const token = ++syncToken;
  const st = await engine.call('state', { fen: game.startFen, moves: game.moves });
  if (!st || token !== syncToken) return;  // the engine restarted or a newer update is on its way
  current = st;
  board.setPosition(st.fen, animateMove, animate);
  board.setHighlights({ lastMove: game.moves[game.moves.length - 1] || null, check: st.check ? kingSquare(st.fen, st.turn) : null });
  renderMoves();
  updateControls();

  if (st.result !== '*') {
    board.setLegalMoves([], false);
    board.showArrows([]);
    const winner = st.result === '1-0' ? 'w' : st.result === '0-1' ? 'b' : null;
    const text = winner === null ? `Draw by ${st.reason}.`
      : winner === game.human ? 'Checkmate. You win!' : 'Checkmate. Bastion wins.';
    setStatus(text);
    announce(text);
    return;
  }
  if (st.turn === game.human) {
    board.setLegalMoves(st.legal.map((m) => m[0]), true);
    const lastSan = game.sans[game.sans.length - 1];
    const engineJustMoved = game.moves.length > 0 && turnAt(game.moves.length - 1) !== game.human;
    const prefix = engineJustMoved ? `Bastion played ${lastSan}. ` : '';
    setStatus(prefix + (st.check ? 'You are in check.' : 'Your move.'));
  } else {
    board.setLegalMoves([], false);
    think();
  }
}

async function think() {
  const token = ++searchToken;
  const level = LEVELS[settings.level];
  thinking = true;
  lastBest = null;
  setStatus('Bastion is thinking…');
  updateControls();
  const started = performance.now();
  const ok = await engine.call('search', {
    fen: game.startFen,
    moves: game.moves,
    options: { movetime: level.movetime || 0, depth: level.depth || 0, skill: level.skill },
  });
  if (token !== searchToken || ok === null) return;
  const wait = MIN_REPLY_MS - (performance.now() - started);
  if (wait > 0) await sleep(wait);
  if (token !== searchToken) return;
  thinking = false;
  board.showArrows([]);
  if (!lastBest || !lastBest.move) {
    setStatus('Bastion could not find a move. Start a new game to continue.');
    return;
  }
  announce(`Bastion played ${lastBest.san}.`);
  play(lastBest.move, lastBest.san, true);
}

function play(uci, san, animate) {
  game.moves.push(uci);
  game.sans.push(san);
  sync(uci, animate);
}

function onEngineEvent(ev) {
  if (ev.type === 'info') {
    lastInfo = ev;
    renderReadout(ev);
    if (settings.showThinking && thinking) board.showArrows(ev.pvUci);
  } else if (ev.type === 'bestmove') {
    lastBest = ev;
  }
}

async function humanMove(prefix, options, animate) {
  if (!current || current.turn !== game.human) return;
  let uci = options[0];
  if (options.length > 1) {
    uci = await choosePromotion(prefix, options);
    if (!uci) { board.setPosition(current.fen, null, false); return; }
  }
  const entry = current.legal.find((m) => m[0] === uci);
  if (!entry) return;
  board.setLegalMoves([], false);
  play(uci, entry[1], animate);
}

function choosePromotion(prefix, options) {
  return new Promise((resolve) => {
    const dialog = $('#promotion');
    const white = game.human === 'w';
    dialog.innerHTML = ['q', 'r', 'b', 'n'].map((p) =>
      `<button type="button" data-p="${p}" aria-label="Promote to ${{ q: 'queen', r: 'rook', b: 'bishop', n: 'knight' }[p]}">` +
      `<svg viewBox="0 0 100 100" class="piece-icon ${white ? 'white' : 'black'}"><use href="#p${p.toUpperCase()}"></use></svg></button>`).join('');
    dialog.hidden = false;
    const finish = (choice) => {
      dialog.hidden = true;
      dialog.removeEventListener('click', onClick);
      document.removeEventListener('keydown', onKey);
      resolve(choice);
    };
    const onClick = (e) => {
      const btn = e.target.closest('button');
      if (btn) finish(prefix + btn.dataset.p);
    };
    const onKey = (e) => { if (e.key === 'Escape') finish(null); };
    dialog.addEventListener('click', onClick);
    document.addEventListener('keydown', onKey);
    dialog.querySelector('button').focus();
  });
}

async function abandonSearch() {
  if (!thinking) return;
  searchToken++;
  thinking = false;
  board.showArrows([]);
  await engine.restart();
}

async function newGame() {
  await abandonSearch();
  game.moves = [];
  game.sans = [];
  game.human = settings.color;
  lastInfo = null;
  board.setOrientation(game.human);
  updateCoordinates();
  renderReadout(null);
  engine.call('newGame');
  sync(null, false);
}

async function undo() {
  await abandonSearch();
  if (!humanMovesPlayed()) return;
  do {
    game.moves.pop();
    game.sans.pop();
  } while (game.moves.length && turnAt(game.moves.length) !== game.human);
  renderReadout(null);
  sync(null, false);
}

function flip() {
  board.setOrientation(board.orientation === 'w' ? 'b' : 'w');
  updateCoordinates();
  renderGauge(lastInfo ? whiteEval(lastInfo) : null);
  if (thinking && settings.showThinking && lastInfo) board.showArrows(lastInfo.pvUci);
}

function updateCoordinates() {
  const files = 'abcdefgh';
  const w = board.orientation === 'w';
  document.querySelectorAll('.coord-file').forEach((el, i) => { el.textContent = files[w ? i % 8 : 7 - (i % 8)]; });
  document.querySelectorAll('.coord-rank').forEach((el, i) => { el.textContent = String(w ? 8 - (i % 8) : (i % 8) + 1); });
}

function pgn() {
  const level = LEVELS[settings.level].name;
  const d = new Date();
  const date = `${d.getFullYear()}.${String(d.getMonth() + 1).padStart(2, '0')}.${String(d.getDate()).padStart(2, '0')}`;
  const result = current && current.result !== '*' ? current.result : '*';
  const white = game.human === 'w' ? 'You' : `Bastion (${level})`;
  const black = game.human === 'b' ? 'You' : `Bastion (${level})`;
  let text = `[Event "Casual game"]\n[Site "Bastion web app"]\n[Date "${date}"]\n[White "${white}"]\n[Black "${black}"]\n[Result "${result}"]\n`;
  if (game.startFen !== START_FEN) text += `[SetUp "1"]\n[FEN "${game.startFen}"]\n`;
  const body = game.sans.map((san, i) => moveNumberPrefix(i, i === 0) + san).join(' ');
  return `${text}\n${body}${body ? ' ' : ''}${result}\n`;
}

async function copyText(text, button) {
  const label = button.textContent;
  try {
    await navigator.clipboard.writeText(text);
    button.textContent = 'Copied';
  } catch {
    const box = $('#copy-fallback');
    box.hidden = false;
    box.value = text;
    box.select();
    button.textContent = 'Select and copy';
  }
  setTimeout(() => { button.textContent = label; }, 1600);
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
function renderLevel() {
  const level = LEVELS[settings.level];
  $('#level').value = String(settings.level + 1);
  $('#level-name').textContent = level.name;
  $('#level-about').textContent = level.about;
}

$('#level').addEventListener('input', (e) => {
  settings.level = Number(e.target.value) - 1;
  saveSettings();
  renderLevel();
});
document.querySelectorAll('input[name="color"]').forEach((radio) => {
  radio.checked = radio.value === settings.color;
  radio.addEventListener('change', () => { settings.color = radio.value; saveSettings(); });
});
$('#show-thinking').checked = settings.showThinking;
$('#show-thinking').addEventListener('change', (e) => {
  settings.showThinking = e.target.checked;
  saveSettings();
  if (!settings.showThinking) board.showArrows([]);
});
$('#new-game').addEventListener('click', newGame);
$('#undo').addEventListener('click', undo);
$('#flip').addEventListener('click', flip);
$('#copy-pgn').addEventListener('click', (e) => copyText(pgn(), e.currentTarget));

renderLevel();
renderReadout(null);
board.setOrientation(game.human);
updateCoordinates();
board.setPosition(START_FEN, null, false);
updateControls();

engine.ready.then(() => {
  engineReady = true;
  updateControls();
  sync(null, false);
}, (error) => {
  setStatus(`The engine could not start (${error.message}). Reload the page to try again.`);
});
