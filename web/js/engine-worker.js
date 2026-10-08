// engine-worker.js: runs the engine off the main thread, so the page stays
// responsive while Bastion searches.
import { loadEngine } from './engine-core.js';

let engine = null;
const ready = (async () => {
  const response = await fetch(new URL('../engine/bastion.wasm', import.meta.url));
  if (!response.ok) throw new Error(`could not download the engine (HTTP ${response.status})`);
  engine = await loadEngine(await response.arrayBuffer(), (event) => postMessage({ kind: 'event', event }));
})();

ready.then(
  () => postMessage({ kind: 'ready' }),
  (error) => postMessage({ kind: 'error', message: String(error && error.message || error) }),
);

onmessage = async ({ data }) => {
  await ready;
  const { id, type, fen = '', moves = [], options = {} } = data;
  let result = null;
  if (type === 'state') result = engine.state(fen, moves);
  else if (type === 'search') result = engine.search(fen, moves, options);
  else if (type === 'newGame') engine.newGame();
  postMessage({ kind: 'reply', id, result });
};
