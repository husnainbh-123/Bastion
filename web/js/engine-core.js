// engine-core.js: loads bastion.wasm and wraps its exported functions.
//
// The engine is compiled for WASI (the WebAssembly System Interface). It only
// needs a handful of system calls - a clock, writing to stdout/stderr and
// reading environment variables - so a tiny shim is provided here instead of a
// full WASI runtime. Works in a Web Worker and in Node (for tests).

const textDecoder = new TextDecoder();
const textEncoder = new TextEncoder();

export async function loadEngine(wasmBytes, onEvent) {
  let memory = null;
  let exports = null;
  const view = () => new DataView(memory.buffer);
  const bytes = (ptr, len) => new Uint8Array(memory.buffer, ptr, len);
  let consoleLine = '';

  const wasi = {
    clock_time_get(clockId, precision, resultPtr) {
      // Monotonic and wall clocks both come from performance.now(), in nanoseconds.
      const ns = BigInt(Math.round((clockId === 0 ? Date.now() : performance.now()) * 1e6));
      view().setBigUint64(resultPtr, ns, true);
      return 0;
    },
    fd_write(fd, iovs, iovsLen, writtenPtr) {
      let written = 0;
      for (let i = 0; i < iovsLen; i++) {
        const ptr = view().getUint32(iovs + i * 8, true);
        const len = view().getUint32(iovs + i * 8 + 4, true);
        consoleLine += textDecoder.decode(bytes(ptr, len).slice());
        written += len;
      }
      let nl;
      while ((nl = consoleLine.indexOf('\n')) >= 0) {
        console.log('[bastion]', consoleLine.slice(0, nl));
        consoleLine = consoleLine.slice(nl + 1);
      }
      view().setUint32(writtenPtr, written, true);
      return 0;
    },
    environ_sizes_get(countPtr, sizePtr) {
      view().setUint32(countPtr, 0, true);
      view().setUint32(sizePtr, 0, true);
      return 0;
    },
    environ_get() { return 0; },
    fd_close() { return 0; },
    fd_seek() { return 70; /* ESPIPE: stdout is not seekable */ },
    proc_exit(code) { throw new Error(`engine exited with code ${code}`); },
  };

  const env = {
    js_emit(ptr, len) {
      const text = textDecoder.decode(bytes(ptr, len).slice());
      onEvent(JSON.parse(text));
    },
  };

  const { instance } = await WebAssembly.instantiate(wasmBytes, { env, wasi_snapshot_preview1: wasi });
  exports = instance.exports;
  memory = exports.memory;
  exports._initialize();
  exports.bastion_init();

  // Copy JavaScript strings into engine memory for the duration of a call.
  function withStrings(strings, fn) {
    const ptrs = strings.map((s) => {
      const encoded = textEncoder.encode(s + '\0');
      const ptr = exports.bastion_alloc(encoded.length);
      bytes(ptr, encoded.length).set(encoded);
      return ptr;
    });
    try {
      return fn(...ptrs);
    } finally {
      ptrs.forEach((p) => exports.bastion_free(p));
    }
  }

  function readCString(ptr) {
    const mem = new Uint8Array(memory.buffer);
    let end = ptr;
    while (mem[end] !== 0) end++;
    return textDecoder.decode(mem.slice(ptr, end));
  }

  return {
    // Legal moves, check and result for the position after `moves` from `fen`.
    state(fen, moves) {
      return withStrings([fen, moves.join(' ')], (f, m) => JSON.parse(readCString(exports.bastion_state(f, m))));
    },
    // Runs synchronously; progress arrives through onEvent as {type: 'info'} and {type: 'bestmove'}.
    search(fen, moves, { movetime = 0, depth = 0, skill = 20 } = {}) {
      return withStrings([fen, moves.join(' ')], (f, m) => exports.bastion_search(f, m, movetime, depth, skill));
    },
    evaluate(fen, moves) {
      return withStrings([fen, moves.join(' ')], (f, m) => exports.bastion_eval(f, m));
    },
    newGame() { exports.bastion_new_game(); },
  };
}
