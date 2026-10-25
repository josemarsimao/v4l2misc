/*
 *  V4L2 video capture: My tests
 *
 *  Nome: Josemar Simão
 *  email: josemars@ifes.edu.br
 *
 *  This program can be used and distributed without restrictions.
 *
 *  ---------------------------------------------------------------------------
 *  v4l2ctl.h - CONTROLES DA CÂMERA (brilho, contraste, exposição...) EM PERCENTUAL
 *
 *  Objetivo: ler e ajustar os controles de UMA câmera sem que o programador precise
 *  conhecer os valores absolutos de cada driver. Todo ajuste é feito em PERCENTUAL
 *  da faixa [min, max] do controle:
 *
 *        0 %  = valor mínimo do controle
 *      100 %  = valor máximo do controle
 *
 *  A conversão para o valor real (e o arredondamento para o "passo" do controle)
 *  acontece aqui dentro. Assim o mesmo código serve para uma câmera cujo brilho vai
 *  de 0 a 255 e para outra que vai de -64 a 64.
 *
 *  Estrutura (substitui camera_control_info / camera_ctrl_table / num_controls_found,
 *  que eram GLOBAIS e só comportavam uma câmera):
 *
 *      v4l2ctl_item : descreve UM controle (id, tipo, faixa, passo, valor, flags).
 *      v4l2ctl_set  : tabela dos controles de UMA câmera. Quem tem várias câmeras
 *                     tem várias v4l2ctl_set (uma por câmera; no multiview, uma em
 *                     cada 'viod').
 *
 *  Regras desta camada (as mesmas do v4l2core):
 *    1. NÃO chama exit(), NÃO usa printf/scanf, NÃO tem variáveis globais.
 *    2. Erros voltam como código negativo; o errno fica em cs->err_no.
 *    3. Uma v4l2ctl_set NÃO é protegida por mutex. Se duas threads a usarem,
 *       quem chama deve serializar (camview.cpp faz isso com um mutex por câmera).
 *
 *  Tipos tratados: inteiro, booleano, menu, menu de inteiros, inteiro de 64 bits e
 *  botão (ação). Controles compostos (vetores/payload), strings, bitmask e títulos
 *  de classe são ignorados: não têm uma faixa que faça sentido em percentual.
 *
 *  Nos controles de MENU o "valor" é o ÍNDICE do item. O percentual percorre a faixa
 *  de índices e é encaixado no item válido mais próximo (menus podem ter buracos).
 *
 *  Convenção de retorno: >= 0 sucesso; < 0 erro (V4L2C_ERR_* do v4l2core.h ou
 *  V4L2CTL_ERR_* abaixo).
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2CTL_H_INCLUDED
#define V4L2CTL_H_INCLUDED

#include <stddef.h>
#include <linux/videodev2.h>
#include "v4l2core.h"            /* códigos V4L2C_ERR_* e v4l2c_xioctl */

/* Quantos controles cabem na tabela de UMA câmera (webcams comuns têm menos de 40). */
#define V4L2CTL_MAX_ITEMS   64

/* Códigos de erro próprios desta camada (somam-se aos V4L2C_ERR_*). */
#define V4L2CTL_ERR_INDEX   -10  /* índice fora da tabela, ou tabela vazia                */
#define V4L2CTL_ERR_ARG     -11  /* argumento inválido (ponteiro nulo, texto vazio...)    */
#define V4L2CTL_ERR_NOTSET  -12  /* controle não é gravável (somente leitura/desativado) */
#define V4L2CTL_ERR_NOFIND  -13  /* nenhum controle com esse nome/id                      */

/* Um controle da câmera. 'cur' é o ÚLTIMO valor lido/gravado (use v4l2ctl_get_raw para reler). */
typedef struct _v4l2ctl_item {
    __u32   id;             /* V4L2_CID_*                                                   */
    __u32   type;           /* V4L2_CTRL_TYPE_*                                             */
    __u32   flags;          /* V4L2_CTRL_FLAG_* (somente leitura, inativo, preso, ...)      */
    char    name[32];       /* nome dado pelo driver ("Brightness", "Exposure, Auto"...)    */
    __s64   min;            /* menor valor                                                  */
    __s64   max;            /* maior valor                                                  */
    __u64   step;           /* passo entre valores válidos (nunca 0 depois de populate)     */
    __s64   def;            /* valor de fábrica                                             */
    __s64   cur;            /* último valor conhecido                                       */
} v4l2ctl_item;

