// board.js: the interactive chess board.
//
// Draws squares and pieces, animates moves, and turns mouse, touch and
// keyboard input into moves. It knows nothing about chess rules: the page
// passes in the legal moves the engine generated.

const FILES = 'abcdefgh';
const PIECE_NAMES = { p: 'pawn', n: 'knight', b: 'bishop', r: 'rook', q: 'queen', k: 'king' };

export const squareName = (sq) => FILES[sq & 7] + ((sq >> 3) + 1);
export const squareIndex = (name) => FILES.indexOf(name[0]) + 8 * (Number(name[1]) - 1);

// Piece placement from the first field of a FEN string: { square: 'P' | 'n' | ... }
export function parseFen(fen) {
  const pieces = {};
  const rows = fen.split(' ')[0].split('/');
  rows.forEach((row, i) => {
    let file = 0;
    for (const ch of row) {
      if (/\d/.test(ch)) file += Number(ch);
      else pieces[(7 - i) * 8 + file++] = ch;
    }
  });
  return pieces;
}

export class Board {
  constructor(root, { onMove, announce }) {
    this.root = root;
    this.onMove = onMove;
    this.announce = announce;
    this.orientation = 'w';
    this.pieces = {};          // square -> piece letter
    this.elements = new Map(); // square -> piece element
    this.legal = [];           // UCI strings available to the player
    this.interactive = false;
    this.selected = null;
    this.cursor = 12;          // keyboard focus square (e2)
    this.drag = null;
    this.highlights = { lastMove: null, check: null };
    this.keyboardMode = false;  // show the square cursor only to keyboard users

    this.squaresEl = root.querySelector('.squares');
    this.piecesEl = root.querySelector('.pieces');
    this.arrowsEl = root.querySelector('.arrows');
    this.squareEls = [];
    for (let i = 0; i < 64; i++) {
      const el = document.createElement('div');
      el.className = 'sq';
      this.squaresEl.appendChild(el);
      this.squareEls.push(el);
    }
    this.layoutSquares();

    root.addEventListener('pointerdown', (e) => this.pointerDown(e));
    root.addEventListener('pointermove', (e) => this.pointerMove(e));
    root.addEventListener('pointerup', (e) => this.pointerUp(e));
    root.addEventListener('pointercancel', () => this.cancelDrag());
    root.addEventListener('keydown', (e) => this.keyDown(e));
    root.addEventListener('focus', () => {
      // Keyboard focus (Tab) shows the square cursor; a mouse click does not.
      try { this.keyboardMode = root.matches(':focus-visible'); } catch { this.keyboardMode = false; }
      this.paintSquares();
    });
    root.addEventListener('blur', () => this.paintSquares());
  }

  // ---- Geometry -----------------------------------------------------------
  displayPos(sq) {
    const file = sq & 7, rank = sq >> 3;
    return this.orientation === 'w' ? { col: file, row: 7 - rank } : { col: 7 - file, row: rank };
  }

  squareAt(clientX, clientY) {
    const r = this.root.getBoundingClientRect();
    const col = Math.floor(((clientX - r.left) / r.width) * 8);
    const row = Math.floor(((clientY - r.top) / r.height) * 8);
    if (col < 0 || col > 7 || row < 0 || row > 7) return null;
    return this.orientation === 'w' ? (7 - row) * 8 + col : row * 8 + (7 - col);
  }

  layoutSquares() {
    for (let sq = 0; sq < 64; sq++) {
      const { col, row } = this.displayPos(sq);
      const el = this.squareEls[sq];
      el.style.gridColumn = col + 1;
      el.style.gridRow = row + 1;
      el.dataset.sq = sq;
      el.classList.toggle('dark', ((sq & 7) + (sq >> 3)) % 2 === 0);
    }
  }

  setOrientation(color) {
    this.orientation = color;
    this.layoutSquares();
    for (const [sq, el] of this.elements) this.placeElement(el, sq, false);
    this.paintSquares();
  }

  // ---- Pieces ---------------------------------------------------------------
  makePieceElement(piece) {
    const el = document.createElement('div');
    const white = piece === piece.toUpperCase();
    el.className = `piece ${white ? 'white' : 'black'}`;
    el.innerHTML = `<svg viewBox="0 0 100 100" aria-hidden="true"><use href="#p${piece.toUpperCase()}"></use></svg>`;
    el.dataset.piece = piece;
    return el;
  }

  placeElement(el, sq, animate) {
    const { col, row } = this.displayPos(sq);
    el.classList.toggle('animate', !!animate);
    el.style.transform = `translate(${col * 100}%, ${row * 100}%)`;
  }

