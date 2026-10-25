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
 *  v4l2core.h - NÚCLEO DE CAPTURA V4L2 (camada base)
 *
 *  Objetivo: existir UMA única implementação do motor de captura
 *  (abrir, configurar, alocar buffers, ligar o streaming, ler frames e
 *  liberar tudo). Essa camada é usada pelas duas bibliotecas de nível mais alto:
 *
 *      icap.c      -> v4l2misc.c (câmera única, thread única, modo texto) --\
 *                                                                           +-> v4l2core
 *      multiview   -> camview.cpp (multicâmera, uma thread por câmera) -----/
 *
 *  Regras desta camada (é isso que a torna reutilizável nos dois mundos):
 *    1. NÃO chama exit(). Todo erro volta como código de retorno (negativo);
 *       quem decide encerrar o programa (ou só ignorar a câmera) é a aplicação.
 *    2. NÃO usa printf/scanf, terminal ou teclado. Nenhuma interação com o usuário.
 *    3. NÃO tem variáveis globais. Todo o estado de UMA câmera mora em uma
 *       v4l2_session, então várias sessões convivem sem interferência
 *       (uma por thread no multiview).
 *    4. O frame capturado é entregue por CALLBACK (v4l2c_frame_cb). Isso substitui
 *       a resolução "por símbolo no link" (process_image fraco / cam_process_image).
 *
 *  Convenção de retorno:
 *    >= 0 : sucesso (V4L2C_OK; v4l2c_read_frame usa 1 = frame entregue, 0 = sem frame ainda)
 *    <  0 : erro (V4L2C_ERR_*). O errno e o contexto ficam em s->err_no / s->err_ctx.
 *           Para imprimir o diagnóstico detalhado faça:
 *               errno = s->err_no;  explain_v4l2_error(s->err_ctx);
 *  ---------------------------------------------------------------------------
 */

#ifndef V4L2CORE_H_INCLUDED
#define V4L2CORE_H_INCLUDED

#include <stddef.h>
#include <string.h>
#include <linux/videodev2.h>

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#endif

/* Método de transferência dos frames entre driver e aplicação.
 * (Mesma ordem usada em v4l2misc.h: os valores numéricos NÃO podem mudar.) */
enum io_method {
        IO_METHOD_READ,
        IO_METHOD_USERPTR,
        IO_METHOD_MMAP,
        IO_METHOD_DMABUF,
};

/* Um buffer de captura. 'binf' guarda o v4l2_buffer devolvido pelo driver
 * (principalmente 'bytesused', necessário para formatos comprimidos como MJPEG). */
typedef struct _vbuff {
        void   *start;
        size_t  length;
        struct v4l2_buffer binf;
} vbuff;

/* Códigos de retorno. Positivos/zero = ok, negativos = erro. */
#define V4L2C_OK                0
#define V4L2C_GOT_FRAME         1   /* v4l2c_read_frame: um frame foi entregue ao callback   */
#define V4L2C_NO_FRAME          0   /* v4l2c_read_frame: ainda não há frame (EAGAIN)         */
#define V4L2C_ERR_IOCTL        -1   /* open/ioctl/read/select falhou: ver s->err_no          */
#define V4L2C_ERR_NOMEM        -2   /* sem memória ou driver deu buffers insuficientes        */
#define V4L2C_ERR_UNSUPPORTED  -3   /* dispositivo/método de I/O não suportado               */
#define V4L2C_ERR_TIMEOUT      -4   /* câmera parou de entregar frames                       */
#define V4L2C_ERR_BUSY         -5   /* dispositivo em uso por outro processo (EBUSY)         */

/* Callback chamado a cada frame pronto. Os dados em 'data' pertencem ao driver:
 * só são válidos DENTRO do callback (o buffer é devolvido à fila logo depois). */
typedef void (*v4l2c_frame_cb)(void *user, const void *data, size_t bytesused,
                               const struct v4l2_buffer *binf);

/* Callback consultado a cada volta do laço: retorne 0 para parar a captura.
 * Câmera única: testa o teclado ('q'). Multicâmera: testa o flag 'thon' da câmera. */
typedef int (*v4l2c_run_cb)(void *user);

/* Estado completo de UMA câmera. É o que hoje está espalhado entre as globais de
 * v4l2misc.c (buffers, num_buf, io, w, h, pxfmt) e os campos de 'viod' em camview.h. */
typedef struct _v4l2_session {
    int             fid;            /* descritor do dispositivo (retorno de v4l2c_open)      */
    int             io;             /* enum io_method; valor fora do enum (-1) = automático  */
    vbuff          *buffers;        /* vetor de buffers de captura                           */
    unsigned int    num_buf;        /* quantidade de buffers válidos em 'buffers'            */
    unsigned int    buffer_maxsize; /* maior tamanho (bytes) de um frame                     */
    int             bon;            /* índice do buffer que acabou de ser entregue           */
    int             w, h;           /* resolução atual (atualizada por v4l2c_get_format)     */
    __u32           pxfmt;          /* pixel format atual (FOURCC)                           */
    int             timeout_sec;    /* segundos sem frames até declarar timeout (0 = 5)      */
    v4l2c_frame_cb  on_frame;       /* quem recebe os frames                                 */
    void           *user;           /* argumento repassado aos callbacks (ex.: viod*)        */
    int             err_no;         /* errno do último erro registrado                       */
    const char     *err_ctx;        /* texto (literal) descrevendo onde o erro ocorreu       */
} v4l2_session;

#ifdef __cplusplus
extern "C" {
#endif

/* --- Utilitários sem sessão ------------------------------------------------ */
int  v4l2c_xioctl(int fh, unsigned long request, void *arg);   /* ioctl que repete em EINTR */
int  v4l2c_open(const char *path);          /* abre /dev/videoX (não-bloqueante); -1 e errno se falhar */
int  v4l2c_close(int fid);                  /* fecha o descritor                                       */
int  v4l2c_probe_io(int fid);               /* descobre o método de I/O livre: io_method ou V4L2C_ERR_* */

/* --- Ciclo de vida de uma sessão ------------------------------------------- */
void v4l2c_session_init(v4l2_session *s, int fid);               /* zera e prepara a sessão       */
int  v4l2c_get_format(v4l2_session *s);                           /* lê w,h,pxfmt,buffer_maxsize   */
int  v4l2c_set_format(v4l2_session *s, __u32 pixfmt, int w, int h);  /* 0 = manter o valor atual  */
int  v4l2c_set_interval(v4l2_session *s, __u32 n, __u32 d);       /* intervalo entre frames n/d    */
int  v4l2c_init_buffers(v4l2_session *s);                         /* aloca buffers conforme s->io  */
int  v4l2c_start(v4l2_session *s);                                /* enfileira buffers + STREAMON  */
int  v4l2c_read_frame(v4l2_session *s);                           /* lê UM frame e chama on_frame  */
int  v4l2c_run(v4l2_session *s, v4l2c_run_cb keep_running);       /* laço select+read_frame        */
int  v4l2c_stop(v4l2_session *s);                                 /* STREAMOFF                     */
int  v4l2c_free_buffers(v4l2_session *s);
int  v4l2c_release_driver(v4l2_session *s);                       /* só REQBUFS(0): libera os buffers no DRIVER */                         /* munmap/free + libera no driver*/

/* --- Atalho: executa o ciclo completo e SEMPRE limpa o que foi alocado ------ */
int  v4l2c_capture(v4l2_session *s, v4l2c_run_cb keep_running);

#ifdef __cplusplus
}
#endif

#endif /* V4L2CORE_H_INCLUDED */
