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
 *  v4l2misc.h - BIBLIOTECA DE CÂMERA ÚNICA (fachada) + MODO TEXTO
 *
 *  Etapa 4: v4l2misc deixou de ser o "depósito" de tudo. Ficou só com o que é
 *  específico de UMA câmera usada por um programa de thread única (icap):
 *  abrir, configurar, capturar e entregar cada frame a process_image().
 *
 *  O restante foi separado em módulos com uma responsabilidade cada:
 *      v4l2core.h  - motor de captura (compartilhado com camview)
 *      v4l2enum.h  - descoberta, enumeração, diagnóstico e menus de escolha
 *      v4l2term.h  - terminal, cursor e asciiart
 *      v4l2gray.h  - conversão de pixels para cinza (única que usa libjpeg)
 *      v4l2ctl.h   - controles da câmera (brilho, contraste...) em percentual
 *
 *  Para não quebrar os programas existentes, este header continua incluindo os
 *  quatro: quem inclui "v4l2misc.h" enxerga a mesma API de antes. Quem precisa
 *  só de uma parte (ex.: diagcam) inclui apenas o módulo correspondente.
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2MISC_H_INCLUDED
#define V4L2MISC_H_INCLUDED

#include "v4l2core.h"
#include "v4l2enum.h"
#include "v4l2term.h"
#include "v4l2gray.h"

/* Variáveis globais da câmera única (definidas em v4l2misc.c). */
extern enum io_method   io;             /* método de I/O: -1 = automático (padrão)                 */
extern int  w;                          /* largura do frame atual                                  */
extern int  h;                          /* altura do frame atual                                   */
extern int  force_format;               /* 1 = força 640x480 YUYV em init_device()                 */
extern __u32 pxfmt;                     /* pixel format atual (FOURCC)                             */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * INTERFACE PÚBLICA: CAPTURA DE UMA CÂMERA
 * Política de erro deste modo: mostrar o diagnóstico e encerrar o programa.
 *****************************************************************************/
enum io_method check_io_method_busy(int fid_l);     /* wrapper de v4l2c_probe_io: -1 ocupada, -2 erro      */
int  xioctl(int fh, int request, void *arg);        /* wrapper de v4l2c_xioctl                              */
int  open_v4l2_device(char *dv_n);                  /* abre o dispositivo (NULL = o escolhido em dev_name)  */
void close_v4l2_device(int fid_l);
int  setting_video_format(int fid_l);               /* menus de formato/resolução/intervalo                 */
int  set_image_format(int fid_l, __u32 format);
int  set_image_size(int fid_l, int iw, int ih);
int  set_image_intervals(int fid_l, __u32 n, __u32 d);
void init_device(int fid_l);                        /* valida, escolhe I/O, lê formato e aloca buffers      */
void uninit_device(int fid_l);
void start_capturing(int fid_l);
void stop_capturing(int fid_l);
void mainloop(int fid_l);                           /* captura até o usuário digitar 'q'                    */

/* Callback de frame do modo câmera única: cada programa (icap, ...) a DEFINE.
 * v4l2misc.c fornece uma versão fraca vazia. Recebe os dados do frame e o tamanho em bytes. */
void process_image(const void *p, int isize);

/* Converte o frame bruto (no formato atual da câmera) para cinza de 8 bits, usando as
 * globais pxfmt, w e h. Devolve NULL, com aviso, se o formato não tiver conversão. */
unsigned char* convert_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size);

/******************************************************************************
 * CONTROLES DA CÂMERA (brilho, contraste, exposição...) EM PERCENTUAL
 * Substituem a antiga populate_controls_table()/camera_ctrl_table (globais).
 * A câmera é a única do programa, então a tabela v4l2ctl_set é interna a v4l2misc.c;
 * os controles são citados pelo NOME ("brightness", "contrast"...). Para acesso
 * completo (menus, botões, índices) use controls_table() com a API de v4l2ctl.h.
 * Política de erro: estas funções NÃO encerram o programa; devolvem < 0 em falha
 * (V4L2C_ERR_* / V4L2CTL_ERR_*, ver v4l2core.h e v4l2ctl.h).
 *****************************************************************************/
int  load_controls(int fid_l);                                   /* lê os controles do driver; devolve quantos      */
v4l2ctl_set* controls_table(void);                               /* tabela da câmera única (para listar/avançado)   */
int  set_control_pct(const char *name, double pct);              /* ajusta "name" para pct % da faixa               */
int  get_control_pct(const char *name, double *pct);             /* lê "name" como percentual                       */
int  adjust_control_pct(const char *name, double delta_pct, double *new_pct);  /* soma delta (anda >= 1 passo)      */
int  reset_control(const char *name);                            /* volta ao valor de fábrica                       */

/* Legado didático: imagem RGB alocável. */
void allocate_image(int h, int w);
void deallocate_image();

#ifdef __cplusplus
}
#endif

#endif // V4L2MISC_H_INCLUDED