  // Show a position. When `move` (UCI) is given, the moving piece slides there.
  setPosition(fen, move = null, animate = true) {
    const next = parseFen(fen);
    const old = this.elements;
    const fresh = new Map();

    if (move) {
      const from = squareIndex(move.slice(0, 2));
      const to = squareIndex(move.slice(2, 4));
      const slides = [[from, to]];
      const moved = this.pieces[from];
      // Castling also moves a rook.
      if (moved && moved.toLowerCase() === 'k' && Math.abs(to - from) === 2) {
        slides.push(to > from ? [from + 3, from + 1] : [from - 4, from - 1]);
      }
      for (const [a, b] of slides) {
        const el = old.get(a);
        if (!el || !next[b]) continue;
        old.delete(a);
        if (old.has(b)) { old.get(b).remove(); old.delete(b); }  // captured piece
        if (el.dataset.piece !== next[b]) {                        // promotion
          el.dataset.piece = next[b];
          el.querySelector('use').setAttribute('href', `#p${next[b].toUpperCase()}`);
        }
        fresh.set(b, el);
        this.placeElement(el, b, animate);
      }
    }

    // Keep pieces that did not change, rebuild everything else.
    for (const [sq, el] of old) {
      if (!fresh.has(sq) && next[sq] === el.dataset.piece) {
        fresh.set(sq, el);
        this.placeElement(el, sq, false);
      } else el.remove();
    }
    for (const sq of Object.keys(next).map(Number)) {
      if (fresh.has(sq)) continue;
      const el = this.makePieceElement(next[sq]);
      this.placeElement(el, sq, false);
      this.piecesEl.appendChild(el);
      fresh.set(sq, el);
    }
    this.elements = fresh;
    this.pieces = next;
    this.selected = null;
    this.paintSquares();
  }

  setLegalMoves(moves, interactive) {
    this.legal = moves;
    this.interactive = interactive;
    this.root.classList.toggle('interactive', interactive);
    if (!interactive) this.selected = null;
    this.paintSquares();
  }

  setHighlights({ lastMove = null, check = null }) {
    this.highlights = { lastMove, check };
    this.paintSquares();
  }

  targetsFrom(sq) {
    const name = squareName(sq);
    return [...new Set(this.legal.filter((m) => m.startsWith(name)).map((m) => squareIndex(m.slice(2, 4))))];
  }

  paintSquares() {
    const { lastMove, check } = this.highlights;
    const lm = lastMove ? [squareIndex(lastMove.slice(0, 2)), squareIndex(lastMove.slice(2, 4))] : [];
    const targets = this.selected !== null ? this.targetsFrom(this.selected) : [];
    const focused = this.keyboardMode && document.activeElement === this.root;
    for (let sq = 0; sq < 64; sq++) {
      const el = this.squareEls[sq];
      el.classList.toggle('last', lm.includes(sq));
      el.classList.toggle('selected', sq === this.selected);
      el.classList.toggle('target', targets.includes(sq) && !this.pieces[sq]);
      el.classList.toggle('capture', targets.includes(sq) && !!this.pieces[sq]);
      el.classList.toggle('check', sq === check);
      el.classList.toggle('cursor', focused && sq === this.cursor);
    }
  }

  // ---- Arrows showing the engine's expected line ------------------------------
  showArrows(moves) {
    const center = (sq) => {
      const { col, row } = this.displayPos(sq);
      return [col * 100 + 50, row * 100 + 50];
    };
    this.arrowsEl.innerHTML = moves.slice(0, 3).map((m, i) => {
      const [x1, y1] = center(squareIndex(m.slice(0, 2)));
      const [x2, y2] = center(squareIndex(m.slice(2, 4)));
      const len = Math.hypot(x2 - x1, y2 - y1);
      const ux = (x2 - x1) / len, uy = (y2 - y1) / len;
      const w = [15, 11, 9][i], head = w * 2.2;
      const sx = x1 + ux * 18, sy = y1 + uy * 18;              // start just off the piece centre
      const ex = x2 - ux * head * 0.9, ey = y2 - uy * head * 0.9; // shaft ends where the head begins
      const px = -uy, py = ux;
      const tip = `${x2 - ux * 6},${y2 - uy * 6}`;
      const left = `${ex + px * head},${ey + py * head}`;
      const right = `${ex - px * head},${ey - py * head}`;
      return `<g class="arrow a${i}"><line x1="${sx}" y1="${sy}" x2="${ex}" y2="${ey}" stroke-width="${w}"/>` +
        `<polygon points="${tip} ${left} ${right}"/></g>`;
    }).join('');
  }

