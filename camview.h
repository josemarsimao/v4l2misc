/*
 *  V4L2 video capture My tests
 *
 *	Nome: Josemar Simão
 */

/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */


#ifndef CAMVIEW_H_INCLUDED
#define CAMVIEW_H_INCLUDED



#include <vector>
using namespace std;


#include "v4l2misc.h"

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

#define CREATE_DANY(obj,T)    {std::addressof(*obj), [](const void* x) { static_cast<const T*>(x)->~T(); } }

typedef struct _video_io_device{

    pthread_t           tid = 0;                            /// Thread identifier
    v4l2_id             vid;                                /// v4l2 device type and number
    int                 fid = 0;                            /// file identifier
    enum io_method      io;                                 /// data transfer method
    int                 st;                                 /// device status
    vbuff               *buffers = 0;                       /// pointer for vector of buffers structs
    int                 bon;                                /// number of actived buffer
    unsigned int        num_buf;                            /// total buffers number
    int                 w;                                  /// width of image
    int                 h;                                  /// height of image

    void (*img_proc)(struct _video_io_device&) = 0;         /// pointer for process function
    int                 procidx = 0;                        /// process index on process vector

    unsigned int        buffer_maxsize;                     /// max buffer size
    int                 view = 0;                           /// set for camera displayed
    int                 thon = 0;                           /// set if thread actived (capturing or processing)

    unsigned char*      xbuf = 0;                           /// extra buffer for convertions

    tmstat              tm;                                 /// time struct to register events

    vector<Mat>         c_mat;                              /// images for calculations only
    vector<Mat>         v_mat;                              /// images for calculations and display
    vector<dany>        d_vet;                              /// pointers to objects

    int                 nv = 0;                             /// number view - number of image to show
    int                 procinit = 0;                       /// control process initialization

    __u32               pxfmt = 0;                          /// pixel format
    ///char                pxdes[32];                          /// pixel description
    char                dvnm[32];                           /// device name

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
void* cam_thread_opencv(void* vd);
int make_camera_list(vector<viod*>& vv);
int show_camera_list(vector<viod*>& vv, int dvst);
int is_displaying(vector<viod*> &vv);
int setting_camera_features(viod &vd);
int capture_pictures_v4l2(viod *vd);
int capture_pictures_opencv(viod &vd);
void cam_process_image(viod &vd);
int free_v4l2_video_buffers(viod &vd);
int cam_uninit_device(viod *vd);
int cam_stop_capturing(viod *vd);
int stop_view(vector<viod*> &vv);
void stop_all_threads(vector<viod*> &vv);
int cam_deallocate_xbuf(viod &vd);
void erase_process_initialization(viod &vd);

void get_errno_description();

#endif // CAMVIEW_H_INCLUDED
