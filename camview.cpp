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
 *  camview.cpp - BIBLIOTECA MULTICÂMERA (uma thread de captura por câmera)
 *
 *  Etapa 3 da reorganização: o motor de captura que antes estava duplicado aqui
 *  (cam_init_mmap/userp/read, cam_read_frame, cam_mainloop...) foi substituído
 *  por chamadas ao núcleo v4l2core. Este arquivo ficou só com o que é próprio
 *  do multicâmera:
 *    - a lista de câmeras e a detecção de hot-plug (make_camera_list);
 *    - o ciclo de vida de cada câmera/thread (estados DEV_*, view, thon);
 *    - a ligação do núcleo com o processamento de imagem (cam_on_frame).
 *
 *  Política de erro: ao contrário da câmera única, NENHUMA função daqui chama
 *  exit(). Se uma câmera falha, só a thread dela termina; as outras continuam.
 *  ---------------------------------------------------------------------------
 */

#include "camview.h"
#include "camconv.h"       /// convertToRgb()

/* Valor guardado em viod.io quando o dispositivo está ocupado por outro processo.
 * (Antes era escrito como ~IO_METHOD_READ, que é -1, mas ninguém percebia.) */
#define CAM_IO_BUSY (-1)

/* Nomes dos estados DEV_* indexados pelo próprio valor do estado (ex.: [16] = "CAPTURING"). */
const char *cam_status[] = {
    "NONE",
    "BUSY",
    "OPEN",
    "XXXX",
    "CONFIGURED",
    "XXXX",
    "XXXX",
    "XXXX",
    "OUR",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "CAPTURING",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "PROCESSING",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "XXXX",
    "ALL"
};

/* ========================================================================== */
/*  Ligação entre o núcleo v4l2core e a câmera (viod)                         */
/* ========================================================================== */

/*
 * Mostra o diagnóstico detalhado do último erro registrado na sessão da câmera.
 * Não encerra o programa (o multiview deve continuar com as outras câmeras).
 */
static void cam_report_error(const viod *vd) {

    errno = vd->sess.err_no;        /* explain_v4l2_error() decide a explicação pelo errno */
    explain_v4l2_error(vd->sess.err_ctx ? vd->sess.err_ctx : "captura de vídeo");
}

/*
 * Copia o estado da sessão (fonte única) para os campos "espelho" de viod que
 * multiview.cpp e os process00X.cpp leem diretamente (buffers, bon, w, h...).
 * Deve ser chamada sempre que a sessão mudar (formato lido, buffers criados/liberados).
 */
static void cam_sync_from_session(viod *vd) {

    vd->buffers        = vd->sess.buffers;
    vd->num_buf        = vd->sess.num_buf;
    vd->buffer_maxsize = vd->sess.buffer_maxsize;
    vd->bon            = vd->sess.bon;
    vd->w              = vd->sess.w;
    vd->h              = vd->sess.h;
    vd->pxfmt          = vd->sess.pxfmt;
}

/*
 * Callback chamado pelo núcleo a cada frame pronto (na thread de captura).
 * Atualiza o índice do buffer ativo e dispara o processamento da imagem.
 * Substitui a chamada direta cam_process_image() que existia dentro de cam_read_frame().
 */
static void cam_on_frame(void *user, const void *data, size_t bytesused,
                         const struct v4l2_buffer *binf) {

    viod *vd = (viod *)user;

    (void)data;         /* os dados são lidos de vd->buffers[vd->bon], como antes */
    (void)bytesused;
    (void)binf;

    vd->bon = vd->sess.bon;     /* qual dos buffers acabou de ser preenchido */

    cam_process_image(*vd);     /* versão do projeto, se existir; senão a padrão (fraca) logo abaixo */
}

/*
 * Callback de parada do laço de captura: devolve 0 quando alguém pediu para
 * encerrar esta câmera (thon = 0, tecla 'E' ou remoção da câmera).
 */
static int cam_keep_running(void *user) {

    return ((viod *)user)->thon == 1;
}

/*
 * Descobre o método de I/O livre da câmera, mantendo a convenção usada por
 * viod.io: >= 0 método válido, CAM_IO_BUSY (-1) ocupada, -2 erro/sem captura.
 */
static int cam_probe_io(int fid) {

    int r = v4l2c_probe_io(fid);

    if (r >= 0) {
        return r;                       /* MMAP / USERPTR / READ */
    }

    if (V4L2C_ERR_BUSY == r) {
        return CAM_IO_BUSY;
    }

    return -2;
}

