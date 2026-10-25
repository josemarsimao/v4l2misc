/*
 *  V4L2 video capture: My tests
 *
 *  Nome: Josemar Simão
 *  email: josemars@ifes.edu.br
 *
 *  This program can be used and distributed without restrictions.
 *
 *  This program is provided with the V4L2 API
 *  see http://linuxtv.org/docs.php for more information
 *
 *  ---------------------------------------------------------------------------
 *  camascii.cpp - IMAGEM (cv::Mat) COMO TEXTO PARA O TERMINAL (implementação)
 *  ---------------------------------------------------------------------------
 */

#include "camascii.h"
#include "v4l2term.h"           /// v4l2t_gray_ramp(): a MESMA rampa de caracteres do asciiart()

#include <cstring>
#include <algorithm>

/* Proporção de uma célula de texto: cada caractere tem ~16 pontos de altura por 8 de largura. */
#define HDOT 16
#define WDOT  8

/*
 * Veja camascii.h. Layout da tela gerada (sempre mTH linhas de mTW caracteres):
 *      linha 1       : título centralizado
 *      linha 2       : em branco
 *      próximas nch  : a imagem, centralizada na horizontal
 *      restante      : em branco
 */
void cam_asciiart_X(char* d_dest, int mTH, int mTW, const cv::Mat &frame, const char *title) {

    const unsigned char *mapchar = v4l2t_gray_ramp();   /* nível de cinza (0..255) -> caractere */
    char *pc = d_dest;                                  /* posição de escrita na tela de texto  */

    const int ih = frame.rows;
    const int iw = frame.cols;
    const int ch = frame.channels();                    /* 1 (cinza) ou 3 (cor) */

    int tt_sz = (int)strlen(title);
    int gapleft, gapright;
    int nch, ncw;                                       /* altura e largura do desenho, em caracteres */
    float rzH, rzW;                                     /* pixels da imagem por caractere (vertical/horizontal) */
    int i, j, l;

    if (tt_sz > mTW) {
        tt_sz = mTW;                                    /* título maior que a tela: corta, em vez de invadir a linha seguinte */
    }

    /// ---- Linha 1: título centralizado ----
    gapleft  = (mTW - tt_sz) / 2;
    gapright = mTW - tt_sz - gapleft;

    for (l = 0; l < gapleft; l++)  *pc++ = ' ';
    memcpy(pc, title, (size_t)tt_sz);
    pc += tt_sz;
    for (l = 0; l < gapright; l++) *pc++ = ' ';

    /// ---- Linha 2: em branco ----
    for (l = 0; l < mTW; l++) *pc++ = ' ';

    /* Formatos que sabemos desenhar e dimensões válidas; senão devolve só o cabeçalho. */
    if ((frame.type() != CV_8UC1 && frame.type() != CV_8UC3) || ih <= 0 || iw <= 0 || mTH < 5 || mTW < 5) {
        *pc = 0;
        return;
    }

    /// ---- Tamanho do desenho: usa a altura útil e mantém a proporção da imagem ----
    nch = mTH - 4;                                      /* linhas disponíveis */
    ncw = (iw * nch * HDOT) / (ih * WDOT);              /* colunas proporcionais */

    if (ncw > mTW - 4) {                                /* largo demais: limita pela largura e recalcula a altura */
        ncw = mTW - 4;
        nch = (ncw * ih * WDOT) / (iw * HDOT);
    }

    if (nch < 1 || ncw < 1) {
        *pc = 0;
        return;
    }

    rzH = (1.0f * ih) / nch;
    rzW = (1.0f * iw) / ncw;

    gapleft  = (mTW - ncw) / 2;
    gapright = mTW - ncw - gapleft;

    /// ---- Imagem: para cada caractere, lê um pixel da imagem e o traduz pela rampa ----
    for (i = 0; i < nch; i++) {

        /* início da linha de pixels correspondente a esta linha de texto */
        const unsigned char *row = frame.data + (size_t)(int(i * rzH)) * frame.step[0];

        for (l = 0; l < gapleft; l++) *pc++ = ' ';

        for (j = 0; j < ncw; j++) {
            /* pixel escolhido; na imagem colorida usa o 2º byte (canal G) como brilho */
            const unsigned char *q = row + (size_t)(int(j * rzW)) * ch + (ch == 3 ? 1 : 0);
            *pc++ = (char)mapchar[*q];
        }

        for (l = 0; l < gapright; l++) *pc++ = ' ';
    }

    /// ---- Completa a tela com linhas em branco até mTH linhas ----
    for (i = 0; i < (mTH - 2 - nch); i++) {
        for (l = 0; l < mTW; l++) *pc++ = ' ';
    }

    *pc = 0;                                            /* fecha a string */
}

/*
 * Propósito: desenhar a imagem em texto dentro de um retângulo rows x cols (sem título e sem
 * '\n'). Veja camascii.h. É a versão "em janela" da cam_asciiart_X: usa o retângulo inteiro,
 * em vez de reservar linhas de cabeçalho, e posiciona a imagem pelo halign / valign.
 */
void cam_asciiart_rect(char *d_dest, int rows, int cols, const cv::Mat &frame, float halign, float valign, int *used_rows, int *used_cols) {

    const unsigned char *mapchar = v4l2t_gray_ramp();   /* nível de cinza (0..255) -> caractere */
    const int ih = frame.rows;
    const int iw = frame.cols;
    const int ch = frame.channels();
    int nch, ncw;                                       /* altura e largura do desenho, em caracteres */
    int top, left;                                      /* canto do desenho dentro do retângulo */
    float rzH, rzW;                                     /* pixels da imagem por caractere */
    int i, j;

    if (used_rows) *used_rows = 0;
    if (used_cols) *used_cols = 0;
    if (rows <= 0 || cols <= 0) {
        if (d_dest) d_dest[0] = 0;
        return;
    }

    memset(d_dest, ' ', (size_t)rows * cols);           /* começa tudo em branco: o que não for imagem apaga o fundo */
    d_dest[(size_t)rows * cols] = 0;

    if ((frame.type() != CV_8UC1 && frame.type() != CV_8UC3) || ih <= 0 || iw <= 0)
        return;

    /* Tamanho do desenho: tenta usar toda a altura e confere a largura; senão limita pela largura. */
    nch = rows;
    ncw = (iw * nch * HDOT) / (ih * WDOT);
    if (ncw > cols) {
        ncw = cols;
        nch = (ncw * ih * WDOT) / (iw * HDOT);
    }
    if (nch < 1) nch = 1;
    if (ncw < 1) ncw = 1;
    if (nch > rows) nch = rows;

    rzH = (1.0f * ih) / nch;
    rzW = (1.0f * iw) / ncw;
    if (halign < 0) halign = 0;                         /* limita o alinhamento a 0..1 */
    if (halign > 1) halign = 1;
    if (valign < 0) valign = 0;
    if (valign > 1) valign = 1;
    top  = (int)((rows - nch) * valign + 0.5f);         /* espaço que sobra, repartido conforme o alinhamento */
    left = (int)((cols - ncw) * halign + 0.5f);

    for (i = 0; i < nch; i++) {
        const unsigned char *row = frame.data + (size_t)(int(i * rzH)) * frame.step[0];
        char *pc = d_dest + (size_t)(top + i) * cols + left;
        for (j = 0; j < ncw; j++) {
            const unsigned char *q = row + (size_t)(int(j * rzW)) * ch + (ch == 3 ? 1 : 0);
            *pc++ = (char)mapchar[*q];
        }
    }

    if (used_rows) *used_rows = nch;
    if (used_cols) *used_cols = ncw;
}
