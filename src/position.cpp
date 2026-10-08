// position.cpp: FEN parsing, making/unmaking moves, legality checks and
// static exchange evaluation.
#include "position.h"

#include <cctype>
#include <cstring>
#include <sstream>

#include "movegen.h"

namespace bastion {

namespace Zobrist {
Key psq[12][64];
Key enpassant[8];
Key castling[16];
Key side;

void init() {
    std::uint64_t s    = 0x6A09E667F3BCC909ULL;  // fixed seed: keys are identical every run
    auto          next = [&s]() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    };
    for (auto& piece : psq)
        for (Key& k : piece) k = next();
    for (Key& k : enpassant) k = next();
    for (Key& k : castling) k = next();
    castling[0] = 0;
    side        = next();
}
}  // namespace Zobrist

namespace {

// Tables are built once, on first use, so a Position can be created anywhere.
void ensure_tables() {
    static const bool done = (init_bitboards(), Zobrist::init(), true);
    (void)done;
}

constexpr const char* PieceChars = "PNBRQKpnbrqk";

// Exchange values used by SEE; NO_PIECE_TYPE maps to 0.
constexpr int SeeValue[7] = {100, 320, 330, 500, 950, 0, 0};

}  // namespace

// ---------------------------------------------------------------------------
// Piece placement helpers
// ---------------------------------------------------------------------------
void Position::put_piece(Piece pc, Square s) {
    board[s] = pc;
    byType[type_of(pc)] |= square_bb(s);
    byColor[color_of(pc)] |= square_bb(s);
}

