// wasm_test.mjs: checks the WebAssembly build through the same JavaScript
// loader the website uses. Run after `make wasm`:  node tests/wasm_test.mjs
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadEngine } from '../web/js/engine-core.js';

const events = [];
const wasm = await readFile(new URL('../web/engine/bastion.wasm', import.meta.url));
const engine = await loadEngine(wasm, (event) => events.push(event));

// Rules: legal moves and notation come from the engine.
const start = engine.state('', []);
assert.equal(start.ok, true);
assert.equal(start.legal.length, 20);
assert.equal(start.turn, 'w');
assert.equal(start.result, '*');

const kiwipete = engine.state('r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1', []);
assert.equal(kiwipete.legal.length, 48);
const san = Object.fromEntries(kiwipete.legal);
assert.equal(san.e1g1, 'O-O');
assert.equal(san.e1c1, 'O-O-O');
assert.equal(san.d5e6, 'dxe6');
assert.equal(san.e5f7, 'Nxf7');

const foolsMate = engine.state('', ['f2f3', 'e7e5', 'g2g4', 'd8h4']);
assert.equal(foolsMate.result, '0-1');
assert.equal(foolsMate.reason, 'checkmate');

const stalemate = engine.state('7k/5Q2/6K1/8/8/8/8/8 b - - 0 1', []);
assert.equal(stalemate.result, '1/2-1/2');
assert.equal(stalemate.reason, 'stalemate');

const bad = engine.state('not a fen', []);
assert.equal(bad.ok, false);

// Search: finds a mate in one and reports progress.
events.length = 0;
engine.search('6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1', [], { depth: 6 });
const best = events.find((e) => e.type === 'bestmove');
assert.equal(best.move, 'd1d8');
assert.equal(best.san, 'Rd8#');
assert.ok(events.some((e) => e.type === 'info' && e.mate === 1));

// A short timed search from the start position returns a legal move.
events.length = 0;
engine.search('', ['e2e4'], { movetime: 300 });
const reply = events.find((e) => e.type === 'bestmove');
const legal = engine.state('', ['e2e4']).legal.map(([uci]) => uci);
assert.ok(legal.includes(reply.move), `unexpected reply ${reply.move}`);

console.log('WebAssembly engine: all checks passed');