/* ========================================================================== */
/*  Processamento de cada frame: cam_process_image()                          */
/* ========================================================================== */

/*
 * cam_process_image - O QUE FAZER COM CADA FRAME RECEBIDO (versão PADRÃO, "FRACA")
 *
 * É chamada uma vez por frame, na thread de captura da câmera, logo depois que o núcleo
 * (v4l2core) entrega o frame (veja cam_on_frame). É o PONTO DE EXTENSÃO da biblioteca:
 * a biblioteca captura, mas quem decide o que fazer com a imagem é o seu projeto.
 *
 * COMO FUNCIONA A SUBSTITUIÇÃO (mesma ideia de process_image() em v4l2misc):
 *   Esta definição está marcada __attribute__((weak)). Se o seu projeto definir, em qualquer
 *   .cpp dele, uma função com EXATAMENTE esta assinatura (C++, sem static):
 *
 *           void cam_process_image(viod &vd) { ... }
 *
 *   o linker usa a SUA versão (forte) e ignora esta. Se não definir nenhuma, vale a padrão.
 *   Só pode haver UMA versão forte por executável (duas dão erro "multiple definition").
 *   Se a sua função estiver dentro de uma biblioteca estática (.a) que nada mais referencia,
 *   o linker pode não incluí-la: liste o arquivo diretamente no projeto.
 *
 * O QUE ESTA VERSÃO PADRÃO FAZ (o mesmo que o multiview fazia):
 *   1. Se a câmera está em exibição (vd.view) ou há um processo ativo (vd.img_proc), converte o
 *      frame para RGB em vd.v_mat.at(0) usando convertToRgb();
 *   2. Se há um processo ativo (vd.img_proc != 0), chama-o: é o ponteiro para uma das funções
 *      de processamento do programa (ex.: vet_of_funcs em multiview.cpp);
 *   3. Atualiza o contador de quadros por segundo (vd.tm.fr);
 *   4. Guarda os instantes t1..t6 em vd.tm (protegidos por vd.tm_mutex) para o programa
 *      mostrar quanto tempo levou cada etapa.
 *
 * COMO ESCREVER A SUA:
 *   - O frame está em vd.buffers[vd.bon].start; o número de bytes úteis em
 *     vd.buffers[vd.bon].binf.bytesused; formato e tamanho em vd.pxfmt, vd.w e vd.h.
 *     Esses dados só valem DENTRO desta chamada (o buffer volta ao driver depois).
 *   - Para ter a imagem colorida use convertToRgb(vd, vd.v_mat.at(0)) (camconv.h).
 *   - Roda na thread de captura de CADA câmera, ao mesmo tempo que as outras: não bloqueie
 *     por muito tempo, não chame exit() e proteja com mutex qualquer dado compartilhado com
 *     a thread principal.
 *   - Se quiser manter o cronômetro do multiview, preencha vd.tm como abaixo.
 *
 *   Exemplo mínimo (conta frames e ignora a imagem):
 *           static int total = 0;
 *           void cam_process_image(viod &vd) { total++; }
 */
__attribute__((weak)) void cam_process_image(viod &vd) {

    struct timeval t1,t2,t3,t4,t5,t6;       /* instantes de cada etapa (para medir o tempo gasto) */

    gettimeofday(&t1, NULL);                /* início do processamento deste frame */

    /// ---- Etapa 1: converte o frame para RGB, só se alguém vai usá-lo ----
    if(vd.view || vd.img_proc != 0){
        convertToRgb(vd,vd.v_mat.at(0));    /* vd.v_mat.at(0) recebe a imagem RGB */
    }

    gettimeofday(&t2, NULL);

    /// ---- Etapa 2: chama o processo de imagem ativo (se houver) ----
    if(vd.img_proc != 0){
        (*vd.img_proc)(vd);
    }

    gettimeofday(&t3, NULL);

    /// ---- Etapa 3: frame rate. Conta quadros e, a cada segundo, grava o total em vd.tm.fr ----
    vd.tm.cnt++;
    if((vd.tm.t1.tv_sec*1000 + vd.tm.t1.tv_usec/1000) - (vd.tm.t0.tv_sec*1000 + vd.tm.t0.tv_usec/1000) > 1000){
        vd.tm.fr = vd.tm.cnt - 1;
        vd.tm.cnt = 0;
        vd.tm.t0 = t1;
    }

    /* As etapas de "swap" e "ascii" são medidas pela thread principal; aqui só carimbam o tempo. */
    gettimeofday(&t4, NULL);
    gettimeofday(&t5, NULL);
    gettimeofday(&t6, NULL);

    /// ---- Etapa 4: publica os instantes para a thread principal (com exclusão mútua) ----
    {
        std::lock_guard<std::mutex> lk(vd.tm_mutex);
        vd.tm.t1 = t1;
        vd.tm.t2 = t2;
        vd.tm.t3 = t3;
        vd.tm.t4 = t4;
        vd.tm.t5 = t5;
        vd.tm.t6 = t6;
    }

}

