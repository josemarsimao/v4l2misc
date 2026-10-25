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
 *  v4l2gray.h - CONVERSÃO DE PIXELS PARA TONS DE CINZA (8 bits)
 *
 *  Etapa 4 da reorganização. Isola aqui tudo que transforma o frame bruto da
 *  câmera em um buffer de brilho (1 byte por pixel), que é o que o asciiart
 *  precisa. É o ÚNICO módulo que depende da libjpeg (-ljpeg): os programas que
 *  não exibem imagem em modo texto (diagcam, multiview) não precisam dela.
 *
 *  Todas as funções recebem largura, altura e formato por PARÂMETRO (antes
 *  liam as globais w, h e pxfmt de v4l2misc.c), então podem ser usadas por
 *  qualquer câmera, inclusive no multicâmera.
 *
 *  Convenção: devolvem um buffer alocado com malloc() (quem chamou faz free())
 *  ou NULL em caso de falha. Nunca chamam exit() nem imprimem mensagens.
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2GRAY_H_INCLUDED
#define V4L2GRAY_H_INCLUDED

#include <linux/videodev2.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 se o pixel format tem conversão para cinza neste módulo, senão 0. */
int v4l2g_is_supported(__u32 pxfmt);

/* Descomprime um frame MJPEG/JPEG para cinza. As dimensões vêm do próprio JPEG
 * e são devolvidas em *out_h e *out_w. Frame corrompido -> NULL (sem derrubar o programa). */
unsigned char* v4l2g_mjpeg_to_gray(const unsigned char* jpeg_buffer, unsigned long jpeg_size,
                                   int *out_h, int *out_w);

/* Extrai o canal de brilho (Y) de formatos intercalados YUYV e UYVY. */
unsigned char* v4l2g_yuyv_to_gray(const unsigned char* raw, unsigned long size,
                                  __u32 pxfmt, int w, int h);

/* Copia o plano Y que já vem isolado no início de formatos planares (NV12 / YUV420). */
unsigned char* v4l2g_planar_to_gray(const unsigned char* raw, unsigned long size, int w, int h);

/* Converte RGB24 / BGR24 para cinza com aritmética inteira. */
unsigned char* v4l2g_rgb_to_gray(const unsigned char* raw, unsigned long size,
                                 __u32 pxfmt, int w, int h);

/* Despachante: escolhe a conversão pelo pixel format. Para MJPEG atualiza *h e *w
 * com o tamanho real da imagem; para os demais formatos usa *h e *w como entrada. */
unsigned char* v4l2g_to_gray(__u32 pxfmt, const unsigned char* raw, unsigned long size,
                             int *h, int *w);

/* --- Legado didático: conversão YUV -> RGB por tabelas (não usada no modo texto) --- */
void fill_lookuptables();
void yuv_to_rgb(const void *p_rgb, const void *p_yuv, int isize, int ih, int iw);

#ifdef __cplusplus
}
#endif

#endif /* V4L2GRAY_H_INCLUDED */
