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
 *  v4l2gray.c - CONVERSÃO DE PIXELS PARA TONS DE CINZA (implementação)
 *
 *  Veja v4l2gray.h. Todas as conversões recebem w, h e o pixel format por
 *  parâmetro e devolvem um buffer novo (malloc) com 1 byte por pixel, ou NULL.
 *  Frames curtos ou corrompidos nunca causam leitura fora do buffer: a parte
 *  que faltar é preenchida com preto (0).
 *  ---------------------------------------------------------------------------
 */

#include "v4l2gray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

/* ========================================================================== */
/*  MJPEG / JPEG                                                              */
/* ========================================================================== */

/* Gerenciador de erros da libjpeg estendido com um ponto de retorno (setjmp). */
struct gray_jpeg_err {
    struct jpeg_error_mgr pub;      /* tem que ser o PRIMEIRO campo: a libjpeg o converte de volta */
    jmp_buf               jump;     /* para onde voltar quando a libjpeg detectar erro            */
};

/*
 * Substitui o tratador de erro padrão da libjpeg, que chama exit().
 * Câmeras UVC entregam de vez em quando um frame MJPEG truncado; com o tratador
 * padrão isso encerrava o programa inteiro. Aqui voltamos ao ponto do setjmp.
 */
static void gray_jpeg_error_exit(j_common_ptr cinfo) {

    struct gray_jpeg_err *err = (struct gray_jpeg_err *)cinfo->err;

    longjmp(err->jump, 1);          /* "pula" de volta para v4l2g_mjpeg_to_gray() */
}

/* Silencia os avisos da libjpeg (ex.: "Corrupt JPEG data"): eles estragariam a tela de texto. */
static void gray_jpeg_emit_message(j_common_ptr cinfo, int msg_level) {

    (void)cinfo;
    (void)msg_level;
}

/*
 * Descomprime um frame MJPEG/JPEG direto para tons de cinza (libjpeg).
 * Devolve o buffer (altura x largura bytes) e grava as dimensões nos ponteiros out_h e out_w.
 * Em qualquer erro libera o que alocou e devolve NULL.
 */
unsigned char* v4l2g_mjpeg_to_gray(const unsigned char* jpeg_buffer, unsigned long jpeg_size,
                                   int *out_h, int *out_w) {

    struct jpeg_decompress_struct cinfo;
    struct gray_jpeg_err jerr;
    unsigned char * volatile gray = NULL;   /* volatile: é lido depois de um possível longjmp */
    int width, height;

    if (!jpeg_buffer || 0 == jpeg_size) {
        return NULL;
    }

    /* Liga o tratador de erro nosso (sem exit) à estrutura de decodificação. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit     = gray_jpeg_error_exit;
    jerr.pub.emit_message   = gray_jpeg_emit_message;

    if (setjmp(jerr.jump)) {
        /* Chegamos aqui por longjmp: a libjpeg encontrou um erro. Limpa e desiste deste frame. */
        jpeg_destroy_decompress(&cinfo);
        free(gray);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_buffer, jpeg_size);   /* a fonte é um buffer em memória, não um arquivo */

    if (JPEG_HEADER_OK != jpeg_read_header(&cinfo, TRUE)) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    cinfo.out_color_space = JCS_GRAYSCALE;          /* a própria libjpeg já entrega só o brilho */

    jpeg_start_decompress(&cinfo);

    width  = (int)cinfo.output_width;
    height = (int)cinfo.output_height;

    gray = (unsigned char *)malloc((size_t)width * (size_t)height);

    if (!gray) {
        jpeg_destroy_decompress(&cinfo);            /* abandona a decodificação em andamento */
        return NULL;
    }

    /* Lê linha por linha, escrevendo direto na posição certa do buffer final. */
    while (cinfo.output_scanline < cinfo.output_height) {

        unsigned char *row = gray + (size_t)cinfo.output_scanline * (size_t)width;

        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    *out_h = height;
    *out_w = width;

    return gray;
}

/* ========================================================================== */
/*  Formatos sem compressão                                                   */
/* ========================================================================== */

/*
 * YUYV (Y0 U Y1 V) e UYVY (U Y0 V Y1): a cada 2 bytes há 1 byte de brilho (Y).
 * No YUYV o Y está nos bytes de índice par; no UYVY, nos de índice ímpar
 * (a versão anterior tratava os dois como YUYV e, no UYVY, copiava o croma).
 */
unsigned char* v4l2g_yuyv_to_gray(const unsigned char* raw, unsigned long size,
                                  __u32 pxfmt, int w, int h) {

    unsigned long gray_size;
    unsigned long i, j = 0;
    unsigned long first;
    unsigned char *gray;

    if (!raw || w <= 0 || h <= 0) {
        return NULL;
    }

    gray_size = (unsigned long)w * (unsigned long)h;
    gray = (unsigned char *)malloc(gray_size);

    if (!gray) {
        return NULL;
    }

    first = (V4L2_PIX_FMT_UYVY == pxfmt) ? 1 : 0;   /* posição do primeiro Y dentro do grupo de 2 bytes */

    for (i = first; i < size && j < gray_size; i += 2) {
        gray[j++] = raw[i];                          /* pula 1 byte de croma a cada pixel */
    }

    if (j < gray_size) {
        memset(gray + j, 0, gray_size - j);          /* frame curto: completa com preto */
    }

    return gray;
}

/*
 * NV12 / YUV420 (planares): o plano Y vem inteiro no começo do buffer,
 * um byte por pixel. Basta copiar os primeiros w*h bytes.
 */
