#include "camview.h"



/// STATUS
/*
#define        DEV_NONE         0
#define        DEV_BUSY         1
#define        DEV_OPEN         2
#define        DEV_CONFIGURED   4
#define        DEV_OUR          8
#define        DEV_CAPTURING    16
#define        DEV_PROCESSING   32
#define        DEV_ALL          64
*/
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

int cam_init_dma(viod *vd) {

  return -1;

}

int cam_init_userp(viod *vd){

    struct v4l2_requestbuffers req;

    memset(&req,0,sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;


    if (-1 == xioctl(vd->fid, VIDIOC_REQBUFS, &req)) {

        if (EINVAL == errno) {

            fprintf(stderr, "Device does not support user pointer i/o\n");
            exit(EXIT_FAILURE);

        } else {

            errno_exit("error in memory buffer allocation VIDIOC_REQBUFS");

        }
    }

    if (req.count < 2) {

        free_v4l2_video_buffers((*vd));
        fprintf(stderr, "Insufficient buffer memory on device\n");
        exit(EXIT_FAILURE);

    }

    vd->buffers = (vbuff *)calloc(4,sizeof(vbuff));

    if (!vd->buffers){

        free_v4l2_video_buffers((*vd));
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);

    }

    for (vd->num_buf = 0; vd->num_buf < 4; vd->num_buf++) {

        vd->buffers[vd->num_buf].length = vd->buffer_maxsize;
        vd->buffers[vd->num_buf].start = malloc(vd->buffer_maxsize);

        if (!vd->buffers[vd->num_buf].start) {

            fprintf(stderr, "Out of memory\n");
            while(vd->num_buf){

                vd->num_buf--;
                free(vd->buffers[vd->num_buf].start);

            }
            free(vd->buffers);
            free_v4l2_video_buffers((*vd));
            exit(EXIT_FAILURE);

        }


    }

    return 0;
}



int cam_init_mmap(viod *vd){

    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;

    memset(&req,0,sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(vd->fid, VIDIOC_REQBUFS, &req)) {

        if (EINVAL == errno) {

            fprintf(stderr, "Device does not support memory mapping\n");
            exit(EXIT_FAILURE);

        } else {

            errno_exit("error in memory buffer allocation VIDIOC_REQBUFS");

        }

    }

    if (req.count < 2) {

        free_v4l2_video_buffers((*vd));
        fprintf(stderr, "Insufficient buffer memory on device\n");
        exit(EXIT_FAILURE);

    }

    vd->buffers = (vbuff *)calloc(req.count,sizeof(vbuff));

    if (!vd->buffers){

        free_v4l2_video_buffers((*vd));
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);

    }

    for(vd->num_buf = 0; vd->num_buf < req.count; vd->num_buf++ ){

        memset(&buf,0,sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = vd->num_buf;

        if (-1 == xioctl(vd->fid,VIDIOC_QUERYBUF,&buf))
            errno_exit("error reading memory buffer features VIDIOC_QUERYBUF");



        vd->buffers[vd->num_buf].length = buf.length;
        vd->buffers[vd->num_buf].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, vd->fid, buf.m.offset);


        if (vd->buffers[vd->num_buf].start == MAP_FAILED){

            fprintf(stderr, "Memory mapping error\n");

            while (vd->num_buf) {

                if (-1 == munmap(vd->buffers[vd->num_buf].start,vd->buffers[vd->num_buf].length)) {

                    errno_exit("error when unmapping memory");

                }

                vd->num_buf--;

            }

            free(vd->buffers);
            free_v4l2_video_buffers((*vd));
            exit(EXIT_FAILURE);

        }

        if (vd->buffer_maxsize < buf.length) {

            vd->buffer_maxsize = buf.length;

        }

    }

    return 0;
}




