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
 *  v4l2enum.h - DESCOBERTA, ENUMERAÇÃO E DIAGNÓSTICO DE DISPOSITIVOS V4L2
 *
 *  Etapa 4 da reorganização. Reúne o que NÃO captura imagem, só consulta e
 *  mostra informação sobre dispositivos, e por isso é compartilhado por todos:
 *
 *      diagcam    -> v4l2enum + v4l2core           (sem libjpeg, sem captura)
 *      v4l2misc   -> v4l2enum + v4l2core + ...     (câmera única)
 *      camview    -> v4l2enum + v4l2core + ...     (multicâmera)
 *
 *  Conteúdo:
 *    1. Diagnóstico de erros (errno_exit, explain_v4l2_error);
 *    2. Descoberta de dispositivos em /dev e a lista de câmeras encontradas;
 *    3. Listagem de capacidades, entradas/saídas, controles e formatos;
 *    4. Escolha interativa (menus no terminal) de dispositivo, formato,
 *       resolução e intervalo.
 *  Depende apenas de v4l2core.h (usa v4l2c_xioctl). Não captura frames.
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2ENUM_H_INCLUDED
#define V4L2ENUM_H_INCLUDED

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>              /// low-level i/o
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <linux/videodev2.h>

#include "v4l2core.h"
#include "v4l2ctl.h"          /* controles em percentual (v4l2ctl_set) */

/* Quantidade máxima de dispositivos V4L2 guardados na lista de descoberta. */
#define MAX_V4L2_DEV_NUM 127

/* Tipos de nó de dispositivo em /dev (o índice combina com o vetor 'prefixes'). */
enum v4l2_device_type {
        V4L2_DEV_VIDEO,
        V4L2_DEV_VBI,
        V4L2_DEV_RADIO,
        V4L2_DEV_V4L_SUBDEV,
        V4L2_DEV_SWRADIO,
        V4L2_DEV_V4L_TOUCH,
        V4L2_DEV_ALL,
};

/* Identifica um dispositivo: tipo (video, vbi...) e número (ex.: /dev/video2 -> VIDEO, 2). */
typedef struct _v4l2_id {
    enum v4l2_device_type typ;       /// Identificador do tipo de node do dispositivo
    unsigned char num;               /// Identificador do número do node do dispositivo
} v4l2_id;


#ifdef __cplusplus
extern "C" {
#endif

/* --- Dados compartilhados -------------------------------------------------- */
extern const char *prefixes[];                                      /* "video", "vbi", "radio"... (NULL no fim) */
extern char dev_name[256];                                          /* caminho do dispositivo escolhido (/dev/videoN) */

/* --- 1. Diagnóstico de erros ----------------------------------------------- */
void errno_exit(const char *s);                     /* mostra "<s> error N, texto" e encerra o programa  */
int  get_errno_description(void);                   /* mostra a descrição do errno atual e o devolve     */
void explain_v4l2_error(const char *context_message); /* diagnóstico detalhado (causa provável e sugestão) */

/* --- 2. Descoberta de dispositivos ------------------------------------------ */
char*    get_dev_name();                            /* caminho do dispositivo escolhido                  */
v4l2_id* get_v4l2_id(int idx);                      /* id do dispositivo de índice idx (ou 0)            */
int  make_v4l2_list();                              /* varre /dev: TODOS os tipos V4L2; devolve quantos  */
int  make_video_list();                             /* varre /dev: só /dev/video*; devolve quantos       */
void make_v4l2_path(char* dv_ph, enum v4l2_device_type typ, unsigned char num);
void get_v4l2_path(char* dv_ph, int idx);
int  found_in_list(v4l2_id vid, char* ck);          /* vid está na lista? marca ck[i] se estiver         */

/* --- 3. Listagem e diagnóstico (só leitura do driver) ----------------------- */
void show_video_format(int fid_l);
int  list_v4l2_capabilities(int fid_l);
int  list_video_io(int fid_l);
void list_controls_pct(const v4l2ctl_set *cs);       /* tabela de controles em % (ver v4l2ctl.h)          */
int  enumerate_controls(int fid_l);
void enumerate_menu(struct v4l2_query_ext_ctrl *qXctrl, int fid_l);
int  enumerate_video_standards(int fid_l);
int  enumerate_audio_input(int fid_l);
int  enumerate_audio_output(int fid_l);
int  enumerate_video_input(int fid_l);
int  enumerate_video_output(int fid_l);

/* --- 4. Escolha interativa (terminal) --------------------------------------- */
int  choose_value(int minv, int maxv, int step, char* tp);   /* pede um número ao usuário (validado)   */
int  choose_or_enumerate_image_formats(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int  choose_or_enumerate_frame_size(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int  choose_or_enumerate_frame_intervals(int fid_l, struct v4l2_frmivalenum* flg, int expanded);
void show_v4l2_list();
void show_video_list();
void choose_v4l2_device();                          /* lista, pergunta e grava o resultado em dev_name   */

#ifdef __cplusplus
}
#endif

#endif /* V4L2ENUM_H_INCLUDED */
