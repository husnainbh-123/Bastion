// eval_params.h: evaluation weights.
//
// GENERATED FILE - written by tools/tuner from 1024029 self-play positions.
// Mean squared error 0.101634 -> 0.095206 (K = 1.3415). Tables are laid out
// visually: the first row is rank 8, a8..h8.
#pragma once

#include "evaluate.h"

namespace bastion::Eval {

inline void set_default_params(Params& p) {
    Score* base = reinterpret_cast<Score*>(&p);
    (void)base;
    static const Score pieceValue[6] = {
        S(  69,  91), S( 296, 298), S( 320, 339), S( 391, 564), S( 880, 965), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.pieceValue[i] = pieceValue[i];
    static const Score psqt[384] = {
        S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), 
        S( 237,  83), S(  64, 195), S(  41, 217), S( -24, 247), S(  31, 249), S( -81, 276), S(-154, 272), S( 252,   6), 
        S(  21,  44), S( -29,  58), S(  57,  36), S(  16,  50), S(  46,  50), S( 119,  -8), S(  31,  -5), S(  -2,  22), 
        S(  -1,  34), S(  10,  23), S(  -2,   1), S(   8,   3), S(  13,  27), S(   9,  -6), S(   4,   3), S(  -9,  18), 
        S(  -6,  21), S(   1,  15), S(   7,   6), S(   2,   8), S(  11,  -2), S(  16,   6), S(   4,   9), S( -14,  18), 
        S(  -6,  22), S( -24,  25), S( -11,  17), S( -19,  32), S( -23,  33), S(  -6,   9), S(   2,   8), S( -12,  10), 
        S(  -8,  23), S(  -6,  21), S( -17,  11), S( -12,  34), S( -25,  33), S(  12,  20), S(  16,  10), S(   2,   5), 
        S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), 

        S(-153, -76), S( -38, -34), S(  15, -49), S( -88,  35), S(  28,  -2), S(-214,  40), S(  99,-148), S(  17,-142), 
        S(  -9, -39), S(   1,   3), S(  13,  -7), S(  12, -10), S(  97, -55), S( -13,   6), S(   8, -61), S(  50,   9), 
        S( -33,   5), S(  17,  -2), S(  24,  20), S(  34,  27), S(  45,  18), S(  55, -43), S(  11,   7), S(   5, -22), 
        S(   8,  -9), S(  12,  -8), S(  -8,  32), S(  19,  14), S(  18,  39), S(  31,  22), S(  13,  18), S(  49, -61), 
        S(  16, -17), S( -14, -21), S(  -2,  43), S(  18,  33), S(  20,  32), S(  10,  20), S(   9, -23), S(  -9,   1), 
        S( -23,  -4), S( -26,  37), S(  -5,   0), S(   2,  12), S(  11,  14), S(  12,   9), S(  23, -15), S(  -7,  -4), 
        S(  -2, -72), S(  -3,  -9), S(  -4, -30), S(  -2,  14), S(  -2,  10), S(  12,  -4), S(   8, -67), S( -11, -26), 
        S(-325,  29), S(  -3, -33), S(  -2, -38), S( -22,  -5), S(  42, -74), S(   5, -11), S(  -1,  -6), S(  15,-120), 

        S(-106,  75), S(-133,  19), S(  17, -15), S(  59,  -2), S(-109,  40), S(-108,  21), S(-116,  66), S(  16,   0), 
        S( -69,   2), S( -29,  12), S(   5,  -3), S( -69,  34), S(  33,  -1), S( -45,  33), S( 118, -14), S( -99,  -1), 
        S( -39,  -2), S( -26,   2), S(  14,  22), S(  57, -30), S(  36,  39), S(  37,  29), S(  78, -61), S(  33,   2), 
        S(  -2,  22), S(  -2,  14), S(  -1,   0), S(  13,  15), S(  10,   1), S(  15, -12), S(  -5,  23), S( -28, -18), 
        S(  21, -24), S(  13,  18), S(   3,   6), S(   7,  12), S(   9,  21), S(   0,  19), S( -15,  29), S( -17,  -3), 
        S(   4, -17), S(  11,  -8), S(  10,   4), S(  -8,  29), S(  12,  19), S(   7,  23), S(  19,  -9), S(  -7,  -8), 
        S( -21,  22), S(   5, -24), S(  -8,  26), S(   0, -17), S(   1,  23), S(  12,   6), S(  23, -25), S(  15, -17), 
        S( -26, -36), S( -19,  17), S(   2, -11), S(  28,   8), S(  40,  -6), S( -13,  -3), S( -27,  23), S(  -4,  -5), 

        S( 110, -26), S( 115,  -6), S(  -3,  16), S(  -9,  32), S( 104,   0), S(  69,  -9), S(   9,  17), S( -40,  33), 
        S(  -9,  19), S(  15,  10), S(  -7,  30), S(   8,  10), S(  41,  31), S(  83,  -2), S(  52,   6), S(  12,  -1), 
        S(  -3,  22), S(  57,  -1), S(  17,  36), S(  65,   1), S(  66, -10), S(  42,   3), S(  81,   0), S( -12,  17), 
        S( -12,  15), S( -14,  25), S(  -7,  12), S(  39,   5), S(  71,  -4), S(  28,  16), S( -60,  56), S(  -2,   5), 
        S( -23,  19), S( -23,  27), S( -45,  37), S( -30,  37), S( -35,  24), S( -41,  35), S(   5,  10), S( -34,  19), 
        S( -59,  19), S(  -3,   7), S( -36,  32), S( -54,  36), S(   2, -15), S(  14,  -5), S(   1,  15), S( -25,   5), 
        S( -43,  24), S(   6,  10), S(  13, -30), S(  -4,   2), S( -20,   7), S(   4, -13), S(   1,   2), S( -30,  -8), 
        S( -34,   4), S( -27,   6), S( -22,  -1), S( -19,   7), S(  -7,  -7), S( -18,  -7), S( -32,   0), S(  -7, -33), 

        S(  19,  10), S( -44, 101), S(  20,  29), S(  90, -67), S(  21,  17), S( 133, -57), S(-131, 112), S(  82, -74), 
        S(  18, -10), S( -58, 147), S( -17,  78), S( -62, 135), S(  68,  40), S( -57, 164), S(  12,   2), S(  69,  22), 
        S(  11,  15), S( -17,  58), S( -29,  60), S( -28,  89), S( -31,  84), S(  32, -22), S( -34, 132), S(  18,   7), 
        S(   2,  -7), S( -41, 154), S( -43, 117), S(  -9,  40), S( -22,  93), S( -28,  79), S(  -3,  23), S(   1,  -3), 
        S(  -8,  45), S( -40,  47), S( -25,  72), S( -13,  75), S( -24,  91), S(  -6,  10), S(  -1,  24), S( -16,  27), 
        S( -27,  57), S(  16, -40), S(  -2,  47), S(  -1,  52), S(   4,  -2), S(  17,  -1), S(   7, -25), S(  44,-190), 
        S( -17, -42), S(  18, -64), S(  15, -43), S(   0,   5), S(   8, -33), S(  16, -49), S(  43, -40), S( -33, -62), 
        S(  21,-116), S(  24, -41), S(   5, -88), S(   9, -47), S(  12, -43), S( -13, -15), S( 111,-329), S( -39,-101), 

        S(-166,-132), S( 245, -15), S(  97,  22), S(  13,  17), S(-147,   3), S( 175, -57), S( 206, -53), S(  94, -75), 
        S(  23,   1), S( -82,  54), S(  36,  16), S(-126,  37), S(  11, -22), S(-253,  54), S(  12,   1), S(-118,  26), 
        S( 153, -14), S(-101,  66), S(  43,   1), S( -91,  33), S(  -8,  -1), S(-132,  54), S(-144,  28), S( -46,  29), 
        S(  -8, -40), S( -92,  22), S( -28,  22), S( -34,  13), S(  94,  11), S( -28,  24), S( -30,  27), S(  32, -24), 
        S(-122,  18), S(  82,   0), S(-123,  22), S(  27,  12), S(  14,   8), S(   7,   8), S( -29,   5), S( -51, -29), 
        S(  -1, -27), S(  17,   1), S( -77,  29), S( -60,  23), S( -45,  26), S( -50,  23), S(   2, -12), S( -20, -13), 
        S( -45,   0), S( -96,  47), S(  18,  -1), S( -46,  15), S( -51,  20), S(   7,   4), S(  10,   7), S(  -5,  -6), 
        S(-130,  23), S(  18,   7), S(   4,   7), S( -26, -20), S(  11, -28), S( -16,  -4), S(  50, -25), S(  32, -43), 
    };
    for (int b = 0; b < 6; ++b)
        for (int s = 0; s < 64; ++s) p.psqt[b][s] = psqt[b * 64 + (s ^ 56)];
    static const Score knightMobility[9] = {
        S( -27,   1), S( -21, -28), S( -10,   1), S(  -2,  12), S(   3,  21), S(   6,  25), S(  12,  24), S(  18,  21), 
        S(  34,  -6), 
    };
    for (int i = 0; i < 9; ++i) p.knightMobility[i] = knightMobility[i];
    static const Score bishopMobility[14] = {
        S( -23, -41), S( -17, -37), S( -11,  -6), S(  -6,   3), S(  -5,  14), S(  -3,  32), S(  -4,  30), S(   3,  35), 
        S(   7,  33), S(   6,  38), S(  45,  -3), S(  22,  35), S( 155, -84), S( -90,  61), 
    };
    for (int i = 0; i < 14; ++i) p.bishopMobility[i] = bishopMobility[i];
    static const Score rookMobility[15] = {
        S( -87,  12), S( -25, -41), S(  -9, -16), S(  -4,  -9), S(  -1,   2), S(   3,   5), S(   2,  12), S(   8,   6), 
        S(   7,  22), S(  17,  23), S(  12,  23), S(  20,  33), S(  33,  24), S(  39,  20), S(  58,  18), 
    };
    for (int i = 0; i < 15; ++i) p.rookMobility[i] = rookMobility[i];
    static const Score queenMobility[28] = {
        S( -12, -45), S( -19,  90), S( -19,  52), S( -18,  -6), S( -15,  64), S( -17,   2), S( -12,  38), S(  -9,  19), 
        S(  -6,  38), S( -11,  79), S( -13,  63), S(  -8,  61), S(  -8,  69), S(  -5,  65), S(  -7,  73), S(  -3,  55), 
        S( -10,  59), S(   9,  42), S(  -8,  77), S(  17,   3), S(  57, -48), S(  62, -53), S(  66, -20), S( 204,-201), 
        S( 251,-191), S(  89,-150), S(  73, -15), S(-133,-400), 
    };
    for (int i = 0; i < 28; ++i) p.queenMobility[i] = queenMobility[i];
    p.bishopPair = S(  10,  25);
    p.bishopPawnsSameColor = S(  -1,  -6);
    p.knightOutpost = S(  24,  15);
    p.bishopOutpost = S(  19,  -2);
    p.rookOpenFile = S(  19,  -9);
    p.rookSemiOpenFile = S(  13,   2);
    p.rookOnSeventh = S(  14,  13);
    p.doubledPawn = S( -13,   6);
    p.isolatedPawn = S(  -3, -12);
    p.backwardPawn = S(   1, -13);
    static const Score connectedPawn[8] = {
        S(   0,   0), S(   6,  -3), S(  17,  -1), S(   5,   8), S(   7,  18), S(   8,  47), S(   3, 213), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.connectedPawn[i] = connectedPawn[i];
    static const Score passedPawn[8] = {
        S(   0,   0), S(   3, -29), S( -22,  -4), S( -10,  27), S(  12,  47), S( -12,  70), S(  11, -61), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.passedPawn[i] = passedPawn[i];
    static const Score passedFreePath[8] = {
        S(   0,   0), S( -60,  19), S( -10,   4), S(-119,  40), S( -39,  37), S( -15,  82), S( -56, 179), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.passedFreePath[i] = passedFreePath[i];
    static const Score passedOwnKingDistance[8] = {
        S( -10,  67), S(  -3,  50), S(  -2,  25), S(  18,   1), S(  -8,  -2), S(   5,  -6), S(   0,  -2), S( -19,  18), 
    };
    for (int i = 0; i < 8; ++i) p.passedOwnKingDistance[i] = passedOwnKingDistance[i];
    static const Score passedEnemyKingDistance[8] = {
        S( -20, -30), S(  -4, -20), S(   0, -12), S(   6,   6), S(  -7,  34), S(   2,  43), S(   3,  52), S(  -5,  48), 
    };
    for (int i = 0; i < 8; ++i) p.passedEnemyKingDistance[i] = passedEnemyKingDistance[i];
    static const Score kingAttackers[8] = {
        S(   3,   7), S(  -7,  -1), S( -10,  -5), S(   3, -20), S(  53, -64), S(  99, 245), S(-156,   0), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.kingAttackers[i] = kingAttackers[i];
    static const Score kingAttackWeight[6] = {
        S(   0,   0), S(   5,   3), S(   7,   0), S(   5,   2), S(   6,  18), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.kingAttackWeight[i] = kingAttackWeight[i];
    static const Score safeCheck[6] = {
        S(   0,   0), S(  36,  -1), S(   3,   8), S(  27,   9), S(  10,  21), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.safeCheck[i] = safeCheck[i];
    static const Score kingShelter[8] = {
        S(  -1,   0), S(   9, -10), S(  -2,  -2), S(  -7,   0), S(  -8,   3), S( -37,  21), S(  -5,  72), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.kingShelter[i] = kingShelter[i];
    static const Score kingStorm[8] = {
        S(   5, -30), S(-305, 217), S(   5,  23), S(  14,  -4), S(  16, -19), S(  16, -27), S(  22, -33), S(   0,   0), 
    };
    for (int i = 0; i < 8; ++i) p.kingStorm[i] = kingStorm[i];
    p.kingOpenFile = S( -23,   2);
    static const Score threatByPawn[6] = {
        S(   0,   0), S(  33,  45), S(  30,  50), S(  66,  14), S(  15,  42), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.threatByPawn[i] = threatByPawn[i];
    static const Score threatByMinor[6] = {
        S(  -2,   6), S(  10,  21), S(  20,  29), S(  30,  23), S(  14,  54), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.threatByMinor[i] = threatByMinor[i];
    static const Score threatByRook[6] = {
        S(  -8,  13), S(   9,  -1), S(   2,   7), S(  15,  94), S(  39,  24), S(   0,   0), 
    };
    for (int i = 0; i < 6; ++i) p.threatByRook[i] = threatByRook[i];
    p.threatByKing = S(   8,  15);
    p.hangingPiece = S(  17,  22);
    p.tempo = S(  13,   9);
}

}  // namespace bastion::Eval