/* ========================================================================== */
/*  Passos do ciclo de captura (cada um delega ao núcleo)                     */
/* ========================================================================== */

/*
 * Lê o formato atual da câmera e calcula o tamanho máximo de um frame.
 * Retorna 0 em sucesso ou um código negativo do núcleo (ver s->err_*).
 */
int cam_def_buffer_maxsize(viod *vd){

    int r = v4l2c_get_format(&vd->sess);

    if (r < 0) {
        return r;
    }

    cam_sync_from_session(vd);          /* publica w, h, pxfmt e buffer_maxsize em viod */
    return 0;
}

/*
 * Aloca os buffers de captura conforme vd->io (READ, MMAP ou USERPTR).
 * Retorna 0 em sucesso ou código negativo. Em falha o núcleo já desfez o que alocou.
 */
int cam_init_device(viod *vd){

    int r = v4l2c_init_buffers(&vd->sess);

    cam_sync_from_session(vd);          /* publica buffers/num_buf (ou NULL se falhou) */

    return (r < 0) ? r : 0;
}

/*
 * Aloca o buffer auxiliar xbuf usado nas conversões de pixel (Bayer, Sonix, MJPEG...).
 * Deve ser chamada depois de cam_init_device(), pois depende de buffer_maxsize.
 */
int cam_allocate_xbuf(viod *vd) {

  vd->xbuf = (unsigned char *)malloc(vd->buffer_maxsize);

  if (!vd->xbuf) {
    vd->sess.err_no  = ENOMEM;          /* registra o motivo no mesmo lugar dos outros erros */
    vd->sess.err_ctx = "malloc (buffer auxiliar xbuf)";
    return V4L2C_ERR_NOMEM;
  }

  return 0;
}

/* Libera o buffer auxiliar xbuf (seguro chamar mesmo se ele não existir). */
int cam_deallocate_xbuf(viod *vd) {

  if (vd->xbuf) {

    free(vd->xbuf);
    vd->xbuf = 0;

  }

  return 0;

}

/* Enfileira os buffers no driver e liga o streaming desta câmera. */
int cam_start_capturing(viod *vd) {

    int r = v4l2c_start(&vd->sess);

    return (r < 0) ? r : 0;
}

/*
 * Laço de captura: entrega frames a cam_on_frame() até thon != 1 ou até um erro.
 * Retorna 0 se parou por pedido (thon = 0); código negativo se falhou
 * (incluindo V4L2C_ERR_TIMEOUT quando a câmera para de enviar frames).
 *
 * Corrige o cam_mainloop() original: lá o retorno de cam_read_frame() era
 * interpretado ao contrário e a função terminava sem 'return'.
 */
int cam_mainloop(viod *vd) {

    int r = v4l2c_run(&vd->sess, cam_keep_running);

    return (r < 0) ? r : 0;
}

/* Desliga o streaming desta câmera (seguro chamar sem ter iniciado). */
int cam_stop_capturing(viod *vd) {

    int r = v4l2c_stop(&vd->sess);

    return (r < 0) ? r : 0;
}

/*
 * Desfaz o mapeamento/alocação dos buffers e os devolve ao driver.
 * Idempotente: pode ser chamada mais de uma vez.
 */
int cam_uninit_device(viod *vd) {

    int r = v4l2c_free_buffers(&vd->sess);

    cam_sync_from_session(vd);          /* buffers = NULL, num_buf = 0 (evita double free em make_camera_list) */

    return (r < 0) ? r : 0;
}

/*
 * Executa o ciclo completo de uma câmera: formato -> buffers -> xbuf -> streaming ->
 * laço -> parar -> liberar. Roda dentro da thread de captura.
 *
 * Diferença em relação à versão anterior: a limpeza (parar, liberar buffers, liberar
 * xbuf) acontece SEMPRE para o que foi alocado, mesmo se uma etapa intermediária
 * falhar, e o primeiro erro fica registrado em vd->sess.err_no/err_ctx.
 */
