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
 *  v4l2term.h - TERMINAL, CURSOR E EXIBIÇÃO EM MODO TEXTO (ASCII ART)
 *
 *  Etapa 4 da reorganização. Tudo que lida com a tela do terminal mora aqui,
 *  separado do acesso à câmera: tamanho da janela, posicionamento do cursor,
 *  detecção de GUI/linha de comando e a renderização de uma imagem em tons de
 *  cinza como caracteres (asciiart).
 *
 *  Usado por: icap (via v4l2misc), multiview (menus e janelas de texto).
 *  Não depende de V4L2 nem de libjpeg.
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2TERM_H_INCLUDED
#define V4L2TERM_H_INCLUDED

#include <stdio.h>
#include <sys/ioctl.h>          /// struct winsize, TIOCGWINSZ
#include <sys/time.h>           /// struct timeval

/* Limpa a tela e leva o cursor para o canto superior esquerdo. */
#define SCRCLR() printf("\033[H\033[J")

/* Posiciona o cursor na LINHA x, COLUNA y (1,1 = canto superior esquerdo).
 * ATENÇÃO: a ordem é (linha, coluna). Já set_cur_pos() recebe (coluna, linha). */
#define GOTOXY(x,y) printf("\033[%d;%dH", (int)(x), (int)(y))

/* Estado compartilhado do terminal (atualizado por init_terminal / update_terminal_size). */
#ifdef __cplusplus
extern "C" {
#endif

extern struct winsize wdw;      /* última medida do terminal (linhas e colunas)           */
extern int maxTH;               /* altura do terminal em linhas                           */
extern int maxTW;               /* largura do terminal em colunas                         */
extern struct timeval t0;       /* cronômetros de uso geral: contagem de taxa de quadros  */
extern struct timeval t1;       /* (asciiart usa t1/t2; multiview usa t0/t1/t2 nos menus) */
extern struct timeval t2;

/* --- Terminal ---------------------------------------------------------------- */
void init_terminal();                   /* zera cronômetros, mede o terminal e limpa a tela se mudou   */
void set_cur_pos(int x, int y);         /* cursor na coluna x, linha y (ordem inversa de GOTOXY!)      */
void update_terminal_size();            /* remede o terminal e atualiza maxTH / maxTW                  */
int  get_terminal_max_height();         /* linhas do terminal (consulta direta, sem alterar maxTH)     */
int  get_terminal_max_width();          /* colunas do terminal (consulta direta, sem alterar maxTW)    */

/* --- Ambiente de execução ---------------------------------------------------- */
int  is_gui_present();                  /* 1 se há servidor gráfico (variável DISPLAY definida)        */
int  from_command_line();               /* 1 se o stdin está ligado a um terminal (isatty)             */

/* --- Exibição ---------------------------------------------------------------- */
/* Desenha na tela um buffer de brilho de 8 bits (1 byte por pixel, ih linhas x iw colunas)
 * como texto, ajustando ao tamanho do terminal, e mostra o frame rate na última linha. */
void asciiart(const void *data, int isize, int ih, int iw);

/* Tabela de 256 níveis de cinza -> caractere ' .-:=+*#$@' (fonte única da rampa, também usada
   por cam_asciiart_X no multicâmera). Não libere o ponteiro. */
const unsigned char* v4l2t_gray_ramp(void);

#ifdef __cplusplus
}
#endif

#endif /* V4L2TERM_H_INCLUDED */