void Position::remove_piece(Square s) {
    Piece pc = board[s];
    byType[type_of(pc)] ^= square_bb(s);
    byColor[color_of(pc)] ^= square_bb(s);
    board[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    Piece    pc     = board[from];
    Bitboard fromTo = square_bb(from) | square_bb(to);
    byType[type_of(pc)] ^= fromTo;
    byColor[color_of(pc)] ^= fromTo;
    board[from] = NO_PIECE;
    board[to]   = pc;
}

// ---------------------------------------------------------------------------
// FEN
// ---------------------------------------------------------------------------
bool Position::set(const std::string& fen, std::string* error) {
    ensure_tables();
    std::string err;
    if (set_internal(fen, err)) return true;
    if (error) *error = err;
    set_internal(StartFEN, err);
    return false;
}

bool Position::set_internal(const std::string& fen, std::string& error) {
    std::fill(std::begin(board), std::end(board), NO_PIECE);
    std::fill(std::begin(byType), std::end(byType), 0);
    std::fill(std::begin(byColor), std::end(byColor), 0);
    states.clear();
    states.reserve(1024);
    states.emplace_back();
    StateInfo& si = states.back();
    std::memset(&si, 0, sizeof(si));
    si.ep       = SQ_NONE;
    si.captured = NO_PIECE;

    std::istringstream ss(fen);
    std::string        placement, stm, castling = "-", ep = "-";
    int                rule50 = 0, fullmove = 1;
    if (!(ss >> placement >> stm)) {
        error = "FEN needs at least piece placement and side to move";
        return false;
    }
    ss >> castling >> ep;
    if (!(ss >> rule50)) rule50 = 0;
    if (!(ss >> fullmove)) fullmove = 1;

    // 1. Piece placement, rank 8 down to rank 1.
    int file = 0, rank = 7;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) {
                error = "bad rank layout in FEN";
                return false;
            }
            file = 0;
            --rank;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) {
                error = "too many squares in a rank";
                return false;
            }
        } else {
            const char* p = std::strchr(PieceChars, c);
            if (!p || c == '\0' || file > 7) {
                error = std::string("unexpected character in FEN: ") + c;
                return false;
            }
            put_piece(Piece(p - PieceChars), make_square(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) {
        error = "FEN does not describe 64 squares";
        return false;
    }

    // 2. Side to move.
    if (stm != "w" && stm != "b") {
        error = "side to move must be 'w' or 'b'";
        return false;
    }
    sideToMove = stm == "w" ? WHITE : BLACK;

    // Sanity: exactly one king each, no pawns on the back ranks.
    if (count(WHITE, KING) != 1 || count(BLACK, KING) != 1) {
        error = "each side needs exactly one king";
        return false;
    }
    if (byType[PAWN] & (Rank1BB | Rank8BB)) {
        error = "pawns cannot stand on the first or last rank";
        return false;
    }
    if (popcount(byColor[WHITE]) > 16 || popcount(byColor[BLACK]) > 16) {
        error = "too many pieces";
        return false;
    }

    // 3. Castling rights; silently drop rights that the piece placement contradicts.
    std::fill(std::begin(castlingRightsMask), std::end(castlingRightsMask), 0);
    castlingRightsMask[SQ_E1] = WHITE_OO | WHITE_OOO;
    castlingRightsMask[SQ_H1] = WHITE_OO;
    castlingRightsMask[SQ_A1] = WHITE_OOO;
    castlingRightsMask[SQ_E8] = BLACK_OO | BLACK_OOO;
    castlingRightsMask[SQ_H8] = BLACK_OO;
    castlingRightsMask[SQ_A8] = BLACK_OOO;
    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
                case 'K': si.castling |= WHITE_OO; break;
                case 'Q': si.castling |= WHITE_OOO; break;
                case 'k': si.castling |= BLACK_OO; break;
                case 'q': si.castling |= BLACK_OOO; break;
                default: error = "bad castling field"; return false;
            }
        }
    }
    const Piece WK = make_piece(WHITE, KING), WR = make_piece(WHITE, ROOK);
    const Piece BK = make_piece(BLACK, KING), BR = make_piece(BLACK, ROOK);
    if (board[SQ_E1] != WK || board[SQ_H1] != WR) si.castling &= ~WHITE_OO;
    if (board[SQ_E1] != WK || board[SQ_A1] != WR) si.castling &= ~WHITE_OOO;
    if (board[SQ_E8] != BK || board[SQ_H8] != BR) si.castling &= ~BLACK_OO;
    if (board[SQ_E8] != BK || board[SQ_A8] != BR) si.castling &= ~BLACK_OOO;

    // 4. En passant square: only kept if a capture is actually possible.
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || (ep[1] != '3' && ep[1] != '6')) {
            error = "bad en passant square";
            return false;
        }
        Square epsq = make_square(ep[0] - 'a', ep[1] - '1');
        Color  them = sideToMove ^ 1;
        bool   ok   = relative_rank(sideToMove, epsq) == RANK_6 && empty(epsq) &&
                    empty(epsq + pawn_push(sideToMove)) &&
                    board[epsq - pawn_push(sideToMove)] == make_piece(them, PAWN) &&
                    (PawnAttacks[them][epsq] & pieces(sideToMove, PAWN));
        if (ok) si.ep = epsq;
    }

    si.rule50 = std::max(0, std::min(rule50, 1000));
    gamePly   = std::max(2 * (std::min(fullmove, 100000) - 1), 0) + (sideToMove == BLACK);

    // Keys
    si.key = si.pawnKey = 0;
    for (Square s = 0; s < 64; ++s)
        if (board[s] != NO_PIECE) {
            si.key ^= Zobrist::psq[board[s]][s];
            if (type_of(board[s]) == PAWN) si.pawnKey ^= Zobrist::psq[board[s]][s];
        }
    if (si.ep != SQ_NONE) si.key ^= Zobrist::enpassant[file_of(si.ep)];
    si.key ^= Zobrist::castling[si.castling];
    if (sideToMove == BLACK) si.key ^= Zobrist::side;

    // The side that just moved must not be in check (its king could be captured).
    if (attackers_to(king_square(sideToMove ^ 1)) & byColor[sideToMove]) {
        error = "side not to move is in check";
        return false;
    }
    si.checkers = attackers_to(king_square(sideToMove)) & byColor[sideToMove ^ 1];
    if (popcount(si.checkers) > 2) {
        error = "impossible check";
        return false;
    }
    set_check_info(si);
    si.repetition = 0;
    return true;
}