int capture_pictures_v4l2(viod *vd){

  int r;
  int have_buffers = 0;         /* 1 depois que cam_init_device() teve sucesso     */
  int have_xbuf = 0;            /* 1 depois que cam_allocate_xbuf() teve sucesso   */
  int saved_no;
  const char *saved_ctx;

  /* Prepara a sessão desta câmera: descritor, método de I/O e quem recebe os frames. */
  v4l2c_session_init(&vd->sess, vd->fid);
  vd->sess.io       = vd->io;
  vd->sess.on_frame = cam_on_frame;
  vd->sess.user     = vd;

  r = cam_def_buffer_maxsize(vd);

  if (r == 0) {
    r = cam_init_device(vd);
    if (r == 0) {
      have_buffers = 1;
    }
  }

  if (r == 0) {
    r = cam_allocate_xbuf(vd);
    if (r == 0) {
      have_xbuf = 1;
    }
  }

  if (r == 0) {
    r = cam_start_capturing(vd);
  }

  if (r == 0) {
    r = cam_mainloop(vd);       /* só volta quando thon = 0 ou em erro */
  }

  /* Guarda o primeiro erro: as funções de limpeza abaixo podem sobrescrevê-lo. */
  saved_no  = vd->sess.err_no;
  saved_ctx = vd->sess.err_ctx;

  if (have_buffers) {
    cam_stop_capturing(vd);
    cam_uninit_device(vd);
  }

  if (have_xbuf) {
    cam_deallocate_xbuf(vd);
  }

  if (r < 0) {
    vd->sess.err_no  = saved_no;
    vd->sess.err_ctx = saved_ctx;
  }

  return r;
}

/*
 * Libera no DRIVER os buffers da câmera (REQBUFS com count = 0), usando o tipo de
 * memória correto para o método de I/O. Usada depois de reconfigurar o formato.
 * (A versão anterior passava o enum io_method como tipo de memória, valores diferentes.)
 */
int free_v4l2_video_buffers(viod &vd){

  v4l2_session t;               /* sessão temporária: só carrega fid e io */

  v4l2c_session_init(&t, vd.fid);
  t.io = vd.io;

  return (v4l2c_release_driver(&t) < 0) ? -1 : 0;
}

/* ========================================================================== */
/*  Thread de captura                                                         */
/* ========================================================================== */

/*
 * Limpa o que o processo de imagem ativo criou: mantém só a imagem original
 * (v_mat[0]), apaga as demais, esvazia c_mat e destrói o estado persistente (d_vet).
 */
void erase_process_initialization(viod &vd) {

    /// Apaga do 2º ao último objeto de v_mat (o 1º é a imagem original)
    if (vd.v_mat.size() > 1) {          /* proteção: antes, v_mat vazio causava comportamento indefinido */
        vd.v_mat.erase(vd.v_mat.begin() + 1, vd.v_mat.end());
    }

    /// Apaga todos os objetos de c_mat
    vd.c_mat.clear();

    /// Destrói todos os objetos de d_vet usando o destrutor guardado em cada um
    while( vd.d_vet.size() ){

        vd.d_vet.at(0).pdest(vd.d_vet.at(0).pobj);
        vd.d_vet.erase(vd.d_vet.begin());

    }

}

/*
 * Função da pthread de captura de UMA câmera (criada pela tecla 'C' em multiview.cpp).
 * Marca a câmera como CAPTURING, zera os cronômetros, roda o ciclo de captura e, ao
 * terminar (pedido de parada ou erro), devolve a câmera ao estado DEV_OPEN.
 */
void* cam_thread_v4l2(void *vd){

  viod *p = (viod*) vd;
  int r;
  Mat im(p->h,p->w,CV_8UC3);            /* v_mat[0]: imagem original no tamanho atual */

  p->thon = 1;                          /* sinaliza que a thread está ativa */
  p->st = DEV_CAPTURING;

  gettimeofday(&(p->tm.t0),NULL);       /* todos os cronômetros começam no mesmo instante */

  p->tm.t1 = p->tm.t0;
  p->tm.t2 = p->tm.t0;
  p->tm.t3 = p->tm.t0;
  p->tm.t4 = p->tm.t0;
  p->tm.t5 = p->tm.t0;
  p->tm.t6 = p->tm.t0;
  p->tm.tx = p->tm.t0;

  ///p->v_mat.push_back(im);


    /// Correção: v_mat[0] recebe o frame RGB convertido (convertToRgb) e sobrevive
  /// entre capturas, pois erase_process_initialization apaga só do 2º elemento em
  /// diante. Antes, "im" era sempre acrescentado com push_back e v_mat[0] ficava
  /// com o tamanho da captura anterior: ao trocar a resolução, convertToRgb
  /// escrevia uma imagem de tamanho novo num buffer de tamanho antigo.
  /// Agora "im" (já alocado com p->w x p->h atuais) substitui v_mat[0]. A
  /// atribuição de cv::Mat não copia pixels: v_mat[0] passa a compartilhar o
  /// buffer de "im" (contagem de referências) e o buffer antigo é liberado
  /// quando ninguém mais o referencia.
  if (p->v_mat.empty())
      p->v_mat.push_back(im);
  else
      p->v_mat.at(0) = im;




  r = capture_pictures_v4l2(p);         /* bloqueia aqui até thon = 0 ou erro */

  if (r < 0) {
    cam_report_error(p);                /* só esta câmera para; o programa continua */
  }

  erase_process_initialization((*p));

  /* Volta ao estado "aberta": pode ser iniciada de novo ou removida pelo hot-plug. */
  p->thon = 0;
  p->st = DEV_OPEN;
  CLEAR(p->tm);
  p->procinit = 0;
  p->procidx = 0;
  p->img_proc = 0;
  p->nv = 0;

  return 0;
}