int cam_init_read(viod *vd){

    vd->buffers = (vbuff *)calloc(1,sizeof(vbuff));

    if (!vd->buffers){
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    vd->buffers->length = vd->buffer_maxsize;

    vd->buffers->start = malloc(vd->buffer_maxsize);

    if (!vd->buffers->start) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

  return 0;
}


int cam_init_device(viod *vd){

  switch (vd->io) {

        case IO_METHOD_READ:
            return cam_init_read(vd);

        case IO_METHOD_MMAP:
            return cam_init_mmap(vd);

        case IO_METHOD_USERPTR:
            return cam_init_userp(vd);

        case IO_METHOD_DMABUF:
            return cam_init_dma(vd);

  }

  return -1;

}


int cam_def_buffer_maxsize(viod *vd){

  struct v4l2_format fmt;

  memset(&fmt,0,sizeof(fmt));

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if(-1 == xioctl(vd->fid,VIDIOC_G_FMT,&fmt)){

    puts("error reading format features");
    errno_exit("VIDIOC_G_FMT");

  }

  vd->buffer_maxsize = fmt.fmt.pix.sizeimage;

  return 0;
}


int cam_allocate_xbuf(viod *vd) {

  vd->xbuf = (unsigned char *)malloc(vd->buffer_maxsize);

  if (!vd->xbuf) {
    return -1;
  }

  return 0;
}


int cam_start_capturing(viod *vd) {

    unsigned int i;
    int er = 0;
    enum v4l2_buf_type type;
    struct v4l2_buffer buf;


    switch (vd->io) {
        case IO_METHOD_READ:
            /* Nothing to do. */
            break;

        case IO_METHOD_MMAP:
            for (i = 0; i < vd->num_buf; ++i) {

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                if (-1 == xioctl(vd->fid, VIDIOC_QBUF, &buf)) {

                    errno_exit("error in VIDIOC_QBUF");
                    er = 1;

                }

            }

            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if (-1 == xioctl(vd->fid, VIDIOC_STREAMON, &type)) {

                errno_exit("error starting stream VIDIOC_STREAMON");

                if (ENOSPC == errno) {

                    errno_exit("There is no bandwidth enough. Try other formar, size or another controller");
                    er = 1;

                }

            }

            if (!er) {

                cam_stop_capturing(vd);
                cam_uninit_device(vd);
                return -1;

            }

            break;

        case IO_METHOD_USERPTR:

            for (i = 0; i < vd->num_buf; ++i) {

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;
                buf.index = i;
                buf.m.userptr = (unsigned long)vd->buffers[i].start;
                buf.length = vd->buffers[i].length;

                if (-1 == xioctl(vd->fid, VIDIOC_QBUF, &buf)) {

                    errno_exit("error in VIDIOC_QBUF");
                    er = 1;

                }

            }

            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if (-1 == xioctl(vd->fid, VIDIOC_STREAMON, &type)) {

                errno_exit("error starting stream VIDIOC_STREAMON");
                errno_exit("There is no bandwidth enough. Try other formar, size or another controller");
                er = 1;

            }

            if (!er) {

                cam_stop_capturing(vd);
                cam_uninit_device(vd);
                return -1;

            }

            break;

        case IO_METHOD_DMABUF:

            return -1;


    }

    return er;
}


int cam_read_frame(viod *vd) {

    struct v4l2_buffer buf;
    unsigned int i;

    switch (vd->io) {

        case IO_METHOD_READ:

            if (-1 == read(vd->fid,vd->buffers->start,vd->buffers->length)) {

                switch (errno) {

                    case EAGAIN:
                        return -2;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("read");

                }

                return -2;

            }

            vd->bon = 0;
            cam_process_image((*vd));

            return 0;

        case IO_METHOD_MMAP:

            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (-1 == xioctl(vd->fid, VIDIOC_DQBUF, &buf)) {

                switch (errno) {
                    case EAGAIN:
                        errno_exit("error found");
                        return -2;

                    case EIO:
                        // Could ignore EIO, see spec.

                        // fall through

                    default:
                        errno_exit("VIDIOC_DQBUF");
                }

                errno_exit("error found");

            }

            assert(buf.index < vd->num_buf);

            vd->bon = buf.index;
            vd->buffers[vd->bon].binf = buf;      /// included to store byteused information

            if (vd->buffer_maxsize < buf.bytesused) {
                puts("erro size buffer");
            }

            cam_process_image((*vd));

            if (-1 == xioctl(vd->fid, VIDIOC_QBUF, &buf)) {

                switch (errno) {
                    case EAGAIN:
                        errno_exit("error found");
                        return -2;

                    case EIO:
                        // Could ignore EIO, see spec.

                        // fall through

                    default:
                        errno_exit("VIDIOC_QBUF");
                }

                errno_exit("error found");
                return -2;

            }

            return 0;

        case IO_METHOD_USERPTR:
            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;

            if (-1 == xioctl(vd->fid, VIDIOC_DQBUF, &buf)) {
                switch (errno) {
                    case EAGAIN:
                        errno_exit("error found");
                        return -2;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("VIDIOC_DQBUF");
                }
                printf("error found\n");

                return -2;

            }

            for (i = 0; i < vd->num_buf; i++)
                if (buf.m.userptr == (unsigned long)vd->buffers[i].start && buf.length == vd->buffers[i].length)
                    break;

            assert(i < vd->num_buf);

            vd->bon = i;
            vd->buffers[i].binf = buf;              /// included to store byteused information

            if (vd->buffer_maxsize < buf.bytesused) {
                puts("erro size buffer");
            }

            cam_process_image((*vd));

            if (-1 == xioctl(vd->fid, VIDIOC_QBUF, &buf)) {

                switch (errno) {
                    case EAGAIN:
                        errno_exit("error found");
                        return -2;

                    case EIO:
                        // Could ignore EIO, see spec.

                        // fall through

                    default:
                        errno_exit("VIDIOC_QBUF");
                }

                errno_exit("error found");
                return -2;

            }

            return 0;


        case IO_METHOD_DMABUF:

            return -2;


    }

    return 1;



}



int cam_mainloop(viod *vd) {


    int r;
    struct timeval tv;
    fd_set fds;

    for( ; ; ){
        if (vd->thon != 1) {
            return 0;
        }

        FD_ZERO(&fds);
        FD_SET(vd->fid, &fds);

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        r = select(vd->fid + 1, &fds, NULL, NULL, &tv);

        if(-1 == r){

            if(EINTR == errno) continue;
            errno_exit("select");
            return -2;

        }


        if(0 == r){

            fprintf(stderr, "select timeout\n");
            exit(EXIT_FAILURE);

        }

        if (cam_read_frame(vd)){

            break;

        }else{


        }

    }

}


int cam_stop_capturing(viod *vd) {

    enum v4l2_buf_type type;

    switch (vd->io) {
        case IO_METHOD_READ:
            /* Nothing to do. */
            return 0;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (-1 == xioctl(vd->fid, VIDIOC_STREAMOFF, &type)) {

                switch (errno) {
                    case EBADF:
                        errno_exit("error found: EBADF");
                        return -2;

                    case ENODEV:

                        errno_exit("error found: ENODEV");
                        return -2;

                    default:
                        errno_exit("VIDIOC_QBUF");
                }

                errno_exit("error in stop stream VIDIOC_STREAMOFF\n");
                return -2;

            }

            return 0;

        case IO_METHOD_DMABUF:

            return -2;

    }

}


int cam_uninit_device(viod *vd) {

    unsigned int i;
    struct v4l2_requestbuffers req;

    switch (io) {

        case IO_METHOD_READ:

            free(vd->buffers[0].start);

            if(vd->buffers) {

                free(vd->buffers);
                vd->buffers = 0;

            }

            return 0;

        case IO_METHOD_MMAP:

            for (i = 0; i < vd->num_buf; i++){

                if (-1 == munmap(vd->buffers[i].start, vd->buffers[i].length)) {

                    errno_exit("error when unmapping memory");
                    return -2;

                }

            }

            /// ***********************************************
            /// ************ DEALLOCATE DRIVER BUFFER *********
            /// ***********************************************

            if (-1 == free_v4l2_video_buffers((*vd))) {

                return -2;

            }

            if (vd->buffers) {

                free(vd->buffers);
                vd->buffers = 0;

            }

            return 0;

        case IO_METHOD_USERPTR:

            for (i = 0; i < vd->num_buf; i++){

                free(vd->buffers[i].start);
                vd->buffers[i].start = 0;
                vd->buffers[i].length = 0;
                memset(&vd->buffers[i].binf,0,sizeof(v4l2_buffer));

            }

            if(vd->buffers) {

                free(vd->buffers);
                vd->buffers = 0;

            }

            return 0;

        case IO_METHOD_DMABUF:

            return -2;

    }

}


int cam_deallocate_xbuf(viod *vd) {

  if (vd->xbuf) {

    free(vd->xbuf);
    vd->xbuf = 0;

  }

  return 0;

}


int capture_pictures_v4l2(viod *vd){

  int r;

  r = cam_def_buffer_maxsize(vd);
  if (r == 0) {
    r = cam_init_device(vd);
  }
  if (r == 0) {
    r = cam_allocate_xbuf(vd);
  }
  if (r == 0) {
    r = cam_start_capturing(vd);
  }
  if (r == 0) {
    r = cam_mainloop(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_stop_capturing(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_uninit_device(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_deallocate_xbuf(vd);
  }

  return r;
}


void erase_process_initialization(viod &vd) {

    vector<Mat>::iterator it;
    vector<Mat>::iterator itf;

    /// It erases from 2nd to last object of v_mat
    it = vd.v_mat.begin();
    it++;
    itf = vd.v_mat.end();
    vd.v_mat.erase(itf, it);

    /// It erases all objetcts of c_mat
    vd.c_mat.clear();

    /// It erases all objects of d_vet
    while( vd.d_vet.size() ){

        vd.d_vet.at(0).pdest(vd.d_vet.at(0).pobj);
        vd.d_vet.erase(vd.d_vet.begin());

    }

}



void* cam_thread_v4l2(void *vd){

  viod *p = (viod*) vd;
  Mat im(p->h,p->w,CV_8UC3);

  gettimeofday(&(p->tm.t0),NULL);

  p->v_mat.push_back(im);

  capture_pictures_v4l2(p);
  erase_process_initialization((*p));


  return 0;
}




int free_v4l2_video_buffers(viod &vd){

  int r;

  struct v4l2_requestbuffers req;


  memset(&req,0,sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = vd.io;
  req.count = 0;

  r = xioctl(vd.fid, VIDIOC_REQBUFS, &req);

  if (r == -1) {

    puts("error in check_io_method_busy function");

  }

  return r;
}


int setting_camera_features(viod &vd){


  struct v4l2_frmivalenum fl;


  if (vd.fid < 0) {
    exit(1);
  }

  choose_or_enumerate_image_formats(vd.fid,&fl,0);
  choose_or_enumerate_frame_size(vd.fid,&fl,0);
  choose_or_enumerate_frame_intervals(vd.fid,&fl,0);
  set_image_format(vd.fid,fl.pixel_format);
  set_image_size(vd.fid,fl.width,fl.height);
  set_image_intervals(vd.fid,fl.discrete.numerator, fl.discrete.denominator);
  vd.w = fl.width;
  vd.h = fl.height;
  vd.pxfmt = fl.pixel_format;

  free_v4l2_video_buffers(vd);

  vd.st = DEV_CONFIGURED;


  return 0;
}




int stop_view(vector<viod*> &vv){



  int d;

  vector<viod*>::iterator __first;
  vector<viod*>::iterator it;
  vector<viod*>::iterator itf;
  long local_10;


  d = -1;
  it = vv.begin();
  itf = vv.end();

  while( it != itf ) {


    if ((*it)->view != 0) {
      __first = vv.begin();

      d = distance(__first, it);



    }

    (*it)->view = 0;

    it++;

  }



  return d;
}


int is_displaying(vector<viod*> &vv){

  vector<viod*>::iterator it;
  vector<viod*>::iterator itf;

  it = vv.begin();
  itf = vv.end();

  do {

    if ((*it)->view == 1) {
      return 1;
    }

    it++;


  } while( it != itf );

  return 0;

}




int update_cam_format(viod *p_vio){

    struct v4l2_format fmto;
    CLEAR(fmto);
    fmto.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(p_vio->fid, VIDIOC_G_FMT, &fmto)) {

        errno_exit("VIDIOC_G_FMT");

    }

    p_vio->h = fmto.fmt.pix.height;
    p_vio->w = fmto.fmt.pix.width;
    p_vio->pxfmt = fmto.fmt.pix.pixelformat;

    return 0;
}

int add_camera(vector<viod*>& vv, int i){

    viod *p_vio;
    v4l2_id *p_vid;
    struct v4l2_capability vcap;
    char npath [256];


    p_vio = (viod *)new(viod);

    p_vid = (v4l2_id *)get_v4l2_id(i);
    (p_vio->vid).typ = p_vid->typ;
    (p_vio->vid).num = p_vid->num;

    make_v4l2_path(npath,(p_vio->vid).typ,(p_vio->vid).num);


    p_vio->fid = open_v4l2_device(npath);


    p_vio->io = check_io_method_busy(p_vio->fid);


    if (p_vio->io == ~IO_METHOD_READ) {
        p_vio->st = DEV_BUSY;
    }
    else {
        p_vio->st = DEV_OPEN;
    }

    memset(&vcap,0,sizeof(vcap));

    if(-1 == xioctl(p_vio->fid,VIDIOC_QUERYCAP,&vcap)){
        errno_exit("VIDIOC_QUERYCAP");
    }

    strcpy(p_vio->dvnm,(char *)vcap.card);

    if (-1 == update_cam_format(p_vio)) {
        close(p_vio->fid);
        if (p_vio) {
          delete(p_vio);

        }
    }
    else {
        vv.push_back(p_vio);
    }

    return 0;
}


int make_camera_list(vector<viod*>& vv) {

    int dev_num;
    int i;
    int cnt;

    vector<viod*>::iterator it;
    vector<viod*>::iterator itf;

    viod* vp;

    char ck [MAX_V4L2_DEV_NUM];

    memset(ck,0,MAX_V4L2_DEV_NUM);

    dev_num = make_video_list();

    if (!vv.empty()) {

        it = vv.begin();
        itf = vv.end();

        while(it != itf){

            if (!found_in_list((*it)->vid,ck)) {

                vp = (viod *)*it;
                vp->view = 0;
                vp->thon = 0;

                while (it, 4 < (*it)->st) {
                    sleep(1);
                }

                if (vp->xbuf) {
                    cam_deallocate_xbuf(vp);
                }

                if (vp->buffers) {
                    free(vp->buffers);
                    vp->buffers = 0;
                }

                close_v4l2_device(vp->fid);

                if (*it) {

                    delete(*it);

                }

                vv.erase(it);
            }
            else {

                if ((*it)->st < DEV_CAPTURING|DEV_CONFIGURED|DEV_BUSY) {

                    switch((*it)->st) {

                        case DEV_BUSY:

                            (*it)->io = check_io_method_busy((*it)->fid);

                            if ((*it)->io == ~IO_METHOD_READ) {

                                (*it)->st = DEV_BUSY;
                                update_cam_format((*it));

                            }
                            else {

                                (*it)->st = DEV_OPEN;

                            }

                            break;

                        case DEV_OPEN:

                            (*it)->io = check_io_method_busy((*it)->fid);

                            if ((*it)->io == ~IO_METHOD_READ) {

                                (*it)->st = DEV_BUSY;
                                update_cam_format((*it));

                            }
                            else {

                                (*it)->st = DEV_OPEN;

                            }
                            break;

                        case DEV_CONFIGURED:

                            (*it)->io = check_io_method_busy((*it)->fid);

                            if ((*it)->io == ~IO_METHOD_READ) {

                                (*it)->st = DEV_BUSY;

                            }else {

                                (*it)->st = DEV_CONFIGURED;

                            }
                    }
                }

                it++;

            }
        }

        for (i = 0; i < dev_num; i = i + 1) {

            if (ck[i] == '\0') {

                add_camera(vv,i);

            }

        }

    }else {

        for (i = 0; i < dev_num; i = i + 1) {

            add_camera(vv,i);

        }

    }

    return 0;
}




int show_camera_list(vector<viod*>& vv, int dvst){

    unsigned char bnum;
    const char *status;
    int r;
    int sz;

    int i;
    int cnt;
    int err;
    vector<viod*>::iterator it;
    vector<viod*>::iterator itf;
    char name [256];
    struct v4l2_capability vcap;

    i = 0;
    cnt = 0;
    printf("\x1b[H\x1b[J");
    sz = vv.size();
    if (sz == 0) {
        puts("\tThere is no video device to show.\n");
        cnt = 0;
    }
    else {
        puts("\n\nVideo Cameras List:\n");
        it = vv.begin();
        itf = vv.end();
        do{
            bnum = (*it)->vid.num;
            sprintf(name,"/dev/%s%d",prefixes[(*it)->vid.typ], bnum);
            r = xioctl((*it)->fid,VIDIOC_QUERYCAP,&vcap);
            if (r == 0) {
                status = cam_status[(*it)->st];
                printf("  %d: %s: %s, %s, %s\n", i, prefixes[(*it)->vid.typ], name, vcap.card, status);
                cnt = cnt + 1;
                i = i + 1;
            }

            it++;

        }while(it != itf);
    }
}


void stop_all_threads(vector<viod*> &vv) {

    vector<viod*>::iterator it;
    vector<viod*>::iterator itf;

    it = vv.begin();
    itf = vv.end();

    while (it != itf) {

        (*it)->thon = 0;
        it++;

    }

}
