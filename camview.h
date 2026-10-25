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
 *  camview.h - API da biblioteca MULTICÂMERA (viod = uma câmera + sua thread).
 *  Etapa 3: o estado V4L2 de cada câmera passou para a v4l2_session 'sess'
 *  (núcleo v4l2core); os campos antigos de viod viram espelho dela.
 */


#ifndef CAMVIEW_H_INCLUDED
#define CAMVIEW_H_INCLUDED



#include <vector>
using namespace std;

#include <atomic>
#include <mutex>          /// std::mutex (tm_mutex): antes dependia de quem incluía camview.h
#include <pthread.h>      /// pthread_t (tid)
#include <sys/time.h>     /// struct timeval (tmstat)

#include "v4l2core.h"
#include "v4l2enum.h"       /// descoberta de câmeras (make_video_list, prefixes...)
#include "v4l2ctl.h"        /// controles em percentual (v4l2ctl_set), uma tabela por câmera
#include "v4l2term.h"       /// SCRCLR

/// STATUS de cada câmera (valores de viod.st). Antes ficavam em v4l2misc.h; pertencem ao multicâmera.
#define        DEV_NONE         0
#define        DEV_BUSY         1
#define        DEV_OPEN         2
#define        DEV_CONFIGURED   4
#define        DEV_OUR          8
#define        DEV_CAPTURING    16
#define        DEV_PROCESSING   32
#define        DEV_ALL          64

#include "opencv2/opencv.hpp"
#include "opencv2/imgproc/types_c.h"
using namespace cv;



typedef struct _timestat{

    struct timeval t0;
    struct timeval t1;
    struct timeval t2;
    struct timeval t3;
    struct timeval t4;
    struct timeval t5;
    struct timeval t6;
    struct timeval tx;
    int cnt = 0;
    int fr = 0;

} tmstat;



// reference about Destructors, lambda and etc
// https://isocpp.org/wiki/faq/dtors
// https://stackoverflow.com/questions/2225330/member-function-pointers-with-default-arguments
// https://stackoverflow.com/questions/7905272/why-is-taking-the-address-of-a-destructor-forbidden
// https://docs.microsoft.com/en-us/cpp/cpp/destructors-cpp?view=vs-2019
// https://herbsutter.com/2016/09/25/to-store-a-destructor/
// https://docs.microsoft.com/en-us/cpp/cpp/lambda-expressions-in-cpp?view=vs-2019

/*
template<class T> class shadowT
{
    public:
        T* pobj;
        shadowT(T* p){ pobj = p;}
        ~shadowT(){
            (*pobj).~T();
        }
};
*/



/*
class shadow
{
    public:
        void* pobj;
        pF pdest = 0;
        shadow(void* pv, pF p ){
            pobj = pv;
            pdest = p;
        }
        ~shadow(){
            pdest();
        }
};
*/

typedef struct _dany{           // Destroy any object
    const void* pobj;
    void(*pdest)(const void*);  // Function pointer for a function that is used to encapsulate the destruction function
} dany;

#define CREATE_DANY(obj,T)    {std::addressof(*obj), [](const void* x) { delete static_cast<const T*>(x); } }