/*
 * Configura formato/resolução/intervalo da câmera interativamente (menus no terminal)
 * e deixa a câmera no estado DEV_CONFIGURED.
 * Retorna 0 em sucesso, -1 se o driver recusar (a câmera continua como estava).
 */
int setting_camera_features(viod &vd){

    struct v4l2_frmivalenum fl;
    v4l2_session t;             /* sessão temporária só para aplicar o formato */

    if (vd.fid < 0) {

        return -1;              /* antes: exit(1), que derrubava todas as câmeras */

    }

    CLEAR(fl);                  /* antes não era zerada: 'fl.discrete' podia ter lixo */

    choose_or_enumerate_image_formats(vd.fid,&fl,0);
    choose_or_enumerate_frame_size(vd.fid,&fl,0);
    choose_or_enumerate_frame_intervals(vd.fid,&fl,0);

    v4l2c_session_init(&t, vd.fid);

    if (v4l2c_set_format(&t, fl.pixel_format, fl.width, fl.height) < 0 ||
        v4l2c_set_interval(&t, fl.discrete.numerator, fl.discrete.denominator) < 0) {

        errno = t.err_no;                   /* mostra o motivo e segue vivo */
        explain_v4l2_error(t.err_ctx);
        return -1;

    }

    /* Guarda o formato que o DRIVER aceitou (pode diferir do pedido). */
    vd.w = t.w;
    vd.h = t.h;
    vd.pxfmt = t.pxfmt;

    free_v4l2_video_buffers(vd);            /* libera buffers antigos do driver, se houver */
    vd.st = DEV_CONFIGURED;

    return 0;
}

/* ========================================================================== */
/*  Seleção e exibição                                                        */
/* ========================================================================== */

/* Retorna o índice da câmera que está em exibição (view != 0) ou -1 se nenhuma. */
int view_on(vector<viod*> &vv){

    int d;
    vector<viod*>::iterator it;

    d = -1;
    it = vv.begin();

    while( it != vv.end() ) {

        if ((*it)->view != 0) {

            d = distance(vv.begin(), it);

        }

        it++;

    }

    return d;
}

/* Desliga a exibição de todas as câmeras e retorna o índice da que estava ligada (ou -1). */
int stop_view(vector<viod*> &vv){

    int d;
    vector<viod*>::iterator it;

    d = -1;
    it = vv.begin();

    while( it != vv.end() ) {

        if ((*it)->view != 0) {

            d = distance(vv.begin(), it);

        }

        (*it)->view = 0;
        it++;

    }

    return d;
}

/* Retorna 1 se alguma câmera está sendo exibida (view == 1), senão 0. */
int is_displaying(vector<viod*> &vv){

    vector<viod*>::iterator it;

    for (it = vv.begin(); it != vv.end(); it++) {

        if ( (*it)->view == 1) {

            return 1;

        }

    }

    return 0;

}

/* ========================================================================== */
/*  Lista de câmeras e hot-plug                                               */
/* ========================================================================== */

/*
 * Relê o formato atual (largura, altura, pixel format) direto do driver e o grava em viod.
 * Retorna 0 em sucesso, -1 se o driver não responder.
 */
int update_cam_format(viod *p_vio){

    v4l2_session t;             /* sessão temporária: não mexe na sessão de captura */

    v4l2c_session_init(&t, p_vio->fid);

    if (v4l2c_get_format(&t) < 0) {

        return -1;

    }

    p_vio->h = t.h;
    p_vio->w = t.w;
    p_vio->pxfmt = t.pxfmt;

    return 0;
}

