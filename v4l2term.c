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
 *  v4l2term.c - TERMINAL, CURSOR E EXIBIÇÃO EM MODO TEXTO (implementação)
 *
 *  Veja v4l2term.h. Não conhece câmeras: recebe um buffer de brilho e o desenha.
 *  ---------------------------------------------------------------------------
 */

#include "v4l2term.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// ***************************
/// Estado do terminal e cronômetros (declarados como extern em v4l2term.h)
struct winsize wdw;
int            maxTH = 0;
int            maxTW = 0;
struct timeval t0;
struct timeval t1;
struct timeval t2;

/// Contador de quadros por segundo usado por asciiart()
int fr = 0;
/// ***************************

///    Sequências ANSI úteis (referência):
///    - Posicionar o cursor:        \033[<L>;<C>H   (linha L, coluna C)
///    - Limpar a tela:              \033[2J
///    - Apagar até o fim da linha:  \033[K
///    - Ocultar o cursor:           \e[?25l
///    - Mostrar o cursor:           \e[?25h

/* ========================================================================== */
/*  Terminal                                                                  */
/* ========================================================================== */

/*
 * Inicializa o terminal: dispara os três cronômetros no instante atual,
 * mede a janela e, se o tamanho mudou desde a última medida, limpa a tela.
 */
void init_terminal(){

    gettimeofday(&t0, NULL);        /* os três começam juntos: o primeiro cálculo de FPS parte de zero */
    gettimeofday(&t1, NULL);
    gettimeofday(&t2, NULL);

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);     /* pergunta ao terminal quantas linhas/colunas tem */

    if(maxTH != wdw.ws_row || maxTW != wdw.ws_col){
        SCRCLR();                               /* tamanho novo: o que estava na tela não serve mais */
        maxTH = wdw.ws_row;
        maxTW = wdw.ws_col;
    }

}

/*
 * Move o cursor para a coluna x e a linha y.
 * ATENÇÃO: a ordem (x = coluna, y = linha) é a inversa da macro GOTOXY(linha, coluna).
 */
void set_cur_pos(int x, int y){

    printf("\033[%d;%dH", y, x);        /* a sequência ANSI espera primeiro a LINHA */

}

/*
 * Remede a janela do terminal e atualiza maxTH / maxTW se ela foi redimensionada.
 * É chamada a cada quadro por asciiart(), então o desenho acompanha o tamanho da janela.
 */
void update_terminal_size(){

    /// Como obter o tamanho do terminal: https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
    /// Primeira forma (getenv LINES/COLUMNS) não funciona no Raspberry Pi 3 com Ubuntu Server;
    /// por isso usamos a segunda forma: TIOCGWINSZ.
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);

    if(maxTH != wdw.ws_row || maxTW != wdw.ws_col){
        maxTH = wdw.ws_row;
        maxTW = wdw.ws_col;
    }

}

/* Devolve a altura (linhas) do terminal, medindo na hora, sem alterar maxTH. */
int get_terminal_max_height(){

    struct winsize ws;                  /* local: não mexe na global wdw */

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

    return ws.ws_row;

}

/* Devolve a largura (colunas) do terminal, medindo na hora, sem alterar maxTW. */
int get_terminal_max_width(){

    struct winsize ws;

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

    return ws.ws_col;

}

/* ========================================================================== */
/*  Ambiente de execução                                                      */
/* ========================================================================== */

/*
 * Há servidor gráfico? A forma padrão em C é testar a variável de ambiente DISPLAY.
 * Referência: https://stackoverflow.com/questions/13204177
 * Isso também distingue um pseudo-terminal dentro do desktop de um tty puro.
 */
int is_gui_present(){

    if (NULL == getenv("DISPLAY")){
        return 0;                       /* sem DISPLAY: console puro */
    }

    return 1;

}

/*
 * O programa foi lançado pela linha de comando? Quando o desktop lança um programa
 * (duplo clique), o stdin não é um terminal; isatty() detecta a diferença.
 */
int from_command_line(){

    if (isatty(fileno(stdin))){
        return 1;
    }

    return 0;

}

/* ========================================================================== */
/*  Exibição                                                                  */
/* ========================================================================== */

/*
 * Tabela que converte um nível de cinza (0..255) no caractere que o representa, do mais
 * escuro ao mais claro: " .-:=+*#$@". Os 31 primeiros níveis viram ' ' e cada um dos demais
 * caracteres cobre 25 níveis. É montada na primeira chamada e reaproveitada depois.
 * Fonte ÚNICA da rampa: asciiart() e cam_asciiart_X() (camascii.cpp) usam esta mesma tabela,
 * então a câmera única e o multicâmera mostram os mesmos tons.
 * (A tabela é preenchida uma vez, de forma idêntica por quem chamar primeiro; no programa
 * ela é pedida pela thread principal, então não há disputa.)
 */
const unsigned char* v4l2t_gray_ramp(void){

    static const char ramp[] = " .-:=+*#$@";
    static unsigned char mapchar[256];
    static int ready = 0;
    int i;

    if (!ready) {
        for (i = 0; i < 256; i++) {
            mapchar[i] = (unsigned char)ramp[(i < 31) ? 0 : 1 + (i - 31) / 25];
        }
        ready = 1;
    }

    return mapchar;

}