typedef struct _video_io_device{

    pthread_t           tid = 0;             /// ID da pthread de captura desta câmera (uma thread por câmera, criada em 'C'apture)
    v4l2_id             vid;                  /// tipo e número do dispositivo (ex: /dev/videoN)
    int                 fid = 0;              /// file descriptor do dispositivo aberto (retorno de open())
    int      io;                   /// método de transferência de buffer (mmap, read, userptr)
    std::atomic<int>    st;                   /// status atual: DEV_NONE/BUSY/OPEN/CONFIGURED/OUR/CAPTURING/PROCESSING/ALL
                                               /// (ver v4l2misc.h). std::atomic: lido pela thread de captura e pela UI
    vbuff               *buffers = 0;         /// vetor de buffers de captura mapeados (mmap) do driver v4l2
    int                 bon;                  /// índice do buffer atualmente ativo/em uso dentro de 'buffers'
    unsigned int        num_buf;              /// número total de buffers alocados pelo driver
    int                 w;                    /// largura da imagem capturada (pixels)
    int                 h;                    /// altura da imagem capturada (pixels)

    void (*img_proc)(struct _video_io_device&) = 0;   /// ponteiro para a função de processamento ativa (um de vet_of_funcs em multiview.cpp);
                                                        /// 0 = sem processamento, só captura crua
    int                 procidx = 0;          /// índice do processo ativo em vet_of_funcs (0-5 = process000..process005; ver multiview.cpp)

    unsigned int        buffer_maxsize;       /// tamanho máximo em bytes de um buffer de captura
    std::atomic<int>                 view{0};             /// 1 = esta é a câmera sendo exibida no momento (só uma por vez; ver view_on/stop_view)
    std::atomic<int>                 thon{0};             /// 1 = thread de captura ativa (setar 0 sinaliza pra thread encerrar - ver tecla 'E'/Exit)

    unsigned char*      xbuf = 0;             /// buffer auxiliar usado nas conversões de formato de pixel (YUYV/Bayer/MJPEG -> RGB)

    tmstat              tm;                   /// timestamps para medir tempo de captura/processamento/renderização (estatísticas de FPS)

    vector<Mat>         c_mat;                /// imagens intermediárias só para cálculo (não exibidas); limpo a cada troca de processo
    vector<Mat>         v_mat;                /// imagens exibíveis: v_mat[0] é sempre a imagem original (RGB/BGR);
                                               /// v_mat[1+] são criadas por cada process00X conforme sua necessidade (ex: HSV, mapa de detecção).
                                               /// 'nv' escolhe qual delas é mostrada na tela
    vector<dany>        d_vet;                /// estado persistente por-câmera do processo ativo (histogramas, classificadores, retângulos
                                               /// rastreados etc.) - ver CREATE_DANY. Limpo junto com c_mat/v_mat em erase_process_initialization()
                                               /// sempre que o processo é trocado ou a captura reinicia

    std::mutex          tm_mutex;              /// sincronisa o cálculo dos atrasos na thread de exibição com o registro dos tempos na thread de captura

    std::atomic<bool>   roi_request{false};   /// captura pede: "preciso de uma ROI selecionada"
    std::atomic<bool>   roi_ready{false};      /// main avisa: "pronto, resultado disponível em roi_result"
    cv::Rect            roi_result;            /// retângulo escolhido pela main - só lido pela captura depois de ver roi_ready==true

    int                 nv = 0;               /// índice de qual imagem de v_mat é exibida no momento (seta esquerda/direita troca)

    int                 procinit = 0;         /// 0 = process00X ainda precisa rodar sua inicialização (1x); 1 = já inicializado, roda só o loop por-frame

    __u32               pxfmt = 0;            /// formato de pixel reportado pelo driver v4l2 (FOURCC, ex: YUYV, MJPG)
    ///char                pxdes[32];          /// descrição textual do pixel format (desativado)
    char                dvnm[32];              /// nome do dispositivo (ex: "video0")
    /// ---------------------------------------------------------------------
    /// Etapa 3: o motor de captura (buffers, streaming, leitura) está em v4l2core.
    /// 'sess' é a FONTE ÚNICA do estado V4L2 desta câmera. Os campos fid, io, buffers,
    /// bon, num_buf, buffer_maxsize, w, h e pxfmt acima continuam existindo como ESPELHO
    /// (multiview.cpp e process00X.cpp os leem); camview.cpp os atualiza a partir de
    /// 'sess' (cam_sync_from_session). Não escreva neles durante a captura.
    /// 'io' agora é int: guarda um io_method (>= 0), -1 = ocupada, -2 = erro.
    /// 'view' e 'thon' são atomic: lidos por threads diferentes (UI e captura).
    /// ---------------------------------------------------------------------
    v4l2_session        sess{};

    /// ---------------------------------------------------------------------
    /// Controles da câmera (brilho, contraste, exposição...), ajustados em PERCENTUAL
    /// da faixa min..max (ver v4l2ctl.h). Cada câmera tem a sua tabela: antes existia
    /// uma tabela global única, que não servia para várias câmeras.
    /// 'ctl_mutex' serializa o acesso à tabela entre a thread da interface (quem ajusta)
    /// e as demais. Use sempre as funções cam_ctl_* abaixo, que travam o mutex.
    /// ---------------------------------------------------------------------
    v4l2ctl_set         ctl{};
    std::mutex          ctl_mutex;

} viod;