/*
 * Abre a câmera de índice 'i' (da lista de dispositivos de vídeo encontrados),
 * descobre se está livre e a acrescenta ao vetor vv.
 *
 * Roda a cada varredura de hot-plug, então NÃO pode encerrar o programa: se a câmera
 * sumiu entre a listagem e a abertura, ou não há permissão, ela é simplesmente ignorada.
 * Retorna 0 se a câmera foi adicionada, -1 se foi ignorada.
 */
int add_camera(vector<viod*>& vv, int i){

    viod *p_vio;
    v4l2_id *p_vid;
    struct v4l2_capability vcap;
    char npath [256];

    p_vid = (v4l2_id *)get_v4l2_id(i);

    if (!p_vid) {

        return -1;              /* a lista mudou: índice não existe mais */

    }

    p_vio = new viod;

    (p_vio->vid).typ = p_vid->typ;
    (p_vio->vid).num = p_vid->num;

    make_v4l2_path(npath,(p_vio->vid).typ,(p_vio->vid).num);

    p_vio->fid = v4l2c_open(npath);     /* antes: open_v4l2_device(), que dava exit() em qualquer falha */

    if (p_vio->fid < 0) {

        delete p_vio;
        return -1;

    }

    p_vio->io = cam_probe_io(p_vio->fid);

    if (p_vio->io == CAM_IO_BUSY) {
        p_vio->st = DEV_BUSY;           /* outro programa está usando esta câmera */
    }
    else {
        p_vio->st = DEV_OPEN;
    }

    CLEAR(vcap);

    if(-1 == v4l2c_xioctl(p_vio->fid,VIDIOC_QUERYCAP,&vcap)){

        v4l2c_close(p_vio->fid);        /* antes: errno_exit(), que derrubava o programa */
        delete p_vio;
        return -1;

    }

    /* 'card' tem 32 bytes e pode vir sem '\0' final; dvnm também tem 32. */
    strncpy(p_vio->dvnm,(char *)vcap.card,sizeof(p_vio->dvnm) - 1);
    p_vio->dvnm[sizeof(p_vio->dvnm) - 1] = '\0';

    if (-1 == update_cam_format(p_vio)) {

        v4l2c_close(p_vio->fid);        /* não é câmera de captura: descarta */
        delete p_vio;
        return -1;

    }

    vv.push_back(p_vio);

    return 0;
}

/*
 * Sincroniza o vetor de câmeras com o que existe em /dev agora (hot-plug):
 *   - câmera que sumiu: pede parada da thread, espera, libera tudo e remove da lista;
 *   - câmera que continua: reavalia se está livre ou ocupada por outro programa;
 *   - câmera nova: abre e acrescenta.
 */
int make_camera_list(vector<viod*>& vv) {

    int dev_num;
    int i;
    int cur;
    char ck [MAX_V4L2_DEV_NUM];     /* ck[i] = 1 se o dispositivo i da varredura já está em vv */
    viod* vp;

    vector<viod*>::iterator it;

    memset(ck,0,MAX_V4L2_DEV_NUM);

    dev_num = make_video_list();    /* varre /dev e guarda a lista no v4l2misc */

    if (!vv.empty()) {

        it = vv.begin();

        while(it != vv.end()){

            if (!found_in_list((*it)->vid,ck)) {

                /* ---- a câmera desapareceu: encerra e libera ---- */
                vp = (viod *)*it;
                vp->view = 0;
                vp->thon = 0;                       /* pede à thread de captura para terminar */

                while (vp->st > DEV_CONFIGURED) {   /* espera sair de OUR/CAPTURING/PROCESSING */
                    sleep(1);
                }

                cam_deallocate_xbuf(vp);
                cam_uninit_device(vp);              /* idempotente; antes havia free() manual com risco de double free */
                v4l2c_close(vp->fid);

                delete vp;

                it = vv.erase(it);                  /* erase() devolve o próximo iterador válido (antes ficava inválido) */
            }
            else {

                /* ---- a câmera continua: atualiza se está livre ou ocupada ---- */
                if ((*it)->st < DEV_CAPTURING) {    /* não mexe em câmeras em captura. Antes: '< A|B|C', sempre verdadeiro */

                    cur = (*it)->st;

                    if (cur == DEV_BUSY || cur == DEV_OPEN || cur == DEV_CONFIGURED) {

                        (*it)->io = cam_probe_io((*it)->fid);

                        if ((*it)->io == CAM_IO_BUSY) {

                            (*it)->st = DEV_BUSY;

                            if (cur != DEV_CONFIGURED) {
                                update_cam_format((*it));   /* mostra a resolução atual de câmera ocupada */
                            }

                        }
                        else {

                            /* Livre: mantém CONFIGURED se já estava configurada, senão OPEN. */
                            (*it)->st = (cur == DEV_CONFIGURED) ? DEV_CONFIGURED : DEV_OPEN;

                        }
                    }
                }

                it++;

            }
        }

        /* ---- câmeras novas: as que a varredura achou e ainda não estão em vv ---- */
        for (i = 0; i < dev_num; i = i + 1) {

            if (ck[i] == '\0') {

                add_camera(vv,i);

            }

        }

    }else {

        /* Lista vazia: toda câmera encontrada é nova. */
        for (i = 0; i < dev_num; i = i + 1) {

            add_camera(vv,i);

        }

    }

    return 0;
}

