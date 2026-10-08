// evaluate.h: static evaluation. Every term is a packed (middlegame, endgame)
// score so it can be blended by game phase. The parameters live in one struct,
// which lets the tuner treat them as a flat array.
#pragma once

#include "position.h"

namespace bastion::Eval {

struct Params {
    Score pieceValue[6];
    Score psqt[6][64];  // indexed by square from white's point of view (a1 = 0)
    Score knightMobility[9];
    Score bishopMobility[14];
    Score rookMobility[15];
    Score queenMobility[28];
    Score bishopPair;
    Score bishopPawnsSameColor;   // per own pawn on the bishop's square color
    Score knightOutpost;
    Score bishopOutpost;
    Score rookOpenFile;
    Score rookSemiOpenFile;
    Score rookOnSeventh;
    Score doubledPawn;
    Score isolatedPawn;
    Score backwardPawn;
    Score connectedPawn[8];       // by relative rank
    Score passedPawn[8];          // by relative rank
    Score passedFreePath[8];      // passed pawn whose path to promotion is clear
    Score passedOwnKingDistance[8];
    Score passedEnemyKingDistance[8];
    Score kingAttackers[8];       // by number of enemy pieces attacking the king zone
    Score kingAttackWeight[6];    // per zone square attacked, by piece type
    Score safeCheck[6];           // safe checking moves available, by piece type
    Score kingShelter[8];         // own pawn in front of the king, by relative rank
    Score kingStorm[8];           // enemy pawn advancing toward the king, by relative rank
    Score kingOpenFile;           // no own pawn on the king's file
    Score threatByPawn[6];        // our pawn attacks an enemy piece of this type
    Score threatByMinor[6];
    Score threatByRook[6];
    Score threatByKing;
    Score hangingPiece;
    Score tempo;
};

constexpr int NumParams = int(sizeof(Params) / sizeof(Score));

extern Params P;

void  init();
Value evaluate(const Position& pos);

// For the tuner: per-parameter white/black usage counts of the last evaluation.
struct Trace {
    int   coeff[NumParams][2];
    int   phase;  // 0 (endgame) .. 24 (opening)
    int   scale;  // endgame scale factor, 128 = normal
    Score extra;  // untuned extras (mop-up), white's point of view
};
Value evaluate_trace(const Position& pos, Trace& trace);  // returns white's point of view

// Human readable breakdown, used by the "eval" UCI command.
std::string describe(const Position& pos);

}  // namespace bastion::Eval