  // ---- Pointer input -------------------------------------------------------
  pointerDown(e) {
    if (!this.interactive || e.button > 0) return;
    const sq = this.squareAt(e.clientX, e.clientY);
    if (sq === null) return;
    this.keyboardMode = false;
    this.cursor = sq;

    if (this.selected !== null && this.targetsFrom(this.selected).includes(sq)) {
      this.tryMove(this.selected, sq);
      return;
    }
    if (this.targetsFrom(sq).length) {
      this.selected = sq;
      const el = this.elements.get(sq);
      const r = this.root.getBoundingClientRect();
      this.drag = { sq, el, startX: e.clientX, startY: e.clientY, size: r.width / 8, active: false };
      this.root.setPointerCapture(e.pointerId);
      e.preventDefault();
    } else {
      this.selected = null;
    }
    this.paintSquares();
  }

  pointerMove(e) {
    const d = this.drag;
    if (!d || !d.el) return;
    if (!d.active && Math.hypot(e.clientX - d.startX, e.clientY - d.startY) < 4) return;
    d.active = true;
    const r = this.root.getBoundingClientRect();
    const x = e.clientX - r.left - d.size / 2, y = e.clientY - r.top - d.size / 2;
    d.el.classList.remove('animate');
    d.el.classList.add('dragging');
    d.el.style.transform = `translate(${x}px, ${y}px)`;
    const over = this.squareAt(e.clientX, e.clientY);
    this.squareEls.forEach((el, i) => el.classList.toggle('hover', i === over && this.targetsFrom(d.sq).includes(i)));
  }

  pointerUp(e) {
    const d = this.drag;
    this.drag = null;
    if (!d) return;
    this.squareEls.forEach((el) => el.classList.remove('hover'));
    if (d.el) d.el.classList.remove('dragging');
    if (!d.active) return;  // a click: keep the piece selected
    const sq = this.squareAt(e.clientX, e.clientY);
    if (sq !== null && sq !== d.sq && this.targetsFrom(d.sq).includes(sq)) {
      this.tryMove(d.sq, sq, false);
    } else {
      if (d.el) this.placeElement(d.el, d.sq, true);
      if (sq !== d.sq) this.selected = null;
      this.paintSquares();
    }
  }

  cancelDrag() {
    if (this.drag && this.drag.el) this.placeElement(this.drag.el, this.drag.sq, true);
    this.drag = null;
    this.paintSquares();
  }

  tryMove(from, to, animate = true) {
    const prefix = squareName(from) + squareName(to);
    const options = this.legal.filter((m) => m.startsWith(prefix));
    this.selected = null;
    this.paintSquares();
    if (options.length) this.onMove(prefix, options, animate);
  }

  // ---- Keyboard input --------------------------------------------------------
  keyDown(e) {
    this.keyboardMode = true;
    const step = { ArrowUp: [0, 1], ArrowDown: [0, -1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] }[e.key];
    if (step) {
      e.preventDefault();
      const flip = this.orientation === 'w' ? 1 : -1;
      const file = Math.min(7, Math.max(0, (this.cursor & 7) + step[0] * flip));
      const rank = Math.min(7, Math.max(0, (this.cursor >> 3) + step[1] * flip));
      this.cursor = rank * 8 + file;
      this.paintSquares();
      this.describeCursor();
      return;
    }
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      if (!this.interactive) return;
      if (this.selected !== null && this.targetsFrom(this.selected).includes(this.cursor)) {
        this.tryMove(this.selected, this.cursor);
      } else if (this.targetsFrom(this.cursor).length) {
        this.selected = this.cursor;
        this.paintSquares();
        this.announce(`${this.describe(this.cursor)} selected. Move to a highlighted square and press Enter.`);
      }
    }
    if (e.key === 'Escape') {
      this.selected = null;
      this.paintSquares();
    }
  }

  describe(sq) {
    const p = this.pieces[sq];
    if (!p) return `${squareName(sq)}, empty`;
    return `${squareName(sq)}, ${p === p.toUpperCase() ? 'white' : 'black'} ${PIECE_NAMES[p.toLowerCase()]}`;
  }

  describeCursor() {
    const isTarget = this.selected !== null && this.targetsFrom(this.selected).includes(this.cursor);
    this.announce(this.describe(this.cursor) + (isTarget ? ', legal move' : ''));
  }
}