/*
 * Imprime a lista de câmeras cujo estado casa com 'dvst' (DEV_ALL = todas).
 * Retorna quantas câmeras foram mostradas.
 */
int show_camera_list(vector<viod*>& vv, int dvst){

    int r;
    int i;
    int cnt;
    vector<viod*>::iterator it;
    vector<viod*>::iterator itf;
    char name [256];
    struct v4l2_capability vcap;

    i = 0;
    cnt = 0;
    SCRCLR();

    if (!vv.size()) {
        puts("\tThere is no video device to show.\n");
        cnt = 0;
    }
    else {
        puts("\n\nVideo Cameras List:\n");
        it = vv.begin();
        itf = vv.end();

        while (it != itf) {
            if (dvst == DEV_ALL || ((*it)->st & dvst) != 0){

                sprintf(name, "/dev/%s%d", prefixes[(*it)->vid.typ], (*it)->vid.num);
                r = v4l2c_xioctl((*it)->fid,VIDIOC_QUERYCAP,&vcap);
                if (r == 0) {
                    printf("  %d: %s: %s, %s, %s\n", i, prefixes[(*it)->vid.typ], name, vcap.card, cam_status[(*it)->st]);
                    cnt = cnt + 1;
                    it++;
                    i = i + 1;
                }else{
                    it++;       /* sem resposta do driver: pula (antes ficava em laço infinito) */
                    i = i + 1;
                }

            }else{

                it++;
                i = i + 1;

            }
        }
    }

    return cnt;
}

/* Pede a todas as threads de captura que terminem (thon = 0). Não espera por elas. */
void stop_all_threads(vector<viod*> &vv) {

    vector<viod*>::iterator it;

    for (it = vv.begin(); it != vv.end(); it++) {

        (*it)->thon = 0;

    }

}

/// ***********************************************************************************
/// ************* CONTROLES DA CÂMERA EM PERCENTUAL (uma tabela por câmera) *************
/// ***********************************************************************************

/*
 * Prepara a tabela da câmera para uso: sincroniza o descritor atual (vd.fid pode ter
 * mudado se a câmera foi reaberta) e verifica que ele é válido. Quem chama já deve
 * estar com vd.ctl_mutex travado.
 */
static int cam_ctl_prepare(viod &vd) {

    if (vd.fid <= 0)
        return V4L2CTL_ERR_ARG;         /* câmera não está aberta */
    vd.ctl.fid = vd.fid;
    return V4L2C_OK;

}

/*
 * Lê do driver os controles desta câmera e monta vd.ctl. Pode ser chamada de novo a
 * qualquer momento (ex.: depois de reabrir a câmera). Devolve quantos controles achou.
 */
int cam_ctl_populate(viod &vd) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);       /* só uma thread mexe na tabela por vez */

    if (vd.fid <= 0)
        return V4L2CTL_ERR_ARG;
    return v4l2ctl_populate(&vd.ctl, vd.fid);

}

/* Ajusta o controle "name" para pct por cento da faixa. */
int cam_ctl_set_pct(viod &vd, const char *name, double pct) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;

    int idx = v4l2ctl_find_name(&vd.ctl, name);
    if (idx < 0) return V4L2CTL_ERR_NOFIND;
    return v4l2ctl_set_pct(&vd.ctl, idx, pct);

}

/* Lê o valor atual do controle "name" como percentual da faixa. */
int cam_ctl_get_pct(viod &vd, const char *name, double *pct) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;

    int idx = v4l2ctl_find_name(&vd.ctl, name);
    if (idx < 0) return V4L2CTL_ERR_NOFIND;
    return v4l2ctl_get_pct(&vd.ctl, idx, pct);

}