/* Proporção de uma célula de texto: cada caractere tem ~16 pontos de altura por 8 de largura. */
#define HDOT 16
#define WDOT  8

/*
 * Desenha um buffer de brilho (1 byte por pixel, ih linhas x iw colunas) como texto.
 *
 *   data  : pixels em tons de cinza, contíguos (gerados por v4l2g_to_gray)
 *   isize : quantos bytes há em data (protege contra leitura além do buffer)
 *   ih/iw : altura e largura da imagem em pixels
 *
 * A imagem é reduzida para caber no terminal mantendo a proporção; cada caractere
 * representa um bloco de pixels e a "tinta" vai de ' ' (preto) a '@' (branco).
 * Na última linha mostra o frame rate (atualizado a cada segundo).
 */
void asciiart(const void *data, int isize, int ih, int iw){

    /* Tabela de 256 níveis de cinza -> caractere (a mesma usada por cam_asciiart_X). */
    const unsigned char *mapchar = v4l2t_gray_ramp();

    static char imx[150000];            /* tela inteira montada em memória e impressa de uma vez */

    char* pc;
    const unsigned char* q;             /* leitura linear de 1 byte por pixel */
    int i, j, l;
    int rzH, rzW;                       /* quantos pixels da imagem cabem em cada caractere (vertical/horizontal) */
    int nch, ncw;                       /* altura e largura do desenho, em caracteres */
    int ct = 0;                         /* colunas já escritas na linha atual */
    int tcol;
    char title[] = "IoT - Image on Terminal (Universal Grayscale)";

    /* Atualiza maxTH / maxTW: o usuário pode ter redimensionado a janela desde o último quadro. */
    update_terminal_size();
    SCRCLR();

    /* Título centralizado na linha 1 (a coluna nunca fica menor que 1, mesmo em terminal estreito). */
    tcol = (maxTW - (int)strlen(title)) / 2;
    if (tcol < 1) tcol = 1;
    GOTOXY(1, tcol);
    printf("%s", title);

    /* Terminal ou imagem pequenos demais para desenhar: sai sem dividir por zero. */
    if (ih <= 0 || iw <= 0 || maxTH < 5 || maxTW < 5) {
        return;
    }

    /* Tamanho do desenho: usa toda a altura útil e calcula a largura que mantém a proporção
     * (cada caractere é HDOT/WDOT vezes mais alto que largo). */
    nch = maxTH - 4;
    ncw = (iw * nch * HDOT) / (ih * WDOT);

    if (ncw > maxTW - 4) {              /* ficou largo demais: limita pela largura e recalcula a altura */
        ncw = maxTW - 4;
        nch = (ncw * ih * WDOT) / (iw * HDOT);
    }

    if (nch < 1 || ncw < 1) {
        return;
    }

    /* O buffer imx é fixo: reduz a altura se o terminal for enorme (evita estourar o vetor). */
    if ((long)nch * (long)maxTW + 1 > (long)sizeof(imx)) {
        nch = (int)((sizeof(imx) - 1) / (size_t)maxTW);
    }

    rzH = ih / nch;                     /* passo de amostragem na vertical   */
    rzW = iw / ncw;                     /* passo de amostragem na horizontal */
    if (rzH <= 0) rzH = 1;
    if (rzW <= 0) rzW = 1;

    q  = (const unsigned char*)data;
    pc = imx;

    /* Monta a tela: para cada caractere pega o pixel do canto do bloco e o traduz pela rampa. */
    for (i = 0; i < nch; i++) {
        for (j = 0; j < ncw; j++) {

            unsigned long pixel_pos = (unsigned long)(i * rzH) * (unsigned long)iw + (unsigned long)(j * rzW);

            if (pixel_pos < (unsigned long)isize) {
                pc[0] = (char)mapchar[q[pixel_pos]];
            } else {
                pc[0] = ' ';            /* além do fim do buffer: espaço, nunca lixo */
            }
            pc++;
            ct++;

            /* Fim da linha do desenho: completa até a largura do terminal com espaços. */
            if (ct == ncw) {
                ct = 0;
                for (l = 0; l < (maxTW - ncw); l++) {
                    pc[0] = ' ';
                    pc++;
                }
            }
        }
    }
    pc[0] = '\0';                       /* fecha a string da tela */

    printf("\e[?25l");                  /* esconde o cursor para não piscar durante o desenho */

    /* Desenho centralizado na horizontal, a partir da linha 3. */
    GOTOXY(3, ((maxTW - ncw - 4) / 2) + 2);
    printf("%s", imx);

    printf("\e[?25h");                  /* mostra o cursor de volta */

    /* Frame rate: conta quadros e, a cada segundo, imprime o total na última linha. */
    fr++;
    gettimeofday(&t2, NULL);

    if ((t2.tv_sec * 1000 + t2.tv_usec / 1000) - (t1.tv_sec * 1000 + t1.tv_usec / 1000) > 1000) {
        GOTOXY(maxTH, 1);
        printf("Frame Rate: %d", fr - 1);
        fr = 0;
        t1 = t2;
    }
}