std::string Position::fen() const {
    std::string out;
    for (int r = 7; r >= 0; --r) {
        int emptyCount = 0;
        for (int f = 0; f <= 7; ++f) {
            Piece pc = board[make_square(f, r)];
            if (pc == NO_PIECE) {
                ++emptyCount;
                continue;
            }
            if (emptyCount) out += char('0' + emptyCount);
            emptyCount = 0;
            out += PieceChars[pc];
        }
        if (emptyCount) out += char('0' + emptyCount);
        if (r) out += '/';
    }
    out += sideToMove == WHITE ? " w " : " b ";
    int cr = castling_rights();
    if (!cr) out += '-';
    if (cr & WHITE_OO) out += 'K';
    if (cr & WHITE_OOO) out += 'Q';
    if (cr & BLACK_OO) out += 'k';
    if (cr & BLACK_OOO) out += 'q';
    out += ' ' + square_name(ep_square());
    out += ' ' + std::to_string(rule50_count()) + ' ' + std::to_string(1 + gamePly / 2);
    return out;
}

std::string Position::pretty() const {
    std::string out = "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f <= 7; ++f) {
            Piece pc = board[make_square(f, r)];
            out += " | ";
            out += pc == NO_PIECE ? ' ' : PieceChars[pc];
        }
        out += " | " + std::to_string(r + 1) + "\n +---+---+---+---+---+---+---+---+\n";
    }
    out += "   a   b   c   d   e   f   g   h\n\nFen: " + fen() + "\n";
    std::ostringstream key;
    key << std::hex << std::uppercase << this->key();
    out += "Key: " + key.str() + "\n";
    return out;
}

// ---------------------------------------------------------------------------
// Attacks, pins and checks
// ---------------------------------------------------------------------------
Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
           (KnightAttacks[s] & byType[KNIGHT]) | (rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN])) |
           (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN])) | (KingAttacks[s] & byType[KING]);
}

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {
    Bitboard blockers = 0;
    pinners           = 0;
    Bitboard snipers  = ((rook_attacks(s, 0) & (byType[ROOK] | byType[QUEEN])) |
                        (bishop_attacks(s, 0) & (byType[BISHOP] | byType[QUEEN]))) &
                       sliders;
    Bitboard occupancy = occupied() ^ snipers;
    while (snipers) {
        Square   sniperSq = pop_lsb(snipers);
        Bitboard b        = BetweenBB[s][sniperSq] & occupancy;
        if (b && !more_than_one(b)) {
            blockers |= b;
            if (b & byColor[color_of(board[s])]) pinners |= square_bb(sniperSq);
        }
    }
    return blockers;
}

void Position::set_check_info(StateInfo& si) const {
    si.blockers[WHITE] = slider_blockers(byColor[BLACK], king_square(WHITE), si.pinners[BLACK]);
    si.blockers[BLACK] = slider_blockers(byColor[WHITE], king_square(BLACK), si.pinners[WHITE]);

    Square ksq             = king_square(sideToMove ^ 1);
    si.checkSquares[PAWN]   = PawnAttacks[sideToMove ^ 1][ksq];
    si.checkSquares[KNIGHT] = KnightAttacks[ksq];
    si.checkSquares[BISHOP] = bishop_attacks(ksq, occupied());
    si.checkSquares[ROOK]   = rook_attacks(ksq, occupied());
    si.checkSquares[QUEEN]  = si.checkSquares[BISHOP] | si.checkSquares[ROOK];
    si.checkSquares[KING]   = 0;
}