/*
    int x0 = sizeof(vio.tid)        	    8	    8	0x08
    int x1 = sizeof(vio.vid)        	    8	    16	0x10
    int x2 = sizeof(vio.fid)        	    4	    20	0x14
    int x3 = sizeof(vio.io)         	    4	    24	0x18
    int x4 = sizeof(vio.st)         	    4	    28	0x1C
    int x5 = sizeof(vio.buffers)       	    8	    36	0x24
    int x6 = sizeof(vio.bon)        	    4	    40	0x28
    int x7 = sizeof(vio.num_buf)    	    4	    44	0x2C
    int x8 = sizeof(vio.w)          	    4	    48	0x30
    int x9 = sizeof(vio.h)          	    4	    52	0x34
    int x10 = sizeof(vio.img_proc)    	    8	    60	0x3C
    int x11 = sizeof(vio.procidx    	    4	    64	0x40
    int x12 = sizeof(vio.buffer_maxsize)	4	    68	0x44
    int x13 = sizeof(vio.view)          	4	    72	0x48
    int x14 = sizeof(vio.thon)          	4	    76	0x4C
    int x15 = sizeof(vio.xbuf)          	8	    84	0x54
    int x16 = sizeof(vio.tm)            	136	    220	0xDC
    int x17 = sizeof(vio.c_mat)         	24	    244	0xF4
    int x18 = sizeof(vio.v_mat)         	24	    268	0x10C
    int x19 = sizeof(vio.d_vet)         	24	    292	0x124
    int x20 = sizeof(vio.nv)            	4	    296	0x128
    int x21 = sizeof(vio.procinit)      	4	    300	0x12C
    int x22 = sizeof(vio.pxfmt)         	4	    304	0x130
    int x23 = sizeof(vio.dvnm)          	32	    336	0x150
*/





extern const char *cam_status[];






void* cam_thread_v4l2(void* vd);
int make_camera_list(vector<viod*>& vv);
int show_camera_list(vector<viod*>& vv, int dvst);
int is_displaying(vector<viod*> &vv);
int setting_camera_features(viod &vd);
int capture_pictures_v4l2(viod *vd);
/* Processa cada frame recebido (thread de captura). A biblioteca fornece uma versão PADRÃO fraca
 * (camview.cpp); o projeto pode definir a sua própria com esta mesma assinatura. Veja a
 * explicação completa e um exemplo em camview.cpp. */
void cam_process_image(viod &vd);
int free_v4l2_video_buffers(viod &vd);


int stop_view(vector<viod*> &vv);
int view_on(vector<viod*> &vv);
void stop_all_threads(vector<viod*> &vv);

void erase_process_initialization(viod &vd);


int cam_def_buffer_maxsize(viod *vd);
int cam_init_device(viod *vd);
int cam_allocate_xbuf(viod *vd);
int cam_start_capturing(viod *vd);
int cam_mainloop(viod *vd);
int cam_stop_capturing(viod *vd);
int cam_uninit_device(viod *vd);
int cam_deallocate_xbuf(viod *vd);

/// ---------------------------------------------------------------------------
/// Controles da câmera em percentual (thread-safe: cada câmera tem seu mutex).
/// "name" é o nome do controle sem diferenciar maiúsculas ("brightness", "exposure"...).
/// Retornam >= 0 em sucesso e < 0 em erro (V4L2C_ERR_* / V4L2CTL_ERR_*); nunca encerram o programa.
/// ---------------------------------------------------------------------------
int cam_ctl_populate(viod &vd);                                              /// lê os controles do driver; devolve quantos
int cam_ctl_set_pct(viod &vd, const char *name, double pct);                 /// ajusta para pct % da faixa
int cam_ctl_get_pct(viod &vd, const char *name, double *pct);                /// lê como percentual
int cam_ctl_adjust_pct(viod &vd, const char *name, double delta_pct, double *new_pct);  /// soma delta (anda >= 1 passo)
int cam_ctl_reset(viod &vd, const char *name);                               /// valor de fábrica
int cam_ctl_snapshot(viod &vd, v4l2ctl_set *out);                            /// cópia da tabela (para a interface listar sem segurar o mutex)

/// Variantes por ÍNDICE da tabela (usadas pela tela de ajuste, que já conhece o índice do item)
int cam_ctl_refresh(viod &vd);                                               /// relê valores e flags de todos os controles
int cam_ctl_adjust_idx(viod &vd, int idx, double delta_pct);                 /// soma delta % ao item idx (>= 1 passo; menu anda um item)
int cam_ctl_toggle_idx(viod &vd, int idx);                                   /// liga/desliga (bool) ou avança o menu com volta ao início
int cam_ctl_reset_idx(viod &vd, int idx);                                    /// o item idx volta ao valor de fábrica
int cam_ctl_reset_all(viod &vd);                                             /// todos voltam ao valor de fábrica; devolve quantos não voltaram

#endif // CAMVIEW_H_INCLUDED