unsigned char* v4l2g_planar_to_gray(const unsigned char* raw, unsigned long size, int w, int h) {

    unsigned long gray_size;
    unsigned long n;
    unsigned char *gray;

    if (!raw || w <= 0 || h <= 0) {
        return NULL;
    }

    gray_size = (unsigned long)w * (unsigned long)h;
    gray = (unsigned char *)malloc(gray_size);

    if (!gray) {
        return NULL;
    }

    n = (size < gray_size) ? size : gray_size;       /* nunca lê além do que a câmera enviou */
    memcpy(gray, raw, n);

    if (n < gray_size) {
        memset(gray + n, 0, gray_size - n);
    }

    return gray;
}

/*
 * RGB24 / BGR24: 3 bytes por pixel. Calcula a luminância com inteiros
 * (pesos 77/256, 150/256 e 29/256 ~ 0,30 / 0,59 / 0,11).
 */
unsigned char* v4l2g_rgb_to_gray(const unsigned char* raw, unsigned long size,
                                 __u32 pxfmt, int w, int h) {

    unsigned long gray_size;
    unsigned long i = 0, j = 0;
    unsigned char *gray;

    if (!raw || w <= 0 || h <= 0) {
        return NULL;
    }

    gray_size = (unsigned long)w * (unsigned long)h;
    gray = (unsigned char *)malloc(gray_size);

    if (!gray) {
        return NULL;
    }

    while (i + 2 < size && j < gray_size) {          /* i + 2 < size: garante os 3 bytes do pixel */

        unsigned char r, g, b;

        if (V4L2_PIX_FMT_RGB24 == pxfmt) {
            r = raw[i];     g = raw[i + 1]; b = raw[i + 2];
        } else {                                     /* BGR24: ordem invertida */
            b = raw[i];     g = raw[i + 1]; r = raw[i + 2];
        }

        gray[j++] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
        i += 3;                                      /* próximo pixel */
    }

    if (j < gray_size) {
        memset(gray + j, 0, gray_size - j);
    }

    return gray;
}

/* ========================================================================== */
/*  Despachante                                                               */
/* ========================================================================== */

/* Informa se o pixel format tem conversão para cinza neste módulo. */
int v4l2g_is_supported(__u32 pxfmt) {

    switch (pxfmt) {
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_RGB24:
        case V4L2_PIX_FMT_BGR24:
            return 1;
        default:
            return 0;
    }
}

/*
 * Escolhe a conversão pelo pixel format e devolve SEMPRE 1 byte por pixel (ou NULL).
 *   - Comprimidos (MJPEG/JPEG): a imagem decodificada define o tamanho; *h e *w são atualizados.
 *   - Intercalados (YUYV/UYVY): pula os bytes de croma.
 *   - Planares (NV12/YUV420): copia o plano Y.
 *   - Cor direta (RGB24/BGR24): calcula a luminância.
 */
unsigned char* v4l2g_to_gray(__u32 pxfmt, const unsigned char* raw, unsigned long size,
                             int *h, int *w) {

    switch (pxfmt) {

        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return v4l2g_mjpeg_to_gray(raw, size, h, w);

        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            return v4l2g_yuyv_to_gray(raw, size, pxfmt, *w, *h);

        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_YUV420:
            return v4l2g_planar_to_gray(raw, size, *w, *h);

        case V4L2_PIX_FMT_RGB24:
        case V4L2_PIX_FMT_BGR24:
            return v4l2g_rgb_to_gray(raw, size, pxfmt, *w, *h);

        default:
            return NULL;                             /* formato sem conversão: quem chamou decide o que fazer */
    }
}

/* ========================================================================== */
/*  Legado didático: YUV -> RGB por tabelas (mantido sem alteração)           */
/* ========================================================================== */

/* Converte blocos de dados YUYV nativos em buffers RGB24 usando tabelas de busca fixas */
/// ***************************
#define POWER 15
#define pr (1 << POWER)
static const int kY  = (int) 1.164 * pr + 0.5;
static const int kRV = (int) 1.596 * pr + 0.5;
static const int kGU = (int) 0.391 * pr + 0.5;
static const int kGV = (int) 0.813 * pr + 0.5;
static const int kBU = (int) 2.018 * pr + 0.5;

static int coefY[256];
static int coefRV[256];
static int coefGU[256];
static int coefGV[256];
static int coefBU[256];

/* Pré-calcula coeficientes de cor para acelerar a conversão matemática de YUV para RGB */
void fill_lookuptables(){
    int i;
    for(i=0;i<256;i++){
        coefY[i] = kY * (i-16) + (pr/2);
        coefRV[i] = kRV * (i-128);
        coefGU[i] = -kGU * (i-128);
        coefGV[i] = -kGV * (i-128);
        coefBU[i] = kBU * (i-128);
    }
}

void yuv_to_rgb(const void *p_rgb, const void *p_yuv, int isize, int ih, int iw){

    unsigned char* p = (unsigned char*)p_rgb;
    unsigned char* q = (unsigned char*)p_yuv;

    for(int i=0;i<((ih * iw)/2);i++){

        /// First pixel
        p[0] = (coefY[q[0]] +      0       + coefRV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[0]] + coefGU[q[3]] + coefGV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[0]] + coefBU[q[3]] +      0      ) >> POWER;
        p++;
        /// Second pixel
        p[0] = (coefY[q[2]] +      0       + coefRV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[2]] + coefGU[q[3]] + coefGV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[2]] + coefBU[q[3]] +      0      ) >> POWER;
        p++;
        q+=4;
    }
}
/// ***************************