bool Position::legal(Move m) const {
    Color  us = sideToMove, them = us ^ 1;
    Square from = m.from(), to = m.to();
    Square ksq = king_square(us);

    if (m.type() == EN_PASSANT) {
        // Removing two pawns from one rank can expose the king along that rank or a diagonal.
        Square   capsq = to - pawn_push(us);
        Bitboard occ   = (occupied() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
        return !(rook_attacks(ksq, occ) & straight_sliders(them)) && !(bishop_attacks(ksq, occ) & diagonal_sliders(them));
    }
    // Castling is fully checked during generation.
    if (m.type() == CASTLING) return true;

    // The king may not step onto an attacked square (remove it so it can't hide behind itself).
    if (type_of(board[from]) == KING) return !(attackers_to(to, occupied() ^ square_bb(from)) & byColor[them]);

    // A pinned piece may only move along the pin line.
    return !(st().blockers[us] & square_bb(from)) || aligned(from, to, ksq);
}

bool Position::pseudo_legal(Move m) const {
    if (!m.is_ok()) return false;
    // Special moves are rare enough to validate against the full legal move list.
    if (m.type() != NORMAL) return MoveList<LEGAL>(*this).contains(m);

    Color  us = sideToMove;
    Square from = m.from(), to = m.to();
    Piece  pc = board[from];

    if (m.promotion_type() != KNIGHT) return false;  // promotion bits must be clear
    if (pc == NO_PIECE || color_of(pc) != us) return false;
    if (byColor[us] & square_bb(to)) return false;

    if (type_of(pc) == PAWN) {
        if ((Rank1BB | Rank8BB) & square_bb(to)) return false;  // would have to be a promotion
        bool capture    = PawnAttacks[us][from] & byColor[us ^ 1] & square_bb(to);
        bool singlePush = from + pawn_push(us) == to && empty(to);
        bool doublePush = from + 2 * pawn_push(us) == to && relative_rank(us, from) == RANK_2 && empty(to) &&
                          empty(to - pawn_push(us));
        if (!capture && !singlePush && !doublePush) return false;
    } else if (!(attacks_bb(type_of(pc), from, occupied()) & square_bb(to)))
        return false;

    // When in check, the move must deal with it (full legality is checked later by legal()).
    if (checkers()) {
        if (type_of(pc) != KING) {
            if (more_than_one(checkers())) return false;
            Square checkSq = lsb(checkers());
            if (!((BetweenBB[king_square(us)][checkSq] | checkers()) & square_bb(to))) return false;
        } else if (attackers_to(to, occupied() ^ square_bb(from)) & byColor[us ^ 1])
            return false;
    }
    return true;
}

bool Position::gives_check(Move m) const {
    Color  us = sideToMove;
    Square from = m.from(), to = m.to();
    Square ksq = king_square(us ^ 1);

    // Direct check
    if (st().checkSquares[type_of(board[from])] & square_bb(to)) return true;
    // Discovered check: a piece of ours that was shielding their king moves off the line.
    if ((st().blockers[us ^ 1] & square_bb(from)) && !aligned(from, to, ksq)) return true;

    switch (m.type()) {
        case NORMAL: return false;
        case PROMOTION: return attacks_bb(m.promotion_type(), to, occupied() ^ square_bb(from)) & square_bb(ksq);
        case EN_PASSANT: {
            Square   capsq = make_square(file_of(to), rank_of(from));
            Bitboard b     = (occupied() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
            return (rook_attacks(ksq, b) & straight_sliders(us)) | (bishop_attacks(ksq, b) & diagonal_sliders(us));
        }
        case CASTLING: {
            Square rfrom = to > from ? from + 3 : from - 4;
            Square rto   = to > from ? from + 1 : from - 1;
            Bitboard occ = (occupied() ^ square_bb(from) ^ square_bb(rfrom)) | square_bb(to) | square_bb(rto);
            return rook_attacks(rto, occ) & square_bb(ksq);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Making and unmaking moves
// ---------------------------------------------------------------------------
void Position::do_move(Move m) {
    const StateInfo& prev = states.back();
    StateInfo        next;
    next.key           = prev.key ^ Zobrist::side;
    next.pawnKey       = prev.pawnKey;
    next.castling      = prev.castling;
    next.ep            = SQ_NONE;
    next.rule50        = prev.rule50 + 1;
    next.pliesFromNull = prev.pliesFromNull + 1;
    if (prev.ep != SQ_NONE) next.key ^= Zobrist::enpassant[file_of(prev.ep)];

    Color  us = sideToMove, them = us ^ 1;
    Square from = m.from(), to = m.to();
    Piece  pc       = board[from];
    Piece  captured = m.type() == EN_PASSANT ? make_piece(them, PAWN) : board[to];

    if (m.type() == CASTLING) {
        Square rfrom = to > from ? from + 3 : from - 4;
        Square rto   = to > from ? from + 1 : from - 1;
        Piece  rook  = make_piece(us, ROOK);
        move_piece(from, to);
        move_piece(rfrom, rto);
        next.key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to] ^ Zobrist::psq[rook][rfrom] ^ Zobrist::psq[rook][rto];
        captured = NO_PIECE;
    } else {
        if (captured != NO_PIECE) {
            Square capsq = m.type() == EN_PASSANT ? to - pawn_push(us) : to;
            remove_piece(capsq);
            next.key ^= Zobrist::psq[captured][capsq];
            if (type_of(captured) == PAWN) next.pawnKey ^= Zobrist::psq[captured][capsq];
            next.rule50 = 0;
        }
        move_piece(from, to);
        next.key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

        if (type_of(pc) == PAWN) {
            next.rule50 = 0;
            next.pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
            if ((to ^ from) == 16) {
                // Only record the en passant square if a capture is possible.
                Square epsq = from + pawn_push(us);
                if (PawnAttacks[us][epsq] & pieces(them, PAWN)) {
                    next.ep = epsq;
                    next.key ^= Zobrist::enpassant[file_of(epsq)];
                }
            } else if (m.type() == PROMOTION) {
                Piece promo = make_piece(us, m.promotion_type());
                remove_piece(to);
                put_piece(promo, to);
                next.key ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promo][to];
                next.pawnKey ^= Zobrist::psq[pc][to];
            }
        }
    }

    int lost = castlingRightsMask[from] | castlingRightsMask[to];
    if (next.castling && lost) {
        next.key ^= Zobrist::castling[next.castling];
        next.castling &= ~lost;
        next.key ^= Zobrist::castling[next.castling];
    }

    next.captured = captured;
    sideToMove    = them;
    ++gamePly;

    next.checkers = attackers_to(king_square(them)) & byColor[us];
    set_check_info(next);
    compute_repetition(next);
    states.push_back(next);
}

void Position::undo_move(Move m) {
    sideToMove ^= 1;
    Color            us = sideToMove;
    Square           from = m.from(), to = m.to();
    const StateInfo& si = states.back();

    if (m.type() == CASTLING) {
        Square rfrom = to > from ? from + 3 : from - 4;
        Square rto   = to > from ? from + 1 : from - 1;
        move_piece(to, from);
        move_piece(rto, rfrom);
    } else {
        if (m.type() == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (si.captured != NO_PIECE) put_piece(si.captured, m.type() == EN_PASSANT ? to - pawn_push(us) : to);
    }
    states.pop_back();
    --gamePly;
}

void Position::do_null_move() {
    StateInfo next = states.back();
    if (next.ep != SQ_NONE) {
        next.key ^= Zobrist::enpassant[file_of(next.ep)];
        next.ep = SQ_NONE;
    }
    next.key ^= Zobrist::side;
    next.rule50++;
    next.pliesFromNull = 0;
    next.captured      = NO_PIECE;
    next.repetition    = 0;
    sideToMove ^= 1;
    ++gamePly;
    next.checkers = 0;  // null moves are never made while in check
    set_check_info(next);
    states.push_back(next);
}

void Position::undo_null_move() {
    states.pop_back();
    sideToMove ^= 1;
    --gamePly;
}

void Position::compute_repetition(StateInfo& si) const {
    // states.back() is the position before this move; si is not yet pushed.
    si.repetition = 0;
    int end       = std::min(si.rule50, si.pliesFromNull);
    int n         = int(states.size());  // index si will get
    for (int i = 4; i <= end && n - i >= 0; i += 2) {
        const StateInfo& old = states[n - i];
        if (old.key == si.key) {
            si.repetition = old.repetition ? -i : i;
            break;
        }
    }
}

Key Position::key_after(Move m) const {
    Square from = m.from(), to = m.to();
    Piece  pc = board[from], captured = board[to];
    Key    k  = st().key ^ Zobrist::side;
    if (captured != NO_PIECE && m.type() != CASTLING) k ^= Zobrist::psq[captured][to];
    return k ^ Zobrist::psq[pc][to] ^ Zobrist::psq[pc][from];
}

// ---------------------------------------------------------------------------
// Draws
// ---------------------------------------------------------------------------
bool Position::is_insufficient_material() const {
    if (byType[PAWN] | byType[ROOK] | byType[QUEEN]) return false;
    Bitboard minors = byType[KNIGHT] | byType[BISHOP];
    if (!more_than_one(minors)) return true;  // K v K, K+minor v K
    // Only bishops, all on the same color of square.
    if (!byType[KNIGHT]) {
        Bitboard b = byType[BISHOP];
        return !(b & DarkSquares) || !(b & ~DarkSquares);
    }
    return false;
}

bool Position::is_draw(int ply) const {
    if (st().rule50 > 99 && (!checkers() || MoveList<LEGAL>(*this).size())) return true;
    return is_repetition_draw(ply) || is_insufficient_material();
}

// ---------------------------------------------------------------------------
// Static exchange evaluation (swap algorithm with x-rays)
// ---------------------------------------------------------------------------
bool Position::see_ge(Move m, int threshold) const {
    if (m.type() != NORMAL) return 0 >= threshold;

    Square from = m.from(), to = m.to();
    int    swap = SeeValue[type_of(board[to])] - threshold;
    if (swap < 0) return false;
    swap = SeeValue[type_of(board[from])] - swap;
    if (swap <= 0) return true;

    Bitboard occ       = occupied() ^ square_bb(from) ^ square_bb(to);
    Color    stm       = sideToMove;
    Bitboard attackers = attackers_to(to, occ);
    Bitboard stmAttackers, bb;
    int      res = 1;

    const Bitboard diag     = byType[BISHOP] | byType[QUEEN];
    const Bitboard straight = byType[ROOK] | byType[QUEEN];

    while (true) {
        stm ^= 1;
        attackers &= occ;
        if (!(stmAttackers = attackers & byColor[stm])) break;
        // Pinned pieces cannot recapture while their pinner is still on the board.
        if (st().pinners[stm ^ 1] & occ) stmAttackers &= ~st().blockers[stm];
        if (!stmAttackers) break;
        res ^= 1;

        // Recapture with the least valuable attacker, revealing x-ray attackers behind it.
        if ((bb = stmAttackers & byType[PAWN])) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & diag;
        } else if ((bb = stmAttackers & byType[KNIGHT])) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
        } else if ((bb = stmAttackers & byType[BISHOP])) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & diag;
        } else if ((bb = stmAttackers & byType[ROOK])) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= rook_attacks(to, occ) & straight;
        } else if ((bb = stmAttackers & byType[QUEEN])) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & diag) | (rook_attacks(to, occ) & straight);
        } else  // King: can only recapture if the other side has no attackers left.
            return (attackers & ~byColor[stm]) ? res ^ 1 : res;
    }
    return bool(res);
}

}  // namespace bastion