/* Soma delta_pct ao controle "name"; garante andar pelo menos um passo. Devolve o novo % em *new_pct. */
int cam_ctl_adjust_pct(viod &vd, const char *name, double delta_pct, double *new_pct) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;

    int idx = v4l2ctl_find_name(&vd.ctl, name);
    if (idx < 0) return V4L2CTL_ERR_NOFIND;
    return v4l2ctl_adjust_pct(&vd.ctl, idx, delta_pct, new_pct);

}

/* Devolve o controle "name" ao valor de fábrica. */
int cam_ctl_reset(viod &vd, const char *name) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;

    int idx = v4l2ctl_find_name(&vd.ctl, name);
    if (idx < 0) return V4L2CTL_ERR_NOFIND;
    return v4l2ctl_reset_default(&vd.ctl, idx);

}

/*
 * Copia a tabela inteira (já com os últimos valores conhecidos) para 'out'. A interface
 * lista a cópia sem segurar o mutex, então não atrasa a thread que está ajustando.
 */
int cam_ctl_snapshot(viod &vd, v4l2ctl_set *out) {

    if (!out) return V4L2CTL_ERR_ARG;
    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    *out = vd.ctl;                                      /* cópia por valor: a estrutura é só dados */
    return out->n;

}

/*
 * Propósito: relê do driver valores e flags (ativo/inativo/preso) de TODOS os controles
 * desta câmera. Necessário depois de qualquer mudança, porque um controle pode ativar ou
 * desativar outros (ex.: exposição automática libera/bloqueia o tempo de exposição).
 */
int cam_ctl_refresh(viod &vd) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;
    return v4l2ctl_refresh_all(&vd.ctl);

}

/*
 * Propósito: soma delta_pct (pode ser negativo) ao controle de índice idx da tabela.
 * Anda no mínimo um passo (ou um item, se for menu). Nos limites o valor não muda.
 */
int cam_ctl_adjust_idx(viod &vd, int idx, double delta_pct) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;
    return v4l2ctl_adjust_pct(&vd.ctl, idx, delta_pct, NULL);

}

/*
 * Propósito: tecla única para controles liga/desliga e menus.
 *   booleano : inverte o valor.
 *   botão    : aciona o botão.
 *   menu     : avança para o próximo item válido; no último volta ao primeiro.
 * Outros tipos devolvem V4L2CTL_ERR_ARG (use cam_ctl_adjust_idx).
 */
int cam_ctl_toggle_idx(viod &vd, int idx) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;
    if (idx < 0 || idx >= vd.ctl.n) return V4L2CTL_ERR_INDEX;

    const v4l2ctl_item &it = vd.ctl.it[idx];            /// referência: set_raw atualiza 'cur' na própria tabela
    __s64 cur;
    r = v4l2ctl_get_raw(&vd.ctl, idx, &cur);            /// valor real do driver, não o último conhecido
    if (r < 0) return r;

    if (it.type == V4L2_CTRL_TYPE_BUTTON)
        return v4l2ctl_trigger(&vd.ctl, idx);           /// botão: o ato de acionar é o efeito

    if (it.type == V4L2_CTRL_TYPE_BOOLEAN)
        return v4l2ctl_set_raw(&vd.ctl, idx, cur ? 0 : 1);

    if (it.type == V4L2_CTRL_TYPE_MENU || it.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
        r = v4l2ctl_adjust_pct(&vd.ctl, idx, 100.0, NULL);      /// próximo item válido no sentido de subida
        if (r < 0) return r;
        if (vd.ctl.it[idx].cur == cur)                          /// já estava no último: volta ao primeiro
            return v4l2ctl_set_raw(&vd.ctl, idx, it.min);
        return V4L2C_OK;
    }
    return V4L2CTL_ERR_ARG;

}

/* Propósito: devolver ao valor de fábrica o controle de índice idx. */
int cam_ctl_reset_idx(viod &vd, int idx) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;
    return v4l2ctl_reset_default(&vd.ctl, idx);

}

/*
 * Propósito: devolver TODOS os controles desta câmera ao valor de fábrica, na ordem certa
 * (valores, modos, valores). Devolve quantos controles graváveis não voltaram (0 = tudo certo).
 */
int cam_ctl_reset_all(viod &vd) {

    std::lock_guard<std::mutex> lk(vd.ctl_mutex);
    int r = cam_ctl_prepare(vd);
    if (r < 0) return r;
    return v4l2ctl_reset_all(&vd.ctl);

}