/* Tabela dos controles de UMA câmera. Zere com v4l2ctl_clear() antes do primeiro uso. */
typedef struct _v4l2ctl_set {
    int           fid;                          /* descritor da câmera (guardado por populate)   */
    int           n;                            /* quantos itens válidos em it[]                 */
    int           dropped;                      /* controles que não couberam em it[]            */
    int           err_no;                       /* errno do último erro de ioctl                 */
    v4l2ctl_item  it[V4L2CTL_MAX_ITEMS];
} v4l2ctl_set;

#ifdef __cplusplus
extern "C" {
#endif

/* --- Ciclo de vida ----------------------------------------------------------- */
void v4l2ctl_clear(v4l2ctl_set *cs);                       /* zera a tabela (n = 0, fid = -1)          */
int  v4l2ctl_populate(v4l2ctl_set *cs, int fid);           /* lê os controles do driver; devolve n ou <0 */
int  v4l2ctl_refresh_all(v4l2ctl_set *cs);                 /* relê valores e flags de todos os itens   */

/* --- Consulta ------------------------------------------------------------------ */
int  v4l2ctl_find_id(const v4l2ctl_set *cs, __u32 id);     /* índice do controle com esse V4L2_CID_*, ou -1 */
int  v4l2ctl_find_name(const v4l2ctl_set *cs, const char *name);  /* por nome (sem diferenciar maiúsculas; igual, senão contém) */
int  v4l2ctl_is_writable(const v4l2ctl_item *it);          /* 1 se o controle aceita gravação agora    */
const char *v4l2ctl_type_name(__u32 type);                 /* "int", "bool", "menu"... (para mostrar)  */

/* --- Conversão pura (sem hardware; usada nos testes) --------------------------- */
double v4l2ctl_raw_to_pct(const v4l2ctl_item *it, __s64 raw);   /* valor real -> percentual 0..100   */
__s64  v4l2ctl_pct_to_raw(const v4l2ctl_item *it, double pct);  /* percentual -> valor real (encaixado no passo e na faixa) */

/* --- Valor real (cru) ------------------------------------------------------------ */
int  v4l2ctl_get_raw(v4l2ctl_set *cs, int idx, __s64 *raw);     /* lê do driver e atualiza it[idx].cur  */
int  v4l2ctl_set_raw(v4l2ctl_set *cs, int idx, __s64 raw);      /* grava (limita, encaixa, relê)         */

/* --- Percentual: a interface recomendada ------------------------------------------ */
int  v4l2ctl_get_pct(v4l2ctl_set *cs, int idx, double *pct);    /* pct recebe o valor atual, 0..100     */
int  v4l2ctl_set_pct(v4l2ctl_set *cs, int idx, double pct);     /* ajusta para pct % da faixa            */
int  v4l2ctl_adjust_pct(v4l2ctl_set *cs, int idx, double delta_pct, double *new_pct); /* soma delta; sempre anda >= 1 passo */
int  v4l2ctl_reset_default(v4l2ctl_set *cs, int idx);           /* volta ao valor de fábrica            */
int  v4l2ctl_trigger(v4l2ctl_set *cs, int idx);                 /* dispara um controle do tipo BOTÃO    */
int  v4l2ctl_reset_all(v4l2ctl_set *cs);                        /* todos ao valor de fábrica (3 passadas); devolve quantos não voltaram */

/* --- Menus --------------------------------------------------------------------------- */
int  v4l2ctl_menu_name(const v4l2ctl_set *cs, int idx, __s64 value, char *out, size_t outsz); /* texto do item; <0 se índice inválido */

#ifdef __cplusplus
}
#endif

#endif /* V4L2CTL_H_INCLUDED */
